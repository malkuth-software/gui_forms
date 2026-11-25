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

Open the socket and type. The first command gets the help back (what the
program says to a model, its own note, the commands and the session's
token), and every reply ends with the prompt `gui-forms> `.

```text
help                                   the help again
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
program's instructions, which end with the session's token.

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
- One client at a time. A new client ends a session waiting for a resume;
  while a session is connected, another connection waits.
- A session whose connection drops waits 120 seconds: initialize again with
  `"resume": "<token>"` in its params (or type `resume <token>` as a
  console's first line) and it carries on with no new question. `quit`, or
  the 120 seconds passing, ends it, and control returns to the user.
- The program's visible console shows every session's calls, labelled by
  client and token: MCP calls as `call <tool> <args>`, console lines as
  typed. It is never in a capture.
- Only your user reaches the socket: the run directory is private (mode
  0700, or a DACL granting only your SID), the socket is mode 0600, and the
  program checks each peer's user with the kernel. `GUI_FORMS_MCP_ATTACH=0`
  in a program's environment turns its socket off.
