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
