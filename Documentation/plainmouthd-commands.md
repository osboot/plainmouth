# plainmouthd Command Interface

This document describes the command set implemented in `plainmouthd`. Commands
are sent by plugin instances to `plainmouthd` and are interpreted as **requests**.

For wire format, framing rules, and full request/response transcripts, see
`Documentation/ipc-protocol.md`.

## Global Commands

### set-title

Defines the title for the global screen that `plainmouthd` uses to render
widgets.

### set-style

Defines the color scheme for different categories and states of widgets.

### hide-splash

The command completely hides the screen with rendered widgets, restoring
the visibility of the terminal.

### show-splash

The commmand restores the screen with rendered widgets.

### has-active-vt

Queries whether an active virtual terminal is available. Returns a boolean
result to the caller. Does not modify UI state.

### ping

Health-check command. Used to verify that the daemon is alive and responsive.
Does not affect UI state.

### quit

Requests `plainmouthd` termination.


## Plugin Commands

These commands manipulate **plugin-owned widget trees**.

### create

Creates a new instance of plugin. Allocates a new root widget. Registers
the dialog within `plainmouthd`.

### update

Updates an existing plugin instance. Applies incremental changes to the dialog
state. Typically triggers re-layout and redraw.

### set-value

Sets a semantic value inside an existing plugin instance. This is intended for
automation and tests; it is not a synthetic keyboard event.

### delete

Deletes the widget tree associated with the plugin instance. Destroys all
widgets owned by the dialog. Releases associated resources.

### focus

Requests keyboard focus for the plugin instance dialog. Focus change is subject
to daemon policy. Does not guarantee immediate focus acquisition.

### result

Sends a result event from the plugin to the daemon. Used to signal completion or
intermediate results.

### wait-result

Blocks until the plugin receives a result event. Used by clients that wait for
user input completion.

## Plugin Result Contracts

### checklist

The `checklist` plugin accepts repeated `option` fields. Each option is assigned
a stable 1-based numeric option id in the order it appears in the create
request. This id is not a visual row number; scrolling, focus movement, or
future rendering changes must not change it.

Results are reported as:

```text
SELECT_<select-id>_OPTION_<option-id>=<0|1>
BUTTON_<button-id>=<0|1>
```

For dialog-compatible clients, tags should be kept by the client and mapped to
`option-id` values. The plugin does not store arbitrary dialog tags.

The plugin supports `set-value` for options and buttons:

```text
action=set-value id=<instance-id> select=<select-id> option=<option-id> selected=<true|false>
action=set-value id=<instance-id> button=<button-id> clicked=<true|false>
```

---
