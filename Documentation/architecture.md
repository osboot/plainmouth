# Architecture Overview

## 1. Purpose and Scope

This project implements a lightweight widget framework on top of **ncurses** to
build structured, composable text-based user interfaces (TUIs).

Usually, scripts that want to display text dialogue need to agree among
themselves about who currently owns the terminal.

```
+==========+
| terminal | <=> [ script ]
+==========+     [ script (blocked) ]
                 [ script (blocked) ]
```

As a possible solution, could be a creation of a dispatcher that allows to draw
dialogs from different scripts:

```
+==========+     +-------------+
| terminal | <=> | plainmouthd |
+==========+     +-------------+
                    ^ ^ ^
                    | | `-> [ script ]
                    | `---> [ script ]
                    `-----> [ script ]
```

With this architecture, each script creates its own dialog and waits for
the user to finish entering data. The user can switch between dialogs.


## 2. High-Level Architecture

At a high level, the system is structured as a tree of widgets managed by a
central event and rendering loop.

```
+-------------+
| plainmouthd |
+-------------+
    |   +-------------------------+
    `-> | Plugin instance (logic) |
        +-------------------------+
            |   +------------------------------+
            `-> | Widget Tree (layout & state) |
                +------------------------------+
                    |   +---------------------------+
                    `-> | Rendering Stage (ncurses) |
                        +---------------------------+
```

Key architectural principles:

- Widgets form a **hierarchical tree**.
- Each widget is responsible only for its own state and behavior.
- Parent widgets coordinate layout but do not render children directly.
- Rendering ultimately maps to ncurses `WINDOW` operations.


## 3. Core Concepts

The primary goals are:

- Separation of **layout**, **rendering**, and **event handling** concerns.
- Composability of widgets into complex interfaces.
- Deterministic sizing and layout behavior.
- Minimal abstraction overhead over ncurses primitives.

The framework is not intended to hide ncurses, but to provide a disciplined
architectural layer above it.


### 3.1 Widget

A **widget** is the fundamental building block. All UI elements—labels, buttons,
containers, text views—are widgets.

Each widget has:

- A **type** (label, button, container, etc.).
- A **geometry** (position and size).
- Optional **children** (for container widgets).
- A set of **function callbacks** defining behavior.

Conceptually:

```
struct widget {
    type
    geometry
    children[]
    callbacks
    private_data
};
```

Widgets are opaque to their parents except through the public callbacks.


### 3.2 Widget Lifecycle

Widgets follow a strict lifecycle:

```
Creation --> Measure (minimum size) --> Layout (final geometry) --.
                                                                  |
Destruction <------- Event Handling <------- Render (ncurses) <---'
```

This lifecycle is critical for predictable layout behavior.


## 4. Sizing and Layout Model

### 4.1 Measure Phase

The **measure** phase computes the minimum size, preferred (content-based) size
and largest acceptable size of a widget. Only the minimum size is guaranteed;
preferred and maximum sizes are advisory.

Rules:

- Must not depend on parent geometry.
- Must not assume final size.
- Must not modify layout state.


### 4.2 Layout Phase

The **layout** phase assigns the final size and position, based on available
space.

Rules:

- Parent decides how space is distributed
- Child must respect assigned geometry
- Minimum size is guaranteed but may be exceeded

Example for a vertical box (VBox):

```
Parent height
+------------------+
| Child 1          |
+------------------+
| Child 2          |
+------------------+
| Child 3          |
+------------------+
```

The layout phase never performs rendering.


## 5. Rendering Model

### 5.1 Windows and Drawing

Each widget may own or draw into an ncurses `WINDOW`. Rendering uses a top-down
traversal.

```
render (root)
 |
 `-> render (child)
      |
      `-> render (grandchild)
```

Rendering rules:

- Rendering must respect the widget's assigned geometry.
- Widgets must not draw outside their region.
- Containers do not implicitly clip children unless explicitly designed to do so.


### 5.2 Borders and Decorations

Borders are implemented as a separate container widget rather than a visual
effect of specific widgets. This is because borders take up a lot of space on a
text terminal. This way, it is always possible to predict the space consumed by
the border.


## 6. Event Handling

### 6.1 Focus Management

Focus is explicit and managed by the `plainmouthd`. Only the focused widget
receives keyboard events. Plugins do not manage focus directly.

Widgets can provide `input_event(widget, character, keycode)` when they need
to distinguish Unicode characters from ncurses keycodes with the same numeric
value. The server falls back to the existing `input` callback for other
widgets. Tab continues to move focus before either callback is called.


### 6.2 Input Dispatch

Keyboard input flows from the `plainmouthd` into the widget tree:

```
getch()
 |
 `-> Focused widget
      |
      `-> Parent fallback (optional)
```


## 7. Containers

Containers are widgets that manage children.

- VBox (vertical layout)
- HBox (horizontal layout)
- Window (single child with decoration)

Responsibilities:

- Measuring children
- Assigning layout
- Delegating rendering

## 8. Plugin Integration and Isolation

### 8.1 Plugins

