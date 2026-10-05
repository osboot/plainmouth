# plaindialog

`plaindialog` implements a subset of the `dialog` command line on top of
plainmouth plugins. A running `plainmouthd` is required; the client does not
start a daemon or take ownership of the terminal.

Set `PLAINMOUTH_SOCKET` or pass `--socket-file PATH` before the widget option.
For a local build, set `LD_LIBRARY_PATH` to the build directory as well.

Supported invocations:

```sh
plaindialog --msgbox "Message" 6 40
plaindialog --yesno "Continue?" 6 40
plaindialog --stdout --inputbox "Name:" 7 40 "initial value"
plaindialog --stdout --passwordbox "Password:" 7 40
plaindialog --stdout --timebox "Time:" 7 40 12 30 0
plaindialog --stdout --rangebox "Value:" 7 40 -100 100 25
printf '25\n100\n' | plaindialog --gauge "Progress" 7 40 0
plaindialog --stdout --menu "Choose:" 10 40 4 tag1 "First" tag2 "Second"
plaindialog --stdout --checklist "Choose:" 10 40 4 tag1 "First" on tag2 "Second" off
plaindialog --stdout --radiolist "Choose:" 10 40 4 tag1 "First" on tag2 "Second" off
plaindialog --stdout --form "Details:" 10 48 4 \
  "Name:" 1 1 "Alice" 1 8 20 40 "Status:" 2 1 "fixed" 2 8 -10 0
plaindialog --stdout --passwordform "Credentials:" 10 48 4 \
  "Password:" 1 1 "" 1 12 20 40
plaindialog --stdout --mixedform "Account:" 10 48 4 \
  "Name:" 1 1 "Alice" 1 12 20 40 0 "Password:" 2 1 "" 2 12 20 40 1
plaindialog --tailbox /var/log/messages 10 60
plaindialog --textbox /etc/hosts 10 60
plaindialog --termbox 'printf "hello\n"; sleep 2' 10 60
```

Input, password, time, menu, checklist and radiolist results go to stderr by
default, without a trailing newline.
`--stdout` and `--stderr` select the output stream. Menu results contain the
original tag, not the displayed item or numeric option ID. Tags are held by
the client and mapped using the option's creation index.

`--checklist` and `--radiolist` use `TAG ITEM STATUS` triples, where status is
`on` or `off`. Radiolist writes the selected tag. Checklist writes every
selected tag in creation order, quoted and separated by spaces; backslashes
and double quotes inside tags are escaped. An empty checklist writes nothing.

`--separate-output` writes checklist tags without quoting, adding a newline
after every selected tag, including the last. `--output-separator STRING`
(also `--separator STRING`) replaces the delimiter. In separate-output mode
it follows each tag; otherwise it precedes each selected tag, including the
first, and checklist quoting is retained. An empty separator is accepted;
when repeated, the last separator wins. No selection or Cancel writes nothing.

`--separate-output` is accepted only with checklist; other supported widgets
reject it, as in dialog. With radiolist, an explicit separator precedes its
unquoted single-tag result. Menu and other single-value widgets are unaffected
by `--output-separator`. These options must precede the widget option, like
the output-stream options.

`--form TEXT HEIGHT WIDTH FORM_HEIGHT` takes repeated groups of eight
arguments: `LABEL LABEL_Y LABEL_X ITEM ITEM_Y ITEM_X FLEN ILEN`. Coordinates
are one-based; zero and negative coordinates refer to the first row/column,
as in dialog. A positive `FLEN` sets the visible width of an editable field.
Negative `FLEN` creates a read-only field with that absolute width; zero
creates a read-only field sized to its initial text. An empty read-only
field occupies one blank cell. `ILEN` is the maximum number of wide
characters, with zero defaulting to the visible width. Negative limits and
an editable initial value exceeding the limit are rejected.

Form results contain editable values in argument order, without quoting,
followed by a newline after each value, including empty values and the last
one. Read-only values are omitted. `--output-separator` and `--separator`
replace that newline, including with an empty string. Cancel and interruption
produce no values. Explicit `--separate-output` is rejected, as in dialog;
forms already use separate output by default.

`--passwordform` takes the same arguments as `--form` and masks every input,
including read-only fields. Results contain the underlying unmasked values.
`--mixedform` adds an `ITYPE` argument after each field's `ILEN`: 0 is ordinary
input, 1 is masked, 2 is read-only, and 3 is both masked and read-only.
Other flag values are rejected. As in dialog, fields with positive `FLEN`
are emitted even if `ITYPE` makes them read-only; these emit their original
values. Fields with zero or negative `FLEN` are omitted. Both variants share
form geometry, limits, separators and cancellation behavior. Initial secrets
passed as arguments are visible in the process argument list.

