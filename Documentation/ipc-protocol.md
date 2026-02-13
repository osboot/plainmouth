# plainmouth IPC Protocol

This document describes the low-level request/response protocol used between
`plainmouth` (client) and `plainmouthd` (server).

## 1. Transport

- Unix domain socket: `AF_UNIX`, `SOCK_STREAM`.
- Messages are sent as NUL-terminated frames (C strings).
- One socket connection can carry multiple request/response exchanges.

## 2. Message Flow

Each client request follows this sequence:

1. `HELLO`
2. Server replies: `TAKE <id>`
3. Client sends one or more: `PAIR <id> <key>=<value>`
4. Client sends: `DONE <id>`
5. Server sends zero or more: `RESPDATA <id> <key>=<value>`
6. Server finalizes: `RESPONSE <id> OK` or `RESPONSE <id> ERROR [message]`

## 3. Wire Commands

### Client to Server

- `HELLO`
- `PAIR <id> <key>=<value>`
- `DONE <id>`

### Server to Client

- `TAKE <id>`
- `RESPDATA <id> <key>=<value>`
- `RESPONSE <id> OK`
- `RESPONSE <id> ERROR`

## 4. Grammar (informal)

```text
HELLO
TAKE      <id>
PAIR      <id> <key>=<value>
DONE      <id>
RESPDATA  <id> <key>=<value>
RESPONSE  <id> <status> [message]
```

Notes:
- `<id>` is an opaque token allocated by the peer handling `HELLO`.
- `PAIR` and `RESPDATA` payloads use the first `=` as key/value delimiter.
- Keys should not contain spaces or `=`.
- Values may contain spaces.

## 5. Request Semantics

Application-level operations are transferred as key-value pairs, usually with
an `action` key. Example request payload:

```text
action=create
plugin=msgbox
id=w1
width=40
height=7
text=Important message
button=OK
button=Cancel
```

At protocol level this becomes multiple `PAIR <id> key=value` lines.

## 6. Response Semantics

- `RESPDATA` carries structured result fields.
- `RESPONSE ... OK` marks successful completion.
- `RESPONSE ... ERROR` marks failed completion.
- Server may additionally send `RESPDATA <id> ERR=<message>` before final error.

## 7. Full Example: Create + Wait Result

### 7.1 Create dialog

```text
C> HELLO
S> TAKE 1
C> PAIR 1 action=create
C> PAIR 1 plugin=msgbox
C> PAIR 1 id=w1
C> PAIR 1 width=40
C> PAIR 1 height=7
C> PAIR 1 border=true
C> PAIR 1 text=Important message.
C> PAIR 1 button=OK
C> PAIR 1 button=Cancel
C> DONE 1
S> RESPONSE 1 OK
```

### 7.2 Wait for completion

```text
C> HELLO
S> TAKE 2
C> PAIR 2 action=wait-result
C> PAIR 2 id=w1
C> DONE 2
S> RESPONSE 2 OK
```

### 7.3 Read result fields

```text
C> HELLO
S> TAKE 3
C> PAIR 3 action=result
C> PAIR 3 id=w1
C> DONE 3
S> RESPDATA 3 BUTTON_1=1
S> RESPDATA 3 BUTTON_2=0
S> RESPONSE 3 OK
```

## 8. Error Example

```text
C> HELLO
S> TAKE 4
C> PAIR 4 action=create
C> PAIR 4 id=w2
C> DONE 4
S> RESPDATA 4 ERR=field is missing: plugin
S> RESPONSE 4 ERROR
```

## 9. Action Catalogue

See `Documentation/plainmouthd-commands.md` for application-level actions
(`create`, `update`, `delete`, `focus`, `wait-result`, `set-style`, and others).