Widgets are not used directly by the core `plainmouthd` logic. Instead, they are
instantiated and composed by **plugins**, each plugin being responsible for
constructing its own user interface.

A plugin typically:

- Creates a set of widgets.
- Connects them into one or more widget trees.
- Manages widget-specific state.
- Exposes high-level behavior to the application.

From the framework’s point of view, a plugin is a producer of widget trees,
while `plainmouthd` controls their lifetime, focus, and rendering.


### 8.2 Widget Tree Isolation

A critical architectural rule is that **widgets belonging to different plugin
instances are never merged into a single widget tree**.

Each plugin instance owns one or more *independent root widgets*:

```
 Plugin A     Plugin B     Plugin C
+--------+   +--------+   +--------+
| Root A |   | Root B |   | Root C |
| +----+ |   | +----+ |   | +----+ |
| | A1 | |   | | B1 | |   | | C1 | |
| +----+ |   | +----+ |   | +----+ |
| | A2 | |   | | B2 | |   | | C2 | |
| +----+ |   | +----+ |   | +----+ |
+--------+   +--------+   +--------+
```

There is no shared parent, no common root, and no implicit global widget
hierarchy.

This design enforces **strict isolation**. A widget cannot traverse "up" or
"sideways" into another plugin's widgets. Plugins cannot accidentally depend on
internal structure of other plugins. Multiple instances of the same plugin are
fully isolated from each other.

In particular, this prevents scenarios where one plugin instance could navigate
the widget tree and reach widgets belonging to another plugin instance.

### 8.3 Plugin Event Sources

An instance may expose event sources through `p_pollfds(root, &fds)`.
This optional accessor returns a borrowed array of `struct pollfd` and
its length. It must not mutate instance state, and the descriptors remain
owned by the plugin. Both this accessor and `p_handle_event(root, &fd)`
run in the UI thread. All plugins must be rebuilt against
the updated `struct plugin` definition.

Before each `poll()`, the server copies plugin descriptors into its poll
array alongside the terminal, IPC listener, internal eventfd and the
server's SIGCHLD signalfd. The
event callback receives a copy including `revents`, and must handle
error/hangup conditions as well as normal readiness. Callbacks should use
nonblocking I/O where applicable and bound work per invocation.

The callback returns `P_EVENT_IDLE`, `P_EVENT_REDRAW` or `P_EVENT_ERROR`.
On redraw, the server measures, lays out and renders the instance once
per event batch, then updates the screen. It also checks completion and
wakes result waiters. On error, event dispatch for that instance is
disabled; the plugin must retain the failure in its state if it should
finish and report an error to the client.

Plugin events are dispatched before input and queued IPC tasks, so an
instance cannot be deleted by a queued task while its poll snapshot is
being dispatched. Callbacks only modify their own instance. If multiple
sources fire together, callbacks must account for earlier callbacks in
that batch; descriptor changes take effect on the next poll iteration.
The instance's delete hook closes its descriptors and frees source state.

The tailbox plugin uses a periodic `CLOCK_MONOTONIC` timerfd as its source.
Regular files are not polled directly because EOF still counts as read
readiness. On each timer expiration, tailbox checks the open file and
reads a bounded amount of data into a bounded buffer. File I/O and widget
state changes occur in the UI thread; no additional worker is created.

The server blocks SIGCHLD before creating threads and consumes it through
one nonblocking signalfd. After a notification, it calls the optional
`p_handle_child_event(root)` callback for every active interested instance:
SIGCHLD notifications can coalesce. This callback runs in the UI thread
and returns the same event result as `p_handle_event`. Plugins must check
only their own children with nonblocking wait operations, never reap
arbitrary children of the server. The command child restores its signal
mask before exec.

Termbox exposes its nonblocking PTY master through `p_pollfds` and checks
its direct child with `waitid(..., WNOHANG | WNOWAIT)` in the child callback.
Output is drained independently of child exit, including data accompanying
a hangup. EOF removes the PTY from polling but retains the descriptor until
deletion so it does not prematurely hang up a still-running command.
Deletion terminates the process group and delegates bounded-grace cleanup
and direct-child reaping to a thread. Plugin shutdown joins cleanup threads
before unloading their code. Retaining the waitable child until cleanup
prevents PID reuse during process-group signalling.

Termbox's screen widget is implemented in its plugin and uses libvterm for
screen state and escape-sequence parsing. The common widget library only
knows its widget type; the libvterm dependency is linked into termbox.
The screen is bounded to 65536 cells per screen, including an optional
alternate screen, and retains no scrollback. Rendering occurs in the UI
thread after output has been processed. Terminal replies append raw bytes
to the same bounded write queue used by validated text input; POLLOUT is
requested only while this queue is nonempty. Tab continues to change focus.
Special keys and control characters are encoded by libvterm, so sequences
follow the child's terminal modes. Backspace retains the PTY erase character;
the text setter continues to validate text rather than accept raw sequences.
The daemon uses raw input while a terminal widget has focus, allowing Ctrl-C
and flow-control characters to reach the child. Other widgets use cbreak.

---
