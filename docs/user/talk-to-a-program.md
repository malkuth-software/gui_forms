# Talk to a running program

A GUI.Forms program launched with `--mcp-shared` or `--mcp-fcfs` (1.0.6 and
later; see Launch) listens on a socket that only your user can reach, and
the socket speaks MCP itself:
newline-delimited JSON-RPC, MCP's stdio framing. Any agent, script or person
can talk to a program the user already has open with standard tools and no
configuration. A first line that is not JSON opens a plain-text console on
the same socket instead. With no client at all, any shell reaches it through
its command folder's named pipes (`/tmp/<id>`; see The command folder).

## Launch

- No flag, no MCP: a program launched without `--mcp-shared` or
  `--mcp-fcfs` (and without `GUI_FORMS_MCP` in its environment) makes no
  command folder, no socket and no discovery entry, prints no MCP lines and
  runs no MCP thread. Nothing a user did not ask for is shared.
- `--mcp-shared` (or `GUI_FORMS_MCP=shared`) is permission-led: the user
  keeps control until a model's first act, which shows a 10-second
  countdown that allows when it ends (see Control).
- `--mcp-fcfs` (or `GUI_FORMS_MCP=fcfs`) is first come, first served: the
  first model to act takes control at once, with no countdown, and keeps
  it; the user gets the program back only by asking (a click on its
  content offers take-back). Both flags given: `--mcp-shared`.
- Either serves every door at once: MCP on standard input and output, the
  command folder and the socket. Each backs up the others: a door that
  cannot be made is named in the start-up text and the rest carry on, and
  the end of standard input ends only the stdio session (a hard disconnect
  for that client: control returns to the user), never the program. A
  model that starts the program in the background from its shell, with
  its input closed or at `/dev/null`, drives it through the folder.
- `--mcp` is a help panel, not a mode: it prints the MCP help (the modes,
  the doors, the send line, chaining, the help paths, release and
  disconnect) and exits, serving nothing. A model that finds a program
  without MCP can run it so, and ask the user: this program isn't shared
  with a model right now; restart it with `--mcp-shared` (you keep
  control) or `--mcp-fcfs`.

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

Open the socket and type: one command a line, each answered, every
answer ending with the prompt `gui-forms> `. There is no banner and no
login; a connection that sends one command and closes is as good as one
kept open.

```text
help                                   the groups, tools, topics and the program's note
help plan_paint                        a group: its tools, one per line
help plan_paint.draw                   one tool: what it does, its arguments, an example
help tools                             every tool, one per line
help mcp.control                       a section of this page (help mcp is all of it)
tools                                  the tools, one per line
call windows                           a tool with no arguments
call find role=button name=OK          key=value arguments (JSON values stay JSON)
call act {"action":"press","target":"auto.ok"}   or one JSON object
transcript                             the program's console record
quit                                   close this connection
```

A reply is the tool's text exactly as an MCP result carries it; an image
is named in a line (fetch it over MCP, or use `capture` with
`points=[[x,y],...]` to read pixels as text).

macOS and Linux:

```sh
nc -U ~/Library/Caches/gui-forms/run/12345.sock
socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR"/gui-forms/12345.sock
```

## The command folder

For an agent with no client at all, whose shell starts afresh for every
command: each program launched with `--mcp-shared` or `--mcp-fcfs` also
makes `/tmp/<id>/`, where `<id>` is 8 hexadecimal digits drawn at start,
and prints what a model needs on standard error (and on standard output,
unless MCP owns it):

```text
GUI.Forms: plan-paint is a GUI.Forms program a model drives with MCP or plain commands; its command folder is /tmp/3fa9c21b
GUI.Forms:   mode: --mcp-fcfs: first come, first served: your first act takes control at once (no countdown) and you keep it until you release it; the user gets it back only by asking
GUI.Forms:   send: echo '<command>' > /tmp/3fa9c21b/in; cat /tmp/3fa9c21b/out
GUI.Forms:   chain: '<cmd1> && <cmd2> ; <cmd3>' (&& stops at an error, ; goes on); one reply, ending ". ok" or ". error at N"
GUI.Forms:   help: send help, then help plan_paint.draw (or run plan-paint --mcp-help [path]); release_control hands control back, disconnect mode=soft|hard ends it
```

