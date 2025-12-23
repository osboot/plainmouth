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
"sideways" into another plugin’s widgets. Plugins cannot accidentally depend on
internal structure of other plugins. Multiple instances of the same plugin are
fully isolated from each other.

In particular, this prevents scenarios where one plugin instance could navigate
the widget tree and reach widgets belonging to another plugin instance.

---
