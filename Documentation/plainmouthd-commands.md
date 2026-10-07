# plainmouthd Command Interface

This document describes the command set implemented in `plainmouthd`. Commands
are sent by plugin instances to `plainmouthd` and are interpreted as **requests**.

For wire format, framing rules, and full request/response transcripts, see
`Documentation/ipc-protocol.md`.

## Global Commands

The daemon starts with `--theme=auto`: a neutral dark palette when ncurses
reports at least 256 colors, otherwise a black-on-white window with dark
inputs and cyan focus using eight colors. Windows remain distinct from the
black screen without borders. `--theme=basic` forces the eight-color palette.
`--theme=terminal` uses the terminal's own foreground and background, with
reverse-video windows to distinguish them from the screen. Terminals without
enough colors or pairs use a monochrome palette with reverse and bold focus.
Palette selection uses ncurses capabilities rather than the TERM name.

### Keyboard Help

F1 opens a panel on the right with commands for the focused widget and
available scrolling commands from its parent. F1 or Esc closes the panel.
The panel follows the dialog's window style and uses the full screen width
when the terminal is narrower than the panel.

Help keeps the current widget focused. While it is open, Up and Down scroll
the help text; other keys still go to the dialog. Tab and Shift-Tab move focus
and update the descriptions, resetting the help scroll position. Close help
to use Up and Down in the dialog again.

Entering a termbox closes help. F1, Esc and arrow keys in termbox continue to
reach the child process.

The daemon option `--animation=MODE` accepts `auto` (default), `none`, and `slide`.
In `auto` mode, animation is enabled for a pseudoterminal with cursor-addressing
support. Linux virtual consoles, serial terminals, unknown devices, and
terminals without cursor addressing use `none`. Detection uses the actual
output TTY, including when it is opened through `/dev/tty` or `/dev/console`,
rather than the TERM name or color count. Explicit `none` and `slide` override
the automatic choice. This does not measure connection speed; a pseudoterminal
over SSH can still have a slow connection.

Currently, `slide` makes help slide in from the right. Set
`--animation-duration=MS` to choose a duration from 0 to 10000 milliseconds
(default: 150); zero shows the panel immediately. Input remains available
during the animation. F1 or Esc closes it immediately, and terminal resizing
finishes the animation. Text wrapping uses the final panel width throughout.

To try it in an interactive test, run
`MODE=view tests/e2e-form.sh` (uses `auto`), or set `ANIMATION=slide` to force
animation. `ANIMATION_DURATION` overrides the duration for these test sessions.

### set-title

Defines the title for the global screen that `plainmouthd` uses to render
widgets.

### set-style

Defines the color scheme for different categories and states of widgets.

`name` selects a global style: `main`, `window`, `button`, `input`, `focus`,
`readonly`, `disabled` or `invalid`.
Set both `fg` and `bg` to change its colors. The optional `attrs` field
replaces its text attributes with a comma-separated list of `bold`, `dim`,
`underline`, `reverse`, `blink`, or `italic`. Use `attrs=normal` to clear
attributes. Attribute names are case-sensitive and contain no spaces.

Colors and attributes can be changed together or separately. Omitted
attributes keep their current value; an attribute-only update keeps colors.
Invalid attributes or colors reject the request before applying the style.
Changes redraw existing visible dialogs and apply to subsequently created
widgets. Attribute appearance depends on the terminal's capabilities.
The command's output screen in `termbox` retains its own terminal attributes.

```sh
plainmouth action=set-style name=focus fg=white bg=green attrs=bold,underline
plainmouth action=set-style name=button attrs=bold
plainmouth action=set-style name=focus attrs=normal
```

The `readonly`, `disabled` and `invalid` roles style read-only fields,
unavailable widgets and rejected keyboard input. By default these use
underline, dim and bold respectively. Rejected input is highlighted until
the next input event or loss of focus; the value stays unchanged.