A door that could not be made adds a line such as
`GUI.Forms:   not available: the socket: <why>; the other doors carry on`.

- `in` and `out` are named pipes (FIFOs, mode 0600) in a folder of mode
  0700: the folder's permissions are the guard. Each line written to `in`
  is one console command (`help`, `help plan_paint.draw`,
  `plan_paint.draw shape=line from={"x":10,"y":10} to={"x":90,"y":40}`,
  `release`, `disconnect`), run in order with every other source's, under
  the same program-owned control and with no prompts. A tool's name or
  dotted path works as the command itself; `call` is optional here.
- Each reply goes to `out` and ends with a line of its own: `. ok` or
  `. error` (a reply line that begins with `.` gets another). `cat out`
  ends after the reply. A reply waits up to 60 seconds for a reader, so the
  write and the read can be separate shells. An empty or blank line is
  answered too (`error: empty command`, on the socket console as well), so
  `cat out` never waits for nothing.
- One line may chain commands, as a shell does: `a && b` runs `b` only if
  `a` succeeded, `a ; b` always runs it (`&&` and `;` inside quoted values
  or JSON are not separators). The one reply numbers each part (`[1] ...`,
  `[2] ...`; one skipped after a failure says `not run`) and ends with
  `. ok`, or `. error at <n>` naming the first that failed:
  `echo 'plan_paint.new_image width=200 height=100 && plan_paint.draw shape=line from=[0,0] to=[200,100]' > /tmp/<id>/in; cat /tmp/<id>/out`.
  The socket console takes chained lines too.
- A model that starts the program itself, in the background from its
  shell, launches it with `--mcp-fcfs` (or `--mcp-shared`, to leave the
  user asked): the program stays up with its input closed or at
  `/dev/null`, and the start-up text's `mode:` line says which flag
  launched it.
- `#<n> ` before a command (up to 16 letters, digits, `-` or `_`) is echoed
  in the end line, `. #<n> ok`, so concurrent clients can match replies.
  One agent at a time per program is the normal case: lines longer than
  the pipe's atomic size (`PIPE_BUF`, 512 bytes or more) from two writers
  may interleave.
- `help` holds the whole help tree and `info` the program's name, pid,
  executable, socket and control state. `--mcp-socket` and the discovery
  entry (`door: <id>`) name each running program's folder.
- The folder goes when the program ends; one an ended program left behind
  goes when the next listing removes that program's entry.
- On Windows the pipes are `\\.\pipe\<id>\in` and `\\.\pipe\<id>\out`,
  limited to your SID, and `help` and `info` are in `%TEMP%\<id>`:
  `echo <command> > \\.\pipe\<id>\in & type \\.\pipe\<id>\out`.

## MCP

Write MCP's messages, one JSON object a line. `initialize` answers with the
program's instructions: what every program says, the line saying who may
connect and how control works, the help pointer, and the program's own
note.

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

## Connections

- The socket is the only permission: a connection from your own user
  account is served, and nothing asks per connection or per command. The
  same line opens every help, `--help`, `--mcp-socket` and the discovery
  entry: `access: ...`.
- There are no sessions and no tokens. Control, the console record and
  anything pending are the program's, so a command on a fresh connection
  acts exactly as if it came on the previous one. One command per
  connection is normal.
- Several connections may be open at once. Their commands run one at a
  time on the program's UI thread, in the order they arrive: one chained
  sequence.
- When the last connection closes, a question the program raises from then
  on (its save question as the user closes it) is the user's. Control stays
  as it was (see Control).
- The program's visible console shows every connection's calls, labelled
  by the client's name, else `console pid <n>` or `client pid <n>`: MCP
  calls as `call <tool> <args>`, console lines as typed. It is never in a
  capture.
