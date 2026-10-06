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
widgets. Tab moves focus forward and Shift-Tab backward before either callback
is called. Cursor coordinates follow widget ancestry and subtract pad scroll
offsets; a cursor outside a viewport is hidden.

Menu, checklist and radiolist share cursor navigation: Up/Down move one item,
Home/End move to the endpoints, and Page Up/Down move by a visible page.
Navigation keeps the active row visible and does not change checkbox state.
Printable text searches item labels by a case-insensitive prefix. A one-second
pause resets the prefix; repeating a single letter cycles through matches.
The matching prefix is underlined in the active row. The underline disappears
after the timeout, navigation or loss of focus, retaining the row's theme.
If an extended prefix has no match, search retries using the latest character.
Space toggles the active checklist/radiolist item subject to its selection
limit; Enter accepts a menu. Item IDs retain their creation order.

Single-line inputs support Left/Right, Home/End, Backspace and Delete.
Their horizontal viewport stays in place while the cursor remains visible
and counts display columns, including wide and combining characters.
Password fields apply the same editing operations to their masked contents.
When an input has focus, the daemon enables bracketed paste on terminals
supporting it. Pasted tabs and line endings become spaces; other control
characters and decoded function keys are ignored. Paste does not submit the
input or change focus. Input length limits still apply, so excess characters
are rejected. The paste mode is disabled when focus leaves the input, the
terminal is released, or the daemon exits. Without terminal support for
bracketed paste, input retains ordinary keyboard semantics.


### 6.2 Input Dispatch

Keyboard input flows from the `plainmouthd` into the widget tree:

```
getch()
 |
 `-> Focused widget
      |
      `-> Parent fallback (optional)
```


Input widgets expose `PROP_INPUT_MAX_LENGTH` as an `int`, measured in
`wchar_t` elements rather than bytes or terminal columns. The default is
`INT_MAX - 1`; zero allows only an empty value. Negative limits, limits
larger than `INT_MAX - 1`, and limits shorter than the current value are
rejected. Keyboard insertion and `PROP_INPUT_VALUE` both respect the limit;
an oversized replacement leaves the existing value and cursor unchanged.
These properties are also supported by password inputs.

`PROP_INPUT_FINISH_ON_ENTER` is a `bool`, defaulting to true. When false,
Enter is consumed without setting `PROP_INPUT_STATE`, so the field remains
editable. Explicit completion through `PROP_INPUT_STATE` still disables
keyboard editing. The form plugin disables completion on Enter for all its
text and password fields; inputbox and passwordbox keep the default behavior.
The positioned form layout exposes this limit through per-field `max-length`.

Text and password inputs use typed input events so ncurses keycodes are not
inserted as Unicode characters. Unsupported special keys are ignored; Unicode
characters whose numeric values overlap with keycodes remain ordinary text.

## 7. Containers

Containers are widgets that manage children.

- VBox (vertical layout)
- HBox (horizontal layout)
- Positioned (explicit child rectangles, independent of viewport dimensions)

`positioned_add()` transfers ownership of a child on success, preserving insertion
order. It rejects negative coordinates, nonpositive dimensions, already attached
children and overflowing extents. Its measured size is the bounding rectangle of
its children. A positioned container inside a scroll container's pad provides
two-axis scrolling without changing child coordinates. Visibility requests retain
the original target while walking ancestors, including nested containers.

Rendering a child of a pad also redraws its containing viewport. Pad contents
are copied only after children finish drawing, with clipping to the source
dimensions; unused viewport cells retain the viewport background. Pad windows
are not refreshed directly onto the terminal. Scroll containers indicate focus
through their scrollbars.

Focused scroll containers use arrows for single-row or single-column movement,
Page Up/Down for one pad viewport, and Home/End for the vertical endpoints.
Only actual terminal keycodes trigger these commands. Unhandled Up/Down and
Page Up/Down from a field scroll its nearest enclosing scroll container without
moving focus or changing the field value. Lists consume their own navigation
keys, and input fields retain Home/End for editing. Visibility calculations
subtract intervening pad offsets when scrolling nested regions.
For positioned input fields, visibility requests also include the nearest
label to the left on the same row when label and field fit together in the
viewport. Smaller viewports prioritize the input field.

`ATTR_READONLY` and `ATTR_DISABLED` prevent keyboard interaction and focus
traversal, including when set on an ancestor. Style roles `readonly`,
`disabled` and `invalid` follow the same global, instance and named-theme
inheritance as other roles. Input callbacks return a negative value for a
rejected action, zero for an unhandled key and positive for a handled key.
The dispatcher marks rejected actions with `FLAG_REJECTED`; the active
widget retains that visual feedback until its next input or loss of focus.
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