Add `id=ID` to override the `window`, `button`, `input`, `focus`, `readonly`,
`disabled` or `invalid` role inside one
existing dialog. The `main` role remains global. Local `fg`, `bg`, and `attrs`
are independent: unspecified properties retain earlier local overrides or
inherit the global role if never overridden. Subsequent global changes affect
only the inherited properties. `attrs=normal` is an explicit local override.

Use `reset=true` with `id` and `name` to remove all overrides for that role
and resume inheritance. Reset cannot be combined with colors or attributes.
Overrides are discarded when the dialog is deleted, including any allocated
color pairs. If terminal color pairs are exhausted, a color override fails
without changing the existing style; attribute-only overrides need no pair.

```sh
plainmouth action=set-style id=dialog1 name=window fg=yellow
plainmouth action=set-style id=dialog1 name=focus attrs=bold,underline
plainmouth action=set-style id=dialog1 name=window reset=true
```

Use `style=NAME` instead of `id` to define or update a named theme. `name`
still selects the role, including `input`. A theme inherits
unspecified properties from the global role. The first successful definition
creates the theme; an invalid request does not publish a new theme. Theme
names must be nonempty. `id` and `style` cannot be combined in `set-style`.

Pass `style=NAME` to `create` to select a previously defined theme. The server
validates the name before invoking the plugin, which binds the theme before
its first render. Unknown theme names reject creation. Existing dialogs share
the theme by reference, so subsequent theme changes redraw all its users.
Local instance overrides take priority over theme properties; theme properties
take priority over global properties, independently for foreground, background,
and text attributes.

`reset=true` on a named theme clears that role's overrides while preserving
the theme and its users. An instance reset resumes inheritance from its theme,
or from the global role when no theme was selected. Themes live until server
shutdown, independently of the dialogs using them; their color pairs are
released on role reset or shutdown.

```sh
plainmouth action=set-style style=warning name=window fg=yellow bg=red attrs=bold
plainmouth action=set-style style=warning name=focus fg=black bg=yellow
plainmouth action=create plugin=msgbox id=dialog1 style=warning \
    width=32 height=6 text=Warning button=OK
plainmouth action=set-style style=warning name=window attrs=underline
```

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
Password updates require `value`, `finished`, or a button target, and validate
the request before changing the widget. Password creation accepts `value` for
initial contents and repeated `button` fields. Timebox creation accepts
optional `hour`, `minute`, and `second` values in their normal clock ranges.
Meter and spinbox values retain the widget's range clamping behavior.

### delete

Deletes the widget tree associated with the plugin instance. Destroys all
widgets owned by the dialog. Releases associated resources.

### focus

Requests keyboard focus for the plugin instance dialog. Focus change is subject
to daemon policy. Does not guarantee immediate focus acquisition.

`node-id=ID` targets a public named widget within the specified dialog.
Compose also accepts `node=N`, where N is the declaration-order node number,
not its visual position. Numeric node selectors are rejected by ready-made
dialog plugins. Provide at most one selector. The containing scroll areas are
adjusted to reveal the target. Unknown, disabled, read-only, hidden, or
non-focusable nodes fail without changing focus. Without a selector, the first
interactive widget is selected.
Focus requests do not emit change events.

Ready-made dialogs publish the following node IDs:

| Plugin | Public Node IDs |
| --- | --- |
| `msgbox` | `text`, `button1`, `button2`, ... |
| `inputbox`, `passwordbox` | `input`, `text`, `button1`, `button2`, ... |
| `menubox`, `checklistbox` | `choices`, `text`, `button1`, `button2`, ... |
| `formbox` | `input1`, `input2`, ..., `text`, `button1`, `button2`, ... |
| `timebox` | `hour`, `minute`, `second`, `text`, `button1`, `button2`, ... |
| `rangebox` | `value`, `text`, `button1`, `button2`, ... |
| `textbox`, `tailbox` | `text`, `button1` |
| `termbox` | `terminal`, `button1` |
| `meterbox`, `gaugebox` | `meter`, `text` |