- Only your user reaches the socket: the run directory is private (mode
  0700, or a DACL granting only your SID), the socket is mode 0600, and the
  program checks each peer's user with the kernel; another user's
  connection is closed unread. `GUI_FORMS_MCP_ATTACH=0` in a program's
  environment turns its socket off.

## Control

- Reads never need control; acts do.
- `--mcp-fcfs`: the model's first act takes control at once, with no
  countdown, and the model keeps it until it calls `release_control` or the
  user takes it back; idle never returns it. After a take-back the model's
  next act takes control again, and its reply says the user took it back.
- `--mcp-shared` leaves the user in
  control until the model's first act, which shows a 10-second countdown
  in the program that allows when it ends (the user can refuse). The model
  then holds control, whichever connection later commands come on, until
  it calls `release_control`, 30 seconds pass without a call, or the user
  takes it back. After 30 idle seconds the next act takes control again
  without the countdown.
- In a `--mcp-shared` launch `release_control` gives the program back and
  the next act asks again (the countdown); so does the user's take-back.
- While the model holds control (AI mode) the window shows still grey
  stripes over its control surfaces, the X cursor and the band, and shows
  no tooltips (the model reads their text in snapshot and inspect). The
  user's input is held: a click or key aimed at the window's content
  offers the take-back dialog; activating the window (Alt-Tab, Cmd-Tab, the
  title bar, the Dock) offers nothing.
- A program tool whose reply is complete (a drawing, a command) returns at
  once, never waiting on the window's own timers and frames.

## Disconnect

- `disconnect` acts on the program, not on a connection: it releases
  control and ends the model's use of the program. Mode `soft` (the
  default) closes the program only when nothing is unsaved: otherwise
  nothing closes and the reply names what to save. Mode `hard` ends
  regardless: control returns to the user and the program's own save
  question is shown to the user, or, with `discard_unsaved` true, unsaved
  changes are discarded. The next command, on any connection, starts
  over.
- `disconnect` is an act (owner: telling the program to close is itself an
  action), so it takes control first as any act does in the launch's mode:
  under `--mcp-fcfs` at once; under `--mcp-shared`, when the model does not
  hold control, with the 10-second countdown, and if the user refuses, the
  program stays open and the reply says so. `release_control` is not an
  act.
- `close` quits the program: every program window is asked to close, as
  the user's quit does. It is an act like `disconnect`, so a model may
  simply connect and call `close`. A save question is the model's and never
  shown: without `on_unsaved` nothing closes and the reply names it;
  `on_unsaved=save` or `discard` answers it. A program's own exit command
  (Plan Paint's `command_file.exit`, say) behaves the same. The difference:
  `close` is about the program, `disconnect` about the model's session
  (closing only when nothing is unsaved, or with mode `hard`).
- The end of standard input ends the stdio session only: control returns
  to the user at once and the program stays up, the user's to use and the
  command folder's and socket's to serve. A socket connection closing is
  not a disconnect.

## Help

- Help is a dotted tree. `help` lists the top: the tool groups, the tools
  outside a group, and the topics. `help plan_paint` gives a group's tools,
  one per line; `help plan_paint.draw` (or `help plan_paint_draw`) gives one
  tool's description, its arguments with their types, ranges and units, and
  an example call. `help tools` lists every tool; `help mcp` is this page,
  and `help mcp.launch`, `help mcp.socket`, `help mcp.console`, `help mcp.folder`,
  `help mcp.json`, `help mcp.stdio`, `help mcp.connections`,
  `help mcp.control`, `help mcp.disconnect` and `help mcp.help` are its
  sections (the bare
  section names work too); `help gui.forms` lists the user guide's pages
  with their addresses. A path it does not know names the nearest.
- `program --mcp-help [path]` prints the same help on standard output and
  exits, with no socket and no client; `program --mcp` prints its top.
- Nothing in it is written for MCP alone: the tools' help is their own
  documentation (the description and schema a program gives
  `register_model_tool`, a command's text and description, the built-in
  tools' specs), and the topics are this page, compiled into the library.
  A tool with no description says so.
