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

Failed requests return a failed response and an `ERR` pair. The command-line
client prints `ERR=<message>` and exits with status 1. Plugins use the following
messages:

- `field is missing: <field>` for a required field or target.
- `invalid value: <field>` for a malformed integer or boolean.
- `ambiguous target: <target> and <target>` when two targets are supplied.
- `widget not found: <type>=<id>` for an absent typed widget.
- `option not found: option=<id>` for an absent checklist option.
- `unable to decode value: value` when text conversion fails.
- `unable to set value: <field>` when a widget rejects the update.

Integers must fit in a signed C `int` and contain no trailing characters.
Booleans accept `1/0`, `true/false`, and `yes/no` (case-insensitive).
`clicked` and `selected` default to true when omitted. Form input, timebox
spinbox, and meter updates require `value`; an empty text value is valid.
Password updates require `value` or `finished`, and validate both before
changing the widget. Meter and spinbox values retain the widget's range
clamping behavior.

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

An optional `status` immediately following an `option` sets its initial
checked state. Without `status`, the option starts unchecked:

```text
plugin=checklist action=create id=choices width=40 height=7 select=2 visible=3 option=apple status=true option=banana status=false option=orange status=true button=OK
```

`status` accepts `1/0`, `true/false`, and `yes/no` (case-insensitive).
A detached or repeated `status`, malformed boolean, or initial selection
exceeding the `select` limit fails creation. In particular, `select=1`
(radiolist) allows at most one initially checked option. Initial checks do
not change creation-order option IDs or scroll the list.

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

Other plugins expose the following `set-value` fields:

```text
plugin=form:     input=<input-id> value=<text>
plugin=form:     button=<button-id> clicked=<true|false>
plugin=inputbox: value=<text> [finished=<true|false>]
plugin=inputbox: button=<button-id> clicked=<true|false>
plugin=meter:    value=<number>
plugin=menu:     option=<option-id> [finished=<true|false>]
plugin=menu:     button=<button-id> clicked=<true|false>
plugin=msgbox:   button=<button-id> clicked=<true|false>
plugin=password: value=<text> [finished=<true|false>]
plugin=timebox:  spinbox=<spinbox-id> value=<number>
plugin=timebox:  button=<button-id> clicked=<true|false>
```

### inputbox

The `inputbox` plugin displays one unmasked, single-line input. Creation
requires `width` and `height` and accepts `x`, `y`, `border`, `text`,
`label`, `value` (initial contents), `placeholder`, and `tooltip`.
Repeated `button` fields add buttons with 1-based IDs in creation order.
Buttons are optional.

Results contain `INPUT_1=<text>` and `BUTTON_N=0|1` for every button.
Enter in the input or a clicked button completes the instance.
`set-value` accepts `value` and/or `finished` for the input, or
`button=<id>` with optional `clicked` (default true). Input and button
updates must be separate requests. An empty `value` clears the input.
As with password, input values and completion flags are validated before
changing state.

### menu

The `menu` plugin displays one current choice without checkbox markers.
Creation requires `width`, `height`, and at least one repeated `option`
field. It accepts `x`, `y`, `border`, `text`, `visible` (preferred
number of visible rows), and optional repeated `button` fields.
The first option is current initially.

Arrow keys, Page Up/Down, and Home/End move the current choice and keep it
visible. Enter confirms it; buttons can also complete the instance.
Results contain `SELECTED=N`, the 1-based option number in creation order,
and `BUTTON_N=0|1` for each button.

`set-value option=N` changes the current choice and scrolls it into view.
Adding `finished=true` confirms the choice. Button updates use
`button=N` with optional `clicked` (default true), in a separate request.

---
