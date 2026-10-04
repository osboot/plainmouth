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
plaindialog --stdout --menu "Choose:" 10 40 4 tag1 "First" tag2 "Second"
plaindialog --tailbox /var/log/messages 10 60
plaindialog --textbox /etc/hosts 10 60
plaindialog --termbox 'printf "hello\n"; sleep 2' 10 60
```

Text and menu results go to stderr by default, without a trailing newline.
`--stdout` and `--stderr` select the output stream. Menu results contain the
original tag, not the displayed item or numeric option ID. Tags are held by
the client and mapped using the option's creation index.

Exit status is 0 for OK/Yes or Enter in an input/menu, 1 for Cancel/No, and
255 for errors or interruption. Cancel/No produces no result text. Created
instances are deleted after completion and on SIGINT, SIGTERM or SIGHUP.
SIGKILL cannot be cleaned up.

`--tailbox FILE HEIGHT WIDTH` reads the file in the server and shows its
latest lines until OK is selected. It produces no result text. The file
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
keys and Page Up/Down scroll the text; Tab selects the OK button. Closing
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
```

The argument and result conventions are based on the
[dialog manual](https://www.invisible-island.net/dialog/manpage/dialog.html).
