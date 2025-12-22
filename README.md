# plainmouth

**plainmouth** is a modular, plugin-based, text-based UI dialog server and
client implemented in C. It allows other applications to display interactive
dialog windows—such as message boxes, forms, and password prompts—inside a
terminal using the `ncurses` library.

## Features

- **Plugin System**: Easily extendable via plugins (e.g., message box, password
  input, forms).
- **Scripting-Friendly**: Includes shell scripts and command-line tools for
  automated testing and embedding into other workflows.
- **Extensible Widgets**: Widgets such as buttons, labels, input fields, text
  views, meters, and more.
- **Keyboard Support**: Handles navigation, focus, key codes, and resizing
  within terminal UIs.

## Example Usage

Start the server:

```sh
plainmouthd -S /tmp/plainmouth.sock --debug-file=/tmp/server.log
```

Create a message box:

```sh
plainmouth plugin=msgbox action=create id=example width=30 height=7 border=true \
  text="Important message." \
  button="OK" \
  button="Cancel"
plainmouth action=wait-result id=example
plainmouth action=delete id=example
plainmouth --quit
```

Create a password prompt:

```sh
plainmouth plugin=password action=create id=pass1 width=30 height=3 border=true \
  label="Enter password:"
plainmouth action=wait-result id=pass1
plainmouth --quit
```

## Building

This project uses `autotools` (autoconf) for configuration and building:

```sh
./autogen.sh
./configure
make
sudo make install
```

Dependencies include:
- `gcc`
- `make`
- `pkg-config`
- `ncurses` and `panel` development libraries
- `pthread`

## License

This software is licensed under the [GNU General Public License v2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).

## Author

Alexey Gladkov.

## Contributing

Contributions are welcome! Feel free to open issues and pull requests.

---