Optional elements have names only when created. Button names follow the order
of `button` fields regardless of labels; single close buttons are `button1`.
Form input names follow field creation order, including disabled and read-only
fields and both plain and password inputs. Labels do not consume input numbers.
Progress meters and static labels are named but are not focusable. For
textviews, `text` names the focusable scrolling area. Other internal containers,
scrollbars, and list options do not receive public names.
These names currently address focus; value and update requests retain each
plugin's existing parameter contract.

### result

Sends a result event from the plugin to the daemon. Used to signal completion or
intermediate results.

### wait-result

Blocks until the plugin receives a result event. Used by clients that wait for
user input completion.

## Plugin Result Contracts

### compose

`action=get-value id=ID node-id=NAME` reads one node in a composed dialog;
`node=N` selects its declaration-order number instead. Exactly one selector
is required. The response contains one `VALUE` pair: input/password text,
checkbox/button/spinner state as 0/1, a 1-based select option, or the current
spinbox/meter number. Password text is returned unmasked, as in `result`.
Spinboxes return the committed value, excluding pending edits.
Reading works before and after completion and for disabled/read-only nodes.
It does not change focus, scroll offsets, button state, or queued events.
Containers, labels, textviews and spacers have no readable value through this
command. Other plugins currently reject `get-value`.

```sh
plainmouth action=get-value id=connection node-id=host
# VALUE=localhost
```

`compose` constructs a fixed widget tree in one `create` request. Window
parameters (`width`, `height`, optional `x`, `y`, `border`, `style`) precede
all nodes. `node=TYPE` opens a node and `node=end` closes it. Nodes are nested:
the currently open container is the parent, so no numeric parent references
are needed. Properties must precede a node's first child; leaves cannot have
children. Exactly one root, a `vbox` or `hbox`, is required. IDs start at 1
and count every opened node, including containers and labels. Tab order
matches declaration order.

Any node may have an optional `node-id=ID`, unique within the dialog. IDs
are case-sensitive, 1..64 ASCII characters from `A-Z`, `a-z`, `0-9`, `_`,
`-`, and `.`. Empty, invalid, or duplicate IDs reject creation. Node names
do not affect declaration-order numbering or result keys.

```sh
plainmouth action=create plugin=compose id=connection width=40 height=7 border=true \
  node=vbox \
    node=hbox \
      node=label text="Host: " node=end \
      node=input value=localhost flex-w=1 node=end \
    node=end \
    node=hbox \
      node=checkbox checked=false node=end \
      node=label text=" Use TLS" node=end \
    node=end \
    node=hbox \
      node=button text=OK node=end \
      node=button text=Cancel node=end \
    node=end \
  node=end
```

Supported node types and their properties:

- `vbox`, `hbox`: containers arranging children vertically or horizontally.
  Optional `gap` (0..4096, default 0) reserves rows or columns between each
  pair of consecutive children, including zero-sized spacers. Empty and
  single-child containers have no gap; there is no extra outer padding.
- `spacer`: an empty, non-focusable leaf. Optional `width` and `height`
  (0..4096, default 0) reserve that minimum size. With `flex-w` or `flex-h`,
  the spacer can also consume remaining space along its parent's main axis.
  It stretches across the other axis and produces no result pairs.
- `scroll`: a container with automatic vertical and horizontal scrollbars.
  Its direct children are arranged vertically; use an `hbox` child for a row.
  Nested scroll containers are supported. Internal pads and scrollbars do
  not receive declaration-order result IDs.
- `label`, `button`: required `text`. Buttons accept `close` (default true);
  `close=false` emits a client event without finishing the dialog.
- `textview`: required `text` (may be empty), displayed without wrapping in a
  scrollable, read-only area. Use `flex-h=1` to fill remaining vertical space.
  The focused area supports arrows, PgUp/PgDown, Home/End and contextual help.
  Replace the text with `action=set-value id=dialog node-id=output value="..."`;
  successful replacement recalculates content dimensions and resets both
  scroll offsets to zero. Text is limited to 4096 characters and the backing
  pad to 1048576 cells. This leaf accepts no child nodes and emits no results.
  Try `MODE=view tests/e2e-compose-textview.sh` for an interactive example.
