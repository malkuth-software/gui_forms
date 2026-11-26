"""A small MCP client over a program's standard input and output.

GUI.Forms programs serve MCP when started with --mcp-shared or --mcp-fcfs (the library's
automation; notes:planning/studies/MCP_AUTOMATION_STUDY.md). This client is
what the development tools use to drive one: it starts the program, says
initialize (the legacy handshake, which the dual-era server still answers)
and calls tools, each call a JSON-RPC request on one line. Replies are read
on a thread, so a caller may send a call and not wait for it (a fuzzer
stressing the program from several directions does).

    client = McpClient([program, '--mcp-fcfs'])
    ok, text, images = client.call('snapshot', {})
    client.close()        # a hard disconnect, then standard input closed
"""

import base64
import json
import queue
import subprocess
import threading

PROTOCOL_VERSION = '2025-06-18'


class McpClient:
    """One program served over MCP, and the calls made to it."""

    def __init__(self, command, environment=None, client_name='gui-forms-tools'):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, env=environment)
        self.next_id = 1
        self.lock = threading.Lock()
        self.replies = {}
        self.arrived = threading.Condition(self.lock)
        threading.Thread(target=self._read, daemon=True).start()
        self.request('initialize', {'protocolVersion': PROTOCOL_VERSION, 'capabilities': {},
                                    'clientInfo': {'name': client_name, 'version': '1'}}, timeout=30.0)
        self._send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})

    def _read(self):
        for raw in iter(self.process.stdout.readline, b''):
            try:
                message = json.loads(raw.decode('utf-8'))
            except ValueError:
                continue
            if 'id' not in message:
                continue
            with self.lock:
                self.replies[message['id']] = message
                self.arrived.notify_all()

    def _send(self, message):
        line = (json.dumps(message) + '\n').encode('utf-8')
        try:
            self.process.stdin.write(line)
            self.process.stdin.flush()
            return True
        except (BrokenPipeError, OSError):
            return False

    def send_request(self, method, params):
        """Sends a request; returns its id, or None when the program is gone."""
        with self.lock:
            identity = self.next_id
            self.next_id += 1
        sent = self._send({'jsonrpc': '2.0', 'id': identity, 'method': method, 'params': params})
        return identity if sent else None

    def wait_reply(self, identity, timeout):
        if identity is None:
            return None
        with self.lock:
            self.arrived.wait_for(lambda: identity in self.replies, timeout=timeout)
            return self.replies.pop(identity, None)

    def request(self, method, params, timeout=30.0):
        return self.wait_reply(self.send_request(method, params), timeout)

    def call(self, tool, arguments, timeout=30.0):
        """Calls a tool and waits: (ok, text, images as PNG bytes)."""
        reply = self.request('tools/call', {'name': tool, 'arguments': arguments}, timeout)
        return self.result_of(reply)

    def call_later(self, tool, arguments):
        """Sends a call without waiting for it; True when it was sent."""
        return self.send_request('tools/call', {'name': tool, 'arguments': arguments}) is not None

    @staticmethod
    def result_of(reply):
        if reply is None or 'result' not in reply:
            return False, '' if reply is None else json.dumps(reply.get('error')), []
        result = reply['result']
        texts = [item.get('text', '') for item in result.get('content', []) if item.get('type') == 'text']
        images = [base64.b64decode(item['data']) for item in result.get('content', [])
                  if item.get('type') == 'image']
        return not result.get('isError', False), '\n'.join(texts), images

    def alive(self):
        return self.process.poll() is None

    def close(self, timeout=10.0):
        """Ends the program and waits for it: a hard disconnect, then its input
        closed (the end of the input alone ends only the stdio session, never
        the program: MCP 1.0.6)."""
        if self.alive():
            self.call('disconnect', {'mode': 'hard', 'discard_unsaved': True}, timeout=timeout)
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
