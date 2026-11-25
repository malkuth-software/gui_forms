# Talk to a running program

Every running GUI.Forms program (1.0.5 and later) listens on a socket that
only your user can reach, and the socket speaks MCP itself:
newline-delimited JSON-RPC, MCP's stdio framing. Any agent, script or person
can talk to a program the user already has open with standard tools and no
configuration. A first line that is not JSON opens a plain-text console on
the same socket instead.

## Find the socket

Each program has `<pid>.sock` and a discovery entry `<pid>.info` in your
run directory:

| Host | Run directory |
|---|---|
| macOS | `~/Library/Caches/gui-forms/run` |
| Linux | `$XDG_RUNTIME_DIR/gui-forms`, else `/tmp/gui-forms-<uid>` |
| Windows | `%LOCALAPPDATA%\gui-forms\run` |

```sh
cat ~/Library/Caches/gui-forms/run/*.info        # macOS
cat "$XDG_RUNTIME_DIR"/gui-forms/*.info           # Linux
```

An entry names the program, its MCP identity, pid, executable, socket, the
number of tools it serves and the first line of its note for a model. The
program also prints `GUI.Forms: MCP socket: <path>` on standard error when
it starts, and `program --mcp-socket` prints the socket of each running
instance of that program (or, when none runs, the path a launch would use). `program --help` says all of this in short.
`gui-forms-attach --list` lists them all.

## The console