- `input`, `password`: required `value` (may be empty), optional `max-length`
  (0..65536 characters, default 65536). Passwords are masked on screen but
  returned as ordinary input values.
- `checkbox`: optional `checked` boolean (default false). Add an adjacent
  label in an `hbox` to describe it.
- `select`: repeated `option` labels, optional `visible` (1..256, default 3)
  and `value` (the initial 1-based option number, default 1).
- `meter`: a non-focusable progress indicator. Optional `total`
  (1..2147483647, default 100) and `value` (0..total, default 0).
  It stretches horizontally, occupies one row, and produces no result pairs.
  Reaching `total` does not finish the composed dialog. Change its value with
  `action=set-value id=dialog node-id=progress value=42`; out-of-range values
  are rejected without changing the indicator. `total` is fixed at creation.
- `spinner`: a non-focusable activity indicator occupying one column and row,
  with optional `active` (default false) and `frames=auto|ascii|braille|wave`
  (default auto). An inactive spinner displays a blank without changing its
  minimum size. It produces no result pairs. Start or stop it with
  `action=set-value id=dialog node-id=busy active=true|false`.
  Starting again resets the sequence to its first frame; repeated starts
  leave the current frame unchanged. Frames are fixed at creation.
- `spinbox`: a numeric field with optional `min` (default 0), `max` (default
  100), `step` (default 1), and `value` (default min). Bounds and values must
  fit a signed integer, `min <= max`, and `step` must be positive. The numeric
  width is calculated from both bounds, including the minus sign; brackets
  occupy two additional columns. Up and Down change the value by `step`,
  clamping at the bounds. Digits start a new number, `-` starts a negative
  number when the range permits it, Enter confirms pending digits, and
  Backspace cancels pending input. Enter does not finish the composed dialog.
  `set-value ... node-id=retries value=5` rejects out-of-range values and
  clears pending digits on success. Results include `SPINBOX_N=value` using
  declaration-order IDs. Range and step are fixed at creation.

Try `MODE=view tests/e2e-compose-spinbox.sh` for numeric fields, including a
signed field outside the scroll viewport.

Spinner `auto` chooses Braille when the server's locale uses UTF-8, all frames
have a width of one column, and `TERM` is not `linux`; otherwise it uses
ASCII `|/-\`. Explicit `braille` and `wave` require one-column characters
in the server's locale. Font coverage cannot be detected automatically.
All active spinners in a composed dialog share a 100 ms timer. The timer
stops when no spinners are active, the dialog becomes hidden because the
terminal is too small, or the dialog finishes. Visibility restoration
resumes active spinners. `--animation` does not control activity indicators.
Try `MODE=view tests/e2e-compose-spinner.sh` to compare all three sequences
alongside a progress meter.

All nodes accept `flex-w` and `flex-h` (0..256); defaults are zero except
`flex-h=1` for the root container. Sizing uses the normal measure/layout
flow. At least one button is required. Only clicking a button completes the
dialog when `close=true`; Enter in an input or select does not complete it.
Button meaning is assigned by the client.

Gaps and spacers contribute to minimum geometry and scroll content size.
For example, place a growing spacer before buttons to align them to the
right, with two columns between buttons:

```sh
node=hbox gap=2
  node=spacer flex-w=1 node=end
  node=button node-id=ok text=OK node=end
  node=button node-id=cancel text=Cancel node=end
