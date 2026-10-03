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
`--tailboxbg` is not yet supported.

`--termbox COMMAND HEIGHT WIDTH` is a plainmouth extension, not a dialog
option. It runs `/bin/sh -c COMMAND` in a PTY with `TERM=dumb`, using the
daemon's permissions, environment and current directory. The PTY size
matches the output viewport. Daemon descriptors are not inherited by the
executed shell. Only run commands you trust.

Termbox displays the latest lines and supports horizontal scrolling with
left/right arrows. When the output view has focus, printable text, Enter
and Backspace are forwarded to the command. Tab moves focus to the OK
button; other control keys, function keys and escape sequences are not
forwarded. The letters `h`, `l` and `0` are ordinary input. Echo and line
editing are provided by the child PTY; no local editing or echo is added.
It does not emulate a full terminal. It handles UTF-8, carriage returns, backspace
and tabs, and discards escape sequences rather than interpreting cursor
movement or colors. It retains at most 65536 wide characters and reads at
most 64 KiB per event. Command exit leaves the window open. Closing with OK
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