Open the socket and type. The first line gets the banner back (what every
program says to a model, the session's token line and the help pointer)
and then its own answer, and every reply ends with the prompt
`gui-forms> `.

```text
help                                   the commands, the topics and the program's note
help tools                             the tools, one per line
help plan_paint_draw                   one tool: what it does, its arguments, an example
help sessions                          a topic of this page (help mcp is all of it)
tools                                  the tools, one per line
call windows                           a tool with no arguments
call find role=button name=OK          key=value arguments (JSON values stay JSON)
call act {"action":"press","target":"auto.ok"}   or one JSON object
transcript                             the program's console record
resume <token>                         as the first line: take back a dropped session
quit                                   end the session
```

A reply is the tool's text exactly as an MCP result carries it; an image
is named in a line (fetch it over MCP, or use `capture` with
`points=[[x,y],...]` to read pixels as text).

macOS and Linux:

```sh
nc -U ~/Library/Caches/gui-forms/run/12345.sock
socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR"/gui-forms/12345.sock
```

## MCP

Write MCP's messages, one JSON object a line. `initialize` answers with the
program's instructions: what every program says, then the session's token
line on its own, the help pointer, and the program's own note.

```sh
S=~/Library/Caches/gui-forms/run/12345.sock
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"me","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"windows","arguments":{}}}' \
  | socat -t5 - UNIX-CONNECT:"$S"
```

Python (macOS, Linux):

```python
import json, socket
s = socket.socket(socket.AF_UNIX); s.connect(PATH); f = s.makefile("rw")
def send(m): f.write(json.dumps(m) + "\n"); f.flush()
send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18",
      "capabilities": {}, "clientInfo": {"name": "me", "version": "1"}}}); print(f.readline())
send({"jsonrpc": "2.0", "method": "notifications/initialized"})
send({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "windows", "arguments": {}}})
print(f.readline())
```

Windows, PowerShell 7 (.NET's Unix domain sockets; Windows 10 1803 and
later):

```powershell
$p = Join-Path $env:LOCALAPPDATA 'gui-forms\run\12345.sock'
$s = [Net.Sockets.Socket]::new('Unix', 'Stream', 'Unspecified')
$s.Connect([Net.Sockets.UnixDomainSocketEndPoint]::new($p))
$n = [Net.Sockets.NetworkStream]::new($s); $w = [IO.StreamWriter]::new($n); $r = [IO.StreamReader]::new($n)
$w.WriteLine('help'); $w.Flush(); while (($l = $r.ReadLine()) -ne $null -and -not $l.StartsWith('gui-forms> ')) { $l }
```

Windows' own `curl.exe` and CPython have no use here: curl's
`--unix-socket` speaks HTTP, and CPython on Windows has no `AF_UNIX`.

## A client that wants a stdio server

A client that can only start a stdio MCP server from `.mcp.json` starts the
relay:

```json
{ "mcpServers": { "running-program": {
    "command": "gui-forms-attach", "args": ["--program", "app.vendor.product"] } } }
```

## Sessions

- A program launched with `--mcp` or `--mcp-shared` accepts a new session
  at once; any other program asks its user in the program, every session,
  naming the client by what the client says (that name is not verified).
- One question per session. A program launched without a flag asks once,
  when the client connects, and that Allow is also the consent to control:
  the first act takes control with no countdown. Under `--mcp-shared` the
  first act's countdown is the one question. A new connection without the
  token asks again; nothing else is remembered.
- One client at a time. A new client ends a session waiting for a resume;
  while a session is connected, another connection waits.
- The session's token line, near the top of the instructions, the console's
  banner and help, and a disconnect's reply, reads: `session token: <t>.
  Reconnect with resume <t> (MCP: params.resume) within 120 s; the person
  at this computer may not be there to answer another prompt. To run a
  program yourself, launch it with --mcp: it won't ask.`
- A session whose connection drops gives control back to the user at once:
  the stripes, the band and the thinking button go, and a question the
  program asks from then on (its save question as the user closes it) is
  the user's. The session waits 120 seconds: initialize again with
  `"resume": "<token>"` in its params (or type `resume <token>` as a
  console's first line) and it carries on with no new question. `quit`, or
  the 120 seconds passing, ends it.
- The program's visible console shows every session's calls, labelled by
  client and token: MCP calls as `call <tool> <args>`, console lines as
  typed. It is never in a capture.
- Only your user reaches the socket: the run directory is private (mode
  0700, or a DACL granting only your SID), the socket is mode 0600, and the
  program checks each peer's user with the kernel. `GUI_FORMS_MCP_ATTACH=0`
  in a program's environment turns its socket off.

## Control

- Reads never need control; acts do.
- `--mcp` is AI-led: the model holds control from the start and keeps it
  until it calls `release_control` or the user takes it back. An agent that
  launches a program itself launches it so: it never asks.
- `--mcp-shared` leaves the user in control until the model's first act,
  which asks in the program, with a 10-second countdown that allows when it
  ends. Control returns after 30 seconds without a call; within the
  session the next act takes it again without asking.
- `release_control` gives the program back; the next act asks again (the
  countdown). So does the user's take-back.
- While the model holds control (AI mode) the window shows still grey
  stripes over its control surfaces, the X cursor and the band, and shows
  no tooltips (the model reads their text in snapshot and inspect). The
  user's input is held: a click or key aimed at the window's content
  offers the take-back dialog; activating the window (Alt-Tab, Cmd-Tab, the
  title bar, the Dock) offers nothing.
- A program tool whose reply is complete (a drawing, a command) returns at
  once, never waiting on the window's own timers and frames.

## Disconnect

- `disconnect` with mode `soft` (the default) closes the program only when
  nothing is unsaved: otherwise nothing closes and the reply names what to
  save. Mode `hard` ends the session regardless: control returns to the
  user and the program's own save question is shown to the user, or, with
  `discard_unsaved` true, unsaved changes are discarded. If the user holds
  control, the program stays open and only the session ends.
- A client that stops, crashes or is killed, and a launch whose standard
  input ends, is a hard disconnect with the default: control returns to
  the user at once, and the program stays the user's to use and to close.

## Help

- `help` gives the commands, the topics and the program's own note;
  `help tools` lists the tools; `help <tool>` gives a tool's description,
  its arguments with their types, ranges and units, and an example call;
  `help mcp` is this page, and `help socket`, `help console`, `help json`,
  `help stdio`, `help sessions`, `help control`, `help disconnect` and
  `help help` are its sections; `help gui.forms` lists the user guide's
  pages with their addresses. A topic it does not know names the nearest.
- Nothing in it is written for MCP alone: the tools' help is their own
  documentation (the description and schema a program gives
  `register_model_tool`, a command's text and description, the built-in
  tools' specs), and the topics are this page, compiled into the library.
  A tool with no description says so.