node=end
```

Spacers receive declaration-order IDs like other nodes. Inserting a spacer
shifts subsequent numeric IDs; use `node-id` for stable update targets.
Gap and spacer dimensions are creation-time properties. Try
`MODE=view tests/e2e-compose-layout.sh` for a scrollable form with spaced,
right-aligned buttons.

All nodes also accept `disabled` and `readonly` booleans (default false).
Both prevent keyboard interaction and exclude the node's descendants from
Tab navigation. Disabled nodes use the disabled style; read-only nodes use
the readonly style. Values remain available in results and may still be
changed by the client with `set-value`.

Use `update` to change either state without recreating the tree:

```sh
plainmouth action=update id=probe node=3 disabled=true
plainmouth action=update id=probe node=3 disabled=false readonly=false
```

Omitted states retain their previous values. State updates require at least
one state and reject invalid, unknown or duplicate parameters before mutation.
Blocking the focused node or its ancestor moves focus to the next available
widget. If none are available, focus is cleared; enabling a node restores
focus only when there is no current focus. Enabling a node does not override
its ancestors' states or its descendants' own states.

For a `select` node, repeated `option=` fields in `update` replace the entire
list. The optional `value=N` selects a 1-based option in the new list; without
it, the first option is selected. The node keeps its ID, keyboard focus,
viewport dimensions and creation-time `visible` setting. Replacement resets
prefix search and scrolls the new selection into view.

```sh
plainmouth action=update id=probe node-id=choices option=Alpha option=Beta value=2
plainmouth action=update id=probe node-id=choices clear=true
```

Without `option=`, updates only change the specified states. Use `clear=true`
to remove every option; an empty select stays focusable and returns `0` from
`get-value` and in results. `clear=true` cannot accompany `option=` or `value=`;
`value=` requires a replacement list. `clear=false` alone is not an update.
List updates may include `disabled` and `readonly`. All parameters and new
options are checked before any change: at most 256 options, at most 4096
characters per option, and each option must fit the existing list viewport.
Errors preserve the old list, selection and states. Replacement and clearing
do not emit change events; pending events still identify the node and
`get-value` reads its current state. Option numbers refer to the new list;
the client maintains any mapping to its own data.

Try `MODE=view tests/e2e-compose-options.sh` for live filtering by a text field.

`set-value` addresses the declaration-order ID with `node=N`:

```sh
plainmouth action=set-value id=connection node=4 value=example.org
plainmouth action=set-value id=connection node=6 checked=true
plainmouth action=set-value id=connection node=9 clicked=true
```

Both `set-value` and `update` also accept `node-id=ID` instead of `node=N`.
Exactly one selector is required. Unknown names and duplicate selectors
reject the request before changing values or states. Names cannot be changed
after creation and can be reused in other dialogs.

```sh
# Declare nodes with: node=label node-id=status ...
# and: node=button node-id=test ...
plainmouth action=set-value id=probe node-id=status text=Connected
plainmouth action=update id=probe node-id=test disabled=true
```

Inputs/passwords accept `value`, checkboxes require `checked`, selects require
`value` (a 1-based option number), and buttons accept `clicked` (default true).
Labels accept `text` up to 4096 characters; replacement text must fit the
label's current width and height. Reserve sufficient space in the initial
label for subsequent status updates. Rejected updates leave the label unchanged.
Changing a node with `set-value` brings it into view through its enclosing
scroll containers. Selecting an off-screen option also scrolls the select's
own list into view. Results use `INPUT_N`,
`CHECKBOX_N`, `SELECT_N`, and `BUTTON_N`, where N is the node ID, not an index
among nodes of the same type. Select results contain option numbers, not
arbitrary client tags. Containers and labels produce no result pairs.

Creation allows at most 256 nodes, 32 tree levels, 256 options per select,
4096 characters per initial text/option, and 1048576 window cells. Window
dimensions are limited to 4096 per axis. Content must fit the window's
minimum geometry; a `scroll` allows its contents to exceed the viewport.
Each backing pad is limited to 4096 per axis and 1048576 cells, checked
before rendering. Unbalanced nodes, multiple roots, children of leaves,
unknown or duplicate properties, and invalid values reject the entire tree
before rendering. The old `node=start type=TYPE parent=N` syntax is not
supported. Inserting nodes requires no changes to parent references; clients
still calculate result IDs from declaration order. Dynamic structural
updates are not supported.

Input, password, checkbox, select and spinbox nodes accept `notify=true`
(default false).
After a user changes their value, `action=wait-event id=ID` returns
`EVENT=change`, `NODE=N` and optional `NODE_ID=ID`. Read the current value
with `action=get-value` using the node selector from the event.
Text insertion (including pasted characters), Delete and Backspace produce
events when they change the text. Cursor movement, rejected input at the
length limit, pending spinbox digits, cancelled numeric edits, and keys
that leave the value unchanged produce no event. `set-value` updates do not
produce change events. `notify` is fixed at creation; disabled and read-only
nodes do not react to input. Passwords follow the same notification rules
as inputs; masking affects rendering only. Event responses contain no text.

`action=wait-event id=ID` also waits for a non-closing button activation and returns
`EVENT=button` and `NODE=N` (the declaration-order ID). An event from
a named button also returns `NODE_ID=ID`; unnamed buttons omit this pair.
The name is captured when the event is queued.
Keyboard activation and `set-value clicked=true` both enqueue events and reset the button
state, allowing repeated clicks. Events are consumed once, in FIFO order;
multiple waiting clients compete for events rather than receiving broadcasts.
Changes and clicks share a FIFO queue. A new change removes any pending
change for the same node after the last queued button and moves the new
notification to the end: change(A), change(B), change(A) becomes
change(B), change(A). Buttons are never combined and form boundaries:
change(A), button(Test), change(A) retains all three events.
An already consumed event cannot be combined. Events contain no value
snapshot; `get-value` returns the current state even across button boundaries.
Events before a waiter connects are retained. Each dialog queues at most 256
events; overflow makes subsequent waits fail explicitly until deletion.
When the queue is empty, finishing/deleting the dialog or stopping the server
wakes waiters with an error. Closing buttons continue to use `wait-result`.

```sh
plainmouth action=create plugin=compose id=probe width=32 height=6 \
  node=vbox \
    node=label node-id=status text="Waiting for connection..." node=end \
    node=button node-id=test text=Test close=false node=end \
    node=button text=OK node=end \
  node=end