`FORM_HEIGHT` specifies the preferred height of the scrollable form area,
including any horizontal scrollbar; zero uses the available space. The area
may shrink to fit the window. Field placement and horizontal scrolling use
plainmouth's positioned layout; Tab moves focus and scrolls the field into
view. Enter keeps the input editable; select OK to submit the form. Navigation
follows plainmouth, including its focus traversal order. Coordinates and
extents are limited to 4096 per axis, with at most 1048576 canvas cells.
Labels and initial values must be valid single-line printable text in the
client's character locale, which should match the daemon's locale.
Input limits use wide characters rather than dialog's byte-based buffers;
initial values are rejected rather than silently truncated.

Button labels can be customized before the widget option:
`--ok-label TEXT`, `--cancel-label TEXT`, `--yes-label TEXT`,
`--no-label TEXT` and `--exit-label TEXT`. Yes/No labels apply only to
yesno; OK/Cancel labels apply to widgets providing those buttons. Textbox
and tailbox use EXIT by default and accept `--exit-label`; termbox uses OK.
An option for a button absent from the widget is ignored. Repeated options
use their last value, and empty labels are accepted. Labels change only the
displayed text; button IDs, result text and exit status are unaffected.

`--no-cancel` (also `--nocancel`) suppresses Cancel in input, password, time,
range, menu, checklist, radiolist and form dialogs. It must precede the widget
option and takes precedence over `--cancel-label`. As in dialog, yesno retains
both Yes and No. Widgets without Cancel are unaffected. Signal interruption
still returns 255 and removes the instance.

`--passwordbox TEXT HEIGHT WIDTH [INIT]` behaves like inputbox but masks its
contents. As with dialog, an initial password is visible in the process argument
list and should normally be avoided.

`--timebox TEXT HEIGHT WIDTH HOUR MINUTE SECOND` writes `HH:MM:SS`. A negative
component selects the corresponding current local-time component. Hours of 24
or greater and minutes or seconds of 60 or greater are rejected.

`--rangebox TEXT HEIGHT WIDTH MIN MAX VALUE` selects a signed integer and
writes it on OK, without a trailing newline. Bounds and initial value must
fit a signed C `int`, with `MIN <= VALUE <= MAX`. Plainmouth presents a
spinbox: Up/Down change its value, numeric input enters positive values,
and Tab moves focus to the buttons. Its presentation differs from dialog's
slider. Cancel emits no result and returns 1.

`--gauge TEXT HEIGHT WIDTH [PERCENT]` displays progress until stdin reaches
EOF, then returns 0 without result text. The initial percentage defaults to
0. Each input line supplies a percentage from 0 to 100. A block consisting
of `XXX`, a percentage, new prompt lines and a closing `XXX` updates both
the percentage and prompt. Updates remain possible after 100%; there are
no buttons or keyboard input. Prompt text is clipped to the available area,
with the progress indicator always on the last content row.
Lines and prompt blocks must be shorter than
8192 bytes. Invalid percentages, NUL bytes, oversized input and incomplete
blocks return 255 and remove the window. A final line without a newline is
accepted. Try `MODE=view tests/e2e-gauge.sh` to see timed updates.

Exit status is 0 for OK/Yes or Enter in an input/menu, 1 for Cancel/No, and
255 for errors or interruption. Cancel/No produces no result text. Created
instances are deleted after completion and on SIGINT, SIGTERM or SIGHUP.
SIGKILL cannot be cleaned up.

`--tailbox FILE HEIGHT WIDTH` reads the file in the server and shows its
latest lines until EXIT is selected. It produces no result text. The file
must be a regular file accessible to the daemon; relative paths are
resolved against the daemon's current directory. Tailbox checks for new
data every 100 ms, retains at most 64 KiB, and reads at most 64 KiB per
notification. Incomplete multibyte characters are held until more bytes
arrive; invalid characters are displayed as `?`.