Common window and button operations are provided by `plugin_helpers` in
`libplainmouth.so`. `plugin_create_window` creates a window, assigns its
borrowed theme source, and adds an optional border with a vertical or
horizontal content container. It returns the owned root and a borrowed
content pointer; failure frees the partial tree and clears the content
pointer. Dimension validation, widget composition, layout and rendering
remain in the plugins. Plugins create and attach their own button containers;
`plugin_add_buttons` fills a container from request fields and assigns IDs
in field creation order. On failure, attached buttons remain owned by the
container and are freed with the dialog root. The helpers also set button
state, emit individual button results and check completion by a numbered
button. Plugins retain validation of conflicting targets, their other
completion conditions, and result traversal order.

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

The internal `daemon_event` module collects and frees poll snapshots,
dispatches descriptor and child callbacks, and renders roots with pending
redraws. A snapshot owns both its descriptor array and the parallel owner
array; the instance pointers are borrowed. All operations run in the UI
thread. The main loop dispatches plugin events before input and IPC tasks
can delete their owners, and retains ownership of the SIGCHLD signalfd and
the final screen update. Collection failure leaves an empty snapshot.

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

Global presentation roles can be overridden on a dialog's root widget.
Descendants inherit those overrides through the widget tree; detached tooltip
windows use their owning widget as a style source. Foreground, background and
text attributes inherit independently from the global role until overridden.
Resolution occurs during rendering, so inherited properties follow later
global changes. Reset removes a role's overrides, and deleting the root frees
all local style state.

Named themes are unrendered style-source widgets owned by the server. Each
creation request carries a borrowed reference to its selected theme; plugins
attach that reference to the root before their initial rendering. The server
rejects unknown names before calling the plugin. Resolution follows the style
source chain and merges individual properties in this order: global role,
named theme, local instance overrides. Theme updates redraw their users.
Themes outlive their dialogs and are freed after all instances at shutdown.

The daemon's internal `daemon_style` module owns named sources and handles
style request validation, color and attribute parsing, and applying overrides.
All its operations run in the UI thread. The daemon supplies instance lookup
and redraws the affected roots after a successful update; the style module
does not access the instance list or UI task queue. Borrowed theme sources
remain valid until shutdown, when the daemon frees instances before themes.

The internal `daemon_instance` module owns the instance list, panels, widget
registration and focus state. Its lookup and iteration APIs return borrowed
pointers for the UI thread; callers finish processing a poll snapshot before
deleting its owners. Worker threads only use its blocking wait API, which
keeps instance lookup and completion reads under the module's mutex and
returns no instance pointer. Publication, removal and completion notification
use the same mutex. Plugins and widget operations run in the UI thread;
shutdown frees instances after joining workers and before unloading plugins
or freeing theme sources.

The internal `daemon_task` module owns the UI task queue and its eventfd.
Workers create tasks with borrowed request pointers, submit them, and wait
for each task's own completion flag under the queue mutex. The submitting
worker frees its task after reading the handler's return value; its IPC
message and connection remain alive throughout the wait. The UI thread
dispatches a detached snapshot without holding the mutex during handlers.
It saves the next task pointer before publishing completion, since the
worker may immediately free the completed task. New submissions are handled
in a later dispatch. The queue is freed only after all workers are joined.

The `daemon_worker` module owns accepted IPC contexts and joinable client
threads. Completed workers notify the UI through the task eventfd and are
joined and freed during normal operation. Each worker retains a separate
close-on-exec socket descriptor for shutdown, so an IPC-side close cannot
make shutdown target a reused descriptor. The worker frees its IPC buffers
and messages before publishing completion.

Shutdown first stops task submission and releases queued task waiters, then
releases instance result waiters. It shuts down client sockets to interrupt
blocked reads and writes, joins workers, and finally frees UI resources.
The quit connection requests shutdown at its next IPC loop iteration, after
the protocol response has been sent.

Widgets can expose borrowed key descriptions through `widget_ops.keybindings`.
The collector includes the nearest scrolling parent's vertical navigation
commands, following the same forwarding rules as input dispatch and giving
the focused widget precedence over duplicate keys.

The UI-thread `daemon_help` module owns the context-help window and panel.
It adds global focus-navigation commands and gives help scrolling precedence
over the dialog's Up/Down bindings while open. The panel follows focus without
joining the focusable widget list. It is raised after widget rendering and
released before instance teardown; terminal widgets retain their own keys.

The UI-thread `daemon_animation` module resolves the global animation mode
after ncurses initialization. Auto enables sliding only for a recognized PTY
slave with cursor-addressing support, using `TIOCGDEV` to identify the actual
output device. Explicit modes bypass detection. The help module receives the
resolved mode and duration; the command-line settings can be reused by other
animated UI elements.

Help animation uses monotonic deadlines combined with the list-search
timeout in the main poll loop. Its content is rendered into a full-width pad
and copied into a growing panel window so descriptions do not rewrap between
frames. Closing help cancels its deadline; resize completes the animation.

Local color overrides and termbox screen colors share a bounded color-pair
allocator, excluding the four global role pairs. Colors used by a live widget
are not reassigned to another owner. Pairs are released on reset or widget
deletion. Local color requests fail if no pair is available; terminal screen
colors fall back to the window pair when allocation fails.

---