plainmouth action=wait-event id=probe
# The client performs its operation, then updates the status label.
plainmouth action=set-value id=probe node-id=status text=Connected
```

Try `MODE=view tests/e2e-compose.sh` for an interactive connection dialog.
For a long form with fixed buttons, try
`MODE=view tests/e2e-compose-scroll.sh`. Put the scroll and button row next
to each other in the root `vbox` so only the form contents move:

```sh
plainmouth action=create plugin=compose id=settings width=40 height=8 border=true \
  node=vbox \
    node=scroll flex-h=1 \
      node=vbox \
        node=input value=first node=end \
        node=input value=second node=end \
      node=end \
    node=end \
    node=hbox \
      node=button text=OK node=end \
      node=button text=Cancel node=end \
    node=end \
  node=end
```

`scroll` takes the common `flex-w` and `flex-h` properties. Use `flex-h=1`
to give it the height remaining after fixed labels and buttons. Tab visits
the scroll container and its fields; focus movement brings fields into view.
On the container, arrows, Home/End and PgUp/PgDown scroll the contents.
Terminal resizing preserves values and keeps the focused field visible.

### checklistbox

The `checklistbox` plugin accepts repeated `option` fields. Each option is assigned
a stable 1-based numeric option id in the order it appears in the create
request. This id is not a visual row number; scrolling, focus movement, or
future rendering changes must not change it.

An optional `status` immediately following an `option` sets its initial
checked state. Without `status`, the option starts unchecked:

```text
plugin=checklistbox action=create id=choices width=40 height=7 select=2 visible=3 option=apple status=true option=banana status=false option=orange status=true button=OK
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

The `textbox` plugin accepts `file=PATH`, `width=`, `height=`, optional `border=`,
`x=`, `y=`, `style=` and `button=LABEL` at creation. It reads a regular file
once (at most 65536 bytes and 1048576 rectangular screen cells), starting at
the top.
`set-value id=ID scroll-x=N scroll-y=N` moves its viewport using nonnegative,
zero-based column/line offsets, clamped to the content. Either offset may be
omitted. `set-value id=ID button=1 [clicked=true|false]` sets the closing button;
the result is `BUTTON_1`. Use scrolling and button fields in separate requests.
Textbox, tailbox and termbox accept a label in the first `button` field at
creation for their single closing button, defaulting to `OK` if absent.
The button retains ID 1, including when its label is empty.

