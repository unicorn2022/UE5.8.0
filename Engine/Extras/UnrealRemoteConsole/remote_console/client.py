"""
RemoteConsole -- JSON-line protocol client for UE Remote Console Server.

Single persistent TCP connection with bidirectional newline-delimited JSON.
Log output streams from the server interleaved with command responses.
Each request has an "id" field echoed in the response for async matching.
"""

import codecs
import json
import socket
import sys
import threading
import time

DEFAULT_TIMEOUT = 30.0
_JSON_SEPS = (',', ':')

DANGEROUS_COMMANDS = {"exit", "quit"}


class RemoteConsole:
    """JSON-line protocol remote console client.

    Single persistent connection. Client sends JSON requests, server sends
    JSON responses and unsolicited log events. All messages are newline-
    delimited JSON.

    Connection lifecycle:
      1. TCP connect
      2. Client sends:  {"id":N,"op":"hello","version":1}
      3. Server responds: {"id":N,"op":"hello","version":1}
      4. Server begins streaming log events
      5. Bidirectional JSON-line exchange until disconnect

    Client -> Server (after handshake):
      {"id":1,"op":"exec","cmd":"stat unit"}
      {"id":2,"op":"complete","q":"r.Nanite","offset":0,"limit":50}
      {"id":3,"op":"getvar","n":"r.RHI.Name"}

    Server -> Client:
      {"id":1,"op":"exec","ok":true}
      {"id":2,"op":"complete","results":[...],"offset":0,"count":N,"total":M}
      {"id":3,"op":"getvar","name":"...","value":"...","source":"...","help":"..."}
      {"op":"log","cat":"LogTemp","v":"Log","line":"Hello World"}
    """

    def __init__(self, host: str, port: int, timeout: float = DEFAULT_TIMEOUT, verbose: bool = False, log_file: str = None, decoded_log_file: str = None):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.verbose = verbose
        self.label = None          # display label for prompt (e.g., target name)
        self._raw_log = None
        self._decoded_log = None
        try:
            if log_file:
                self._raw_log = open(log_file, "wb")
            if decoded_log_file:
                self._decoded_log = open(decoded_log_file, "w", encoding="utf-8")
        except Exception:
            if self._raw_log:
                self._raw_log.close()
            raise
        self._sock = None
        self._next_id = 1
        self._pending = {}       # id -> threading.Event
        self._responses = {}     # id -> parsed JSON dict
        self._lock = threading.Lock()
        self._reader_thread = None
        self._stop_event = threading.Event()
        self._reconnect_stop = threading.Event()
        self._log_callback = None
        # Accessed from multiple threads; CPython GIL ensures atomic
        # read/write of simple attributes, which suffices here.
        self._connected = False
        self._server_version = 0
        self._auto_reconnect = False
        self._reconnect_thread = None
        self._disconnect_callback = None
        self._reconnect_callback = None
        self._discover_endpoint = None

    def _dbg(self, msg: str):
        """Print a diagnostic message when verbose mode is enabled."""
        if self.verbose:
            print(f"  [debug] {msg}", file=sys.stderr)

    @property
    def connected(self) -> bool:
        return self._connected

    def connect(self, quiet: bool = False) -> bool:
        """Connect to the server. Returns True on success."""
        # Clean up any previous connection before establishing a new one.
        self._cleanup_connection()
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                sock.settimeout(self.timeout)
                sock.connect((self.host, self.port))
            except (socket.error, OSError):
                sock.close()
                raise
            self._sock = sock
            self._connected = True
            self._dbg(f"connect() TCP success, _connected={self._connected}")

            # Start reader thread
            self._stop_event.clear()
            self._reader_thread = threading.Thread(
                target=self._reader_loop, daemon=True, name="RemoteConsole-Reader")
            self._reader_thread.start()

            # Handshake: send {"op":"hello","version":1} and wait for the
            # server's hello response.  This both validates that we're talking
            # to a JSON-line remote console and lets the server defer log
            # subscription until the client is ready.
            try:
                resp = self._send_request("hello", timeout=3.0, version=1)
                if resp.get("error"):
                    raise ConnectionError(resp["error"])
                self._server_version = resp.get("version", 1)
                self._dbg(f"handshake ok, server version={self._server_version}")
            except Exception as e:
                if not quiet:
                    print(f"Handshake failed: {e}", file=sys.stderr)
                self.close()
                return False

            # Even if the handshake round-tripped, the reader may have already
            # died (e.g., server closed the connection right after responding).
            if not self._connected:
                if not quiet:
                    print("Connection lost immediately after handshake.", file=sys.stderr)
                self.close()
                return False

            return True
        except (socket.error, OSError) as e:
            if not quiet:
                print(f"Connection failed: {e}", file=sys.stderr)
            self._sock = None
            self._connected = False
            return False

    def _send_request(self, op: str, timeout: float = None, **kwargs) -> dict:
        """Send a JSON-line request and wait for the response."""
        if not self._connected or self._sock is None:
            self._dbg(f"_send_request rejected: _connected={self._connected}, _sock={'set' if self._sock else 'None'}")
            raise ConnectionError("Not connected")
        with self._lock:
            req_id = self._next_id
            self._next_id += 1
            event = threading.Event()
            self._pending[req_id] = event

        msg = {"id": req_id, "op": op}
        msg.update(kwargs)
        line = json.dumps(msg, separators=_JSON_SEPS) + "\n"

        sock = self._sock
        if sock is None:
            with self._lock:
                self._pending.pop(req_id, None)
            raise ConnectionError("Not connected")
        try:
            sock.sendall(line.encode("utf-8"))
        except (socket.error, OSError) as e:
            with self._lock:
                self._pending.pop(req_id, None)
            raise ConnectionError(f"Send failed: {e}")

        # Wait for response, checking for disconnect periodically so we don't
        # hang for the full timeout if the connection drops after sending.
        effective_timeout = timeout if timeout is not None else self.timeout
        deadline = time.monotonic() + effective_timeout
        while not event.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                with self._lock:
                    self._pending.pop(req_id, None)
                    resp = self._responses.pop(req_id, None)
                if resp is not None:
                    return resp
                raise TimeoutError(f"No response for request {req_id} (op={op})")
            # Wake every 0.5s to check if connection dropped
            event.wait(timeout=min(remaining, 0.5))
            if event.is_set():
                break
            if not self._connected:
                with self._lock:
                    self._pending.pop(req_id, None)
                    resp = self._responses.pop(req_id, None)
                if resp:
                    return resp
                raise ConnectionError("Connection lost while waiting for response")

        with self._lock:
            response = self._responses.pop(req_id, {})
            self._pending.pop(req_id, None)

        return response

    def _reader_loop(self):
        """Background thread: read JSON lines from the server, dispatch responses and log events."""
        sock = self._sock
        if not sock:
            return

        # Capture identity so we can detect if we're a stale reader from a previous connection.
        my_sock = sock
        sock.settimeout(0.5)
        self._dbg(f"reader_loop started, stop_event={self._stop_event.is_set()}, sock={id(sock):#x}")

        # Use an incremental decoder so multi-byte UTF-8 sequences split across
        # recv() boundaries are reassembled correctly instead of producing U+FFFD.
        # Genuine invalid bytes still produce U+FFFD which we strip below.
        utf8_decoder = codecs.getincrementaldecoder("utf-8")("replace")
        buf_parts = []   # accumulate decoded text chunks; join only when splitting lines
        json_loads = json.loads  # local binding avoids global/module lookup per iteration

        while not self._stop_event.is_set():
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            except (socket.error, OSError) as e:
                self._dbg(f"reader recv error: {e}")
                break
            if not data:
                self._dbg("reader recv empty (EOF)")
                break

            if self._raw_log:
                self._raw_log.write(data)
                self._raw_log.flush()

            decoded = utf8_decoder.decode(data)

            # In verbose mode, report any replacement characters from genuine
            # decode errors (not boundary splits -- the incremental decoder
            # handles those). This helps diagnose server-side encoding issues.
            if self.verbose and "\ufffd" in decoded:
                count = decoded.count("\ufffd")
                bad = []
                i = 0
                while i < len(data) and len(bad) < 5:
                    try:
                        data[i:].decode("utf-8")
                        break
                    except UnicodeDecodeError as ude:
                        pos = i + ude.start
                        end = i + ude.end
                        ctx = data[max(0, pos - 3):min(len(data), end + 3)]
                        bad.append(f"@{pos}: [{ctx.hex(' ')}]")
                        i = i + ude.end
                self._dbg(f"UTF-8 errors: {count} bad byte(s) in {len(data)}B chunk: {'; '.join(bad)}")

            buf_parts.append(decoded)

            # Only join and split when we have at least one complete line
            if "\n" not in decoded:
                continue

            text_buf = "".join(buf_parts)
            buf_parts.clear()

            # Process complete lines
            lines = text_buf.split("\n")
            # Last element is the incomplete tail (possibly empty)
            tail = lines[-1]
            if tail:
                buf_parts.append(tail)

            for raw_line in lines[:-1]:
                if not raw_line:
                    continue
                # Strip trailing \r (Windows line endings)
                if raw_line[-1] == '\r':
                    raw_line = raw_line[:-1]
                    if not raw_line:
                        continue

                try:
                    msg = json_loads(raw_line)
                except json.JSONDecodeError:
                    continue

                op = msg.get("op", "")
                msg_id = msg.get("id")

                if op == "log":
                    # Unsolicited log event
                    if self._decoded_log:
                        cat = msg.get("cat", "")
                        text = msg.get("line", "")
                        self._decoded_log.write(f"[{cat}] {text}\n")
                        self._decoded_log.flush()
                    cb = self._log_callback
                    if cb:
                        try:
                            cb(msg)
                        except Exception as _cb_err:
                            self._dbg(f"log callback error: {_cb_err}")
                elif msg_id is not None:
                    # Response to a request -- only store if still pending
                    # (avoids orphaned entries after caller already timed out)
                    with self._lock:
                        event = self._pending.get(msg_id)
                        if event is not None:
                            self._responses[msg_id] = msg
                    if event:
                        event.set()
                else:
                    # Unsolicited non-log message (e.g., parse error from server)
                    cb = self._log_callback
                    if cb:
                        try:
                            cb({"op": "log", "cat": "RemoteConsole",
                                "v": "Error", "line": msg.get("msg", str(msg))})
                        except Exception as _cb_err:
                            self._dbg(f"log callback error: {_cb_err}")

        # Flush any remaining bytes from the decoder (incomplete multi-byte tail).
        utf8_decoder.decode(b"", final=True)

        # Only update state if we're still the active reader (not a stale thread
        # from a previous connection that outlived its join timeout).
        if self._sock is not my_sock:
            self._dbg("stale reader exiting")
            return

        self._connected = False
        self._dbg("reader_loop setting _connected=False")
        # Wake all pending requests so callers see the disconnect instead of
        # hanging for the full timeout.  _cleanup_connection also does this,
        # but the reader exits first, so we wake them here for promptness.
        with self._lock:
            for req_id, event in self._pending.items():
                if req_id not in self._responses:
                    self._responses[req_id] = {"error": "connection lost"}
                event.set()
        self._start_auto_reconnect()

    def enable_auto_reconnect(self, on_disconnect=None, on_reconnect=None):
        """Enable automatic reconnection when the connection drops.
        Optional callbacks are invoked on disconnect/reconnect."""
        self._auto_reconnect = True
        self._disconnect_callback = on_disconnect
        self._reconnect_callback = on_reconnect

    def set_endpoint_discovery(self, callback):
        """Set callback() -> (host, port, label) for endpoint rediscovery on reconnect.
        Used when the server port may change between connections."""
        self._discover_endpoint = callback

    def try_rediscover_endpoint(self) -> bool:
        """Re-discover the endpoint if a discovery callback is set.
        Updates host/port/label on success. Returns True if updated."""
        if not self._discover_endpoint:
            return False
        result = self._discover_endpoint()
        if not result or len(result) < 2 or result[0] is None:
            return False
        self.host, self.port = result[0], result[1]
        if len(result) > 2 and result[2]:
            self.label = result[2]
        return True

    def _start_auto_reconnect(self):
        """Start the auto-reconnect loop if enabled."""
        if not self._auto_reconnect:
            return
        if self._reconnect_thread and self._reconnect_thread.is_alive():
            return
        cb = self._disconnect_callback
        if cb:
            try:
                cb()
            except Exception as _cb_err:
                self._dbg(f"disconnect callback error: {_cb_err}")
        self._reconnect_stop.clear()
        self._reconnect_thread = threading.Thread(
            target=self._reconnect_loop, daemon=True, name="RemoteConsole-Reconnect")
        self._reconnect_thread.start()

    def _reconnect_loop(self):
        """Periodically attempt to reconnect until successful or stopped.
        Uses _reconnect_stop (not _stop_event) so that connect()'s internal
        _cleanup_connection calls don't interfere with the reconnect sleep."""
        while not self._reconnect_stop.is_set():
            self._reconnect_stop.wait(timeout=2.0)
            if self._reconnect_stop.is_set() or not self._auto_reconnect:
                break
            if self._connected:
                break
            # Re-discover endpoint if available (port may change)
            if self._discover_endpoint:
                if not self.try_rediscover_endpoint():
                    continue  # endpoint not available yet, retry later
            if self.connect(quiet=True):
                # Verify the connection is still alive after connect returned.
                # The reader thread may have already detected EOF (e.g., server
                # accepted on OS backlog but isn't actually serving yet).
                if not self._connected:
                    continue
                self._dbg(f"reconnect succeeded, _connected={self._connected}")
                cb = self._reconnect_callback
                if cb:
                    try:
                        cb()
                    except Exception as _cb_err:
                        self._dbg(f"reconnect callback error: {_cb_err}")
                # If connection dropped during/after the callback, keep retrying
                # instead of exiting (fixes death race where auto-reconnect dies
                # silently on immediate post-reconnect disconnect).
                if not self._connected:
                    continue
                break

    def execute(self, command: str) -> dict:
        """Execute a console command. Returns the response dict."""
        return self._send_request("exec", cmd=command)

    def complete(self, prefix: str, offset: int = 0, limit: int = 100) -> tuple:
        """Tab completion. Returns (results, total).
        Results is a list of dicts with keys: name, value, source, help."""
        resp = self._send_request("complete", q=prefix, offset=offset, limit=limit)
        results = resp.get("results", [])
        total = resp.get("total", 0)
        # Normalize results to tuples for backward compat with completer
        tuples = []
        for r in results:
            tuples.append((
                r.get("name", ""),
                r.get("value", ""),
                r.get("source", ""),
                r.get("help", "")
            ))
        return tuples, total

    def getvar(self, name: str) -> dict:
        """Get a CVar's current value. Returns the response dict."""
        return self._send_request("getvar", n=name)

    def _cleanup_connection(self):
        """Tear down socket and reader thread without affecting auto-reconnect state."""
        self._stop_event.set()
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=2.0)
        self._reader_thread = None
        self._connected = False
        with self._lock:
            for req_id, event in self._pending.items():
                self._responses[req_id] = {"error": "connection closed"}
                event.set()
            self._pending.clear()
            self._responses.clear()

    def close(self, permanent=False):
        """Close the connection. If permanent=True, also disables auto-reconnect."""
        if permanent:
            self._auto_reconnect = False
            self._reconnect_stop.set()
            if self._raw_log:
                self._raw_log.close()
                self._raw_log = None
            if self._decoded_log:
                self._decoded_log.close()
                self._decoded_log = None
        self._cleanup_connection()
        if permanent and self._reconnect_thread and self._reconnect_thread.is_alive():
            self._reconnect_thread.join(timeout=3.0)

    def reconnect(self):
        """Manually reconnect. Stops any running auto-reconnect, tears down the
        current connection, attempts one connect, and restarts auto-reconnect
        on failure if it was previously enabled."""
        saved_auto = self._auto_reconnect
        self._auto_reconnect = False
        self._reconnect_stop.set()
        self._cleanup_connection()
        if self._reconnect_thread and self._reconnect_thread.is_alive():
            self._reconnect_thread.join(timeout=3.0)
        self._reconnect_stop.clear()
        self._auto_reconnect = saved_auto
        if not self.connect(quiet=True):
            self._start_auto_reconnect()

    def set_log_callback(self, callback):
        """Set callback(msg: dict) for async log output.
        msg has keys: op, cat, v, line."""
        self._log_callback = callback