Tailbox follows the open file descriptor. An observed decrease in file
size clears the buffer and restarts reading. Replacing the pathname does
not switch to the replacement file. Truncation followed by regrowth
between checks may go unnoticed. Left/right arrows or `h`/`l` scroll
horizontally; `0` resets that offset. Vertical position follows the tail.
`--textbox FILE HEIGHT WIDTH` displays a snapshot read by the server. Arrow
keys and Page Up/Down scroll the text; Tab selects the EXIT button. Closing
returns 0 without emitting text. Only regular files up to 65536 bytes are
accepted, with a maximum rectangular content area of 1048576 cells. Oversized
files are rejected rather than silently truncated. Tabs use eight-column stops;
invalid multibyte sequences, NUL and other nonprinting characters become `?`.
Later file changes are not followed. File paths refer to the server's filesystem.

`--tailboxbg` is not yet supported.

`--termbox COMMAND HEIGHT WIDTH` is a plainmouth extension, not a dialog
option. It runs `/bin/sh -c COMMAND` in a PTY with `TERM=xterm`, using the
daemon's permissions, environment and current directory. The PTY size
matches the output viewport. Daemon descriptors are not inherited by the
executed shell. Only run commands you trust.

Termbox uses libvterm (version 0.3 or later) to interpret the command's output
as a screen with the PTY's dimensions. Cursor movement, erasing, line wrapping,
scroll regions and the alternate screen are handled by the emulator. The
renderer supports wide and combining characters, basic foreground/background
colors, bold, underline, italic, blink, reverse and conceal. Extended colors
are approximated with the host terminal's eight basic colors. Screen storage
is bounded to 65536 cells per screen; scrollback history is not retained.
The screen and PTY retain their initial dimensions when the host terminal
resizes. The renderer redraws the screen once per event batch.

When the output view has focus, printable text, Enter, Backspace, Escape,
arrows, Home/End, Insert/Delete, Page Up/Down, function keys and control
characters are forwarded to the command. Libvterm encodes special keys
according to the command's terminal modes, including application cursor
mode. Tab moves focus to the OK button. Alt combinations and modified
special keys are not decoded. The letters `h`, `l` and `0` are ordinary input.
Echo and line editing are provided by the child PTY; no local editing or
echo is added.
While the output view has focus, the daemon uses raw input so that Ctrl-C
and Ctrl-Z reach the child PTY rather than signal the daemon.
In a UTF-8 daemon locale, the child PTY initially enables `IUTF8` so that
Backspace erases a complete UTF-8 character in canonical mode. The command
can change these settings after startup. A cursor is shown at the emulator's
current position while the command is running and requests cursor visibility.
Terminal query replies are queued for nonblocking writes to the PTY using
the same bounded queue as user input. It reads at most 64 KiB per event.
Command exit leaves the screen visible and hides its cursor. Closing with OK
returns 0 and no result text, independently of the command's exit status.

Closing the window closes the PTY and sends SIGHUP to the command's process
group and SIGTERM to the direct child. Cleanup escalates to SIGKILL after
up to 200 ms and reaps the direct child in a cleanup thread. Processes
which leave the command's process group are not managed. An exited child
remains waitable until the window is deleted, preventing its PID from
being reused before process-group cleanup.

The plainmouth result includes `PID`, `RUNNING`, `OUTPUT_CLOSED` and, once
the command exits, `EXIT_STATUS` or `SIGNAL`. PTY output closure and child
exit are independent events. `INPUT_PENDING` reports bytes waiting to be
written. Input writes are nonblocking and retain short writes in a bounded
64 KiB queue. A keyboard character that cannot be queued produces a beep.
Input is rejected after child exit or PTY closure.

Scripts can append input with `plainmouth action=set-value id=ID input=TEXT`.
This accepts printable text, newline/carriage return and backspace/DEL;
backspace is translated to the PTY's initial erase character. Unsupported
controls, invalid/incomplete multibyte text, an overflowing queue or input
combined with `button`/`clicked` are rejected without appending any bytes.
An interactive example is available with
`MODE=view tests/e2e-termbox.sh`.

This is not a complete replacement for dialog. Dimensions and menu height
must be positive integers; automatic sizing, ESC cancellation, dialog
chaining, other widget types, common styling options, `--output-fd`, and
`DIALOG_*` environment overrides are not implemented. Options must precede
the widget; remaining arguments are treated literally. Layout, wrapping
and focus navigation follow plainmouth rather than ncurses dialog.

The integration test can be run interactively:

```sh
MODE=view tests/e2e-plaindialog.sh
```

To check client memory as well as server memory, run the integration test
outside the five-second `make check` timeout:

```sh
CHECK_CLIENT=yes tests/e2e-plaindialog.sh
CHECK_CLIENT=yes tests/e2e-plaindialog-form.sh
```

The argument and result conventions are based on the
[dialog manual](https://www.invisible-island.net/dialog/manpage/dialog.html).