```text
plugin=formbox:     input=<input-id> value=<text>
plugin=formbox:     button=<button-id> clicked=<true|false>
plugin=inputbox: value=<text> [finished=<true|false>]
plugin=inputbox: button=<button-id> clicked=<true|false>
plugin=meterbox:    value=<number>
plugin=menubox:     option=<option-id> [finished=<true|false>]
plugin=menubox:     button=<button-id> clicked=<true|false>
plugin=msgbox:   button=<button-id> clicked=<true|false>
plugin=passwordbox: value=<text> [finished=<true|false>]
plugin=passwordbox: button=<button-id> clicked=<true|false>
plugin=termbox:  input=<text>
plugin=termbox:  button=1 clicked=<true|false>
plugin=timebox:  spinbox=<spinbox-id> value=<number>
plugin=timebox:  button=<button-id> clicked=<true|false>
plugin=rangebox: value=<number>
plugin=rangebox: button=<button-id> clicked=<true|false>
```

For termbox, `input` appends text to the child PTY's bounded write queue.
It accepts printable characters, newline/carriage return and backspace/DEL.
Invalid text, unsupported controls, a full queue, a closed PTY or an exited
command fail the request without appending bytes. Input and button updates
must be separate requests. `INPUT_PENDING` in the result reports queued bytes.

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
As with passwordbox, input values and completion flags are validated before
changing state.

### gaugebox

Create with `plugin=gaugebox text=TEXT width=N height=N value=PERCENT` and
optional `border=true`. Percentages must be between 0 and 100. Use
`action=update id=ID value=PERCENT [text=TEXT]` to update the indicator and
optionally replace its prompt. `action=set-value` accepts the same fields.
There are no buttons or completion result: the client deletes the instance
when its input ends, including after a value of 100.

### rangebox

Creation requires `width`, `height`, `min`, `max`, and `value`. The initial
value must lie within the inclusive signed integer bounds. Optional fields
are `text`, `border`, `x`, `y`, `style`, and repeated `button` labels.
Results contain `VALUE` and `BUTTON_N` states. A clicked button completes
the instance. `set-value value=N` clamps to the configured range; button
and value updates must be separate requests.

### menubox

The `menubox` plugin displays one current choice without checkbox markers.
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

### formbox

The existing `hbox=start/end` layout remains the default. `layout=positioned`
places single-line items at zero-based coordinates in scrollable content:

```sh
plainmouth plugin=formbox action=create id=form1 width=40 height=10 border=true \
  layout=positioned \
  field=start label=Name: x=0 y=0 width=6 field=end \
  field=start input=Alice x=7 y=0 width=20 max-length=40 field=end \
  field=start password=secret x=7 y=12 width=20 readonly=true field=end \
  button=OK button=Cancel
```

Each group requires exactly one `label`, `input` or `password`, plus `x`, `y`
and a positive visible `width`. Optional input parameters are `max-length`
(default: visible width, measured in wide characters) and `readonly` (default:
false), plus `disabled` (default: false). Disabled inputs skip keyboard focus
and input, retain their creation-order result IDs and allow programmatic
`set-value` updates. An editable input's initial value exceeding the limit is rejected. Coordinates and
extents are limited to 4096 per axis and the canvas to 1048576 cells.
Repeated or unknown parameters within a group are rejected.

Input IDs are assigned in creation order, including read-only inputs; labels
do not consume IDs. Read-only inputs cannot receive focus or `set-value` and
are omitted from results. Passwords remain masked even when read-only.
Editable inputs return `INPUT_N`; buttons return `BUTTON_N`. Enter does not
complete an input; a clicked button completes the form. Tab moves focus and
scrolls the focused field into view. `set-value input=N value=...` also scrolls
the field into view, without changing focus. Coordinate placement does not
change result order.

In positioned mode, optional `visible=N` sets the preferred height of the
form area, including scrollbars, and limits its maximum height. Zero (the
default) uses the available space; smaller windows can shrink the area.
Values must lie between 0 and 4096.

---
