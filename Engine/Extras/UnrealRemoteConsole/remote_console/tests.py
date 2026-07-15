"""Self-tests for the UE Remote Console client."""

import codecs
import json
import re
import socket
import sys
import threading
import time

from .client import RemoteConsole, DEFAULT_TIMEOUT, DANGEROUS_COMMANDS
from .formatting import (
    _format_log_msg, _highlight_log_text, _disable_colors,
    _NUM_COLOR, _UNIT_COLOR, _STR_COLOR, _QUOTE_COLOR, _ARROW_COLOR,
    _BRACKET_COLOR, _KEY_COLOR, _RESET,
)


def _run_self_tests():
    """Run internal tests to verify UTF-8 decoding and protocol handling."""
    passed = 0
    failed = 0

    def _check(name, condition, detail=""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  PASS  {name}")
        else:
            failed += 1
            print(f"  FAIL  {name}  {detail}")

    def _make_log_line(text, ascii_only=False):
        obj = {"op": "log", "cat": "Test", "line": text}
        return (json.dumps(obj, ensure_ascii=ascii_only) + "\n").encode("utf-8")

    def _decode_chunks(chunks):
        """Simulate the reader's incremental decode pipeline."""
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        text_buf = ""
        lines = []
        for chunk in chunks:
            text_buf += decoder.decode(chunk)
            while "\n" in text_buf:
                line, text_buf = text_buf.split("\n", 1)
                line = line.rstrip("\r")
                if line:
                    lines.append(line)
        remaining = decoder.decode(b"", final=True)
        if remaining:
            text_buf += remaining
        if text_buf.strip():
            lines.append(text_buf.strip())
        return lines

    def _extract(json_line):
        return json.loads(json_line).get("line", "")

    print("Running self-tests...")
    print()

    # --- UTF-8 boundary split tests ---
    print("UTF-8 incremental decoder:")

    # Baseline: no split
    raw = _make_log_line("abc \u2502 def \u2503 ghi")
    lines = _decode_chunks([raw])
    _check("no split", len(lines) == 1 and _extract(lines[0]) == "abc \u2502 def \u2503 ghi")

    # Split before multi-byte char
    raw = _make_log_line("A\u2503B")
    idx = raw.index(b"\xe2\x94\x83")
    lines = _decode_chunks([raw[:idx], raw[idx:]])
    _check("split before 3-byte char", len(lines) == 1 and _extract(lines[0]) == "A\u2503B")

    # Split after 1st byte of 3-byte sequence (E2 | 94 83)
    lines = _decode_chunks([raw[:idx + 1], raw[idx + 1:]])
    _check("split after byte 1 of 3", len(lines) == 1 and _extract(lines[0]) == "A\u2503B")

    # Split after 2nd byte of 3-byte sequence (E2 94 | 83)
    lines = _decode_chunks([raw[:idx + 2], raw[idx + 2:]])
    _check("split after byte 2 of 3", len(lines) == 1 and _extract(lines[0]) == "A\u2503B")

    # Each byte of 3-byte char in separate chunk
    lines = _decode_chunks([raw[:idx + 1], raw[idx + 1:idx + 2], raw[idx + 2:]])
    _check("3-byte char across 3 chunks", len(lines) == 1 and _extract(lines[0]) == "A\u2503B")

    # 4-byte emoji split at each position
    emoji = "\U0001F600"
    raw = _make_log_line(f"x{emoji}y")
    emoji_bytes = emoji.encode("utf-8")  # F0 9F 98 80
    idx = raw.index(emoji_bytes)
    all_ok = True
    for sp in range(1, 4):
        lines = _decode_chunks([raw[:idx + sp], raw[idx + sp:]])
        if not (len(lines) == 1 and _extract(lines[0]) == f"x{emoji}y"):
            all_ok = False
            break
    _check("4-byte emoji split at each position", all_ok)

    # Byte-at-a-time delivery
    raw = _make_log_line("\u2502\u2503\u2502\u2503\u2502")
    chunks = [raw[i:i + 1] for i in range(len(raw))]
    lines = _decode_chunks(chunks)
    _check("byte-at-a-time delivery",
           len(lines) == 1 and _extract(lines[0]) == "\u2502\u2503\u2502\u2503\u2502")

    # Multiple lines split mid-char
    line1 = _make_log_line("line1")
    line2 = _make_log_line("A\u2503B")
    raw = line1 + line2
    idx = raw.index(b"\xe2\x94\x83")
    lines = _decode_chunks([raw[:idx + 1], raw[idx + 1:]])
    _check("two lines, split mid-char in second",
           len(lines) == 2 and _extract(lines[1]) == "A\u2503B")

    # ASCII-escaped JSON: immune to any split
    print()
    print("ASCII-escaped JSON (server-side \\\\uXXXX):")
    raw = _make_log_line("\u2502 data \u2503 more \u2502", ascii_only=True)
    all_ok = True
    for i in range(1, len(raw)):
        lines = _decode_chunks([raw[:i], raw[i:]])
        if not (len(lines) == 1 and _extract(lines[0]) == "\u2502 data \u2503 more \u2502"):
            all_ok = False
            break
    _check("split at every byte position (pure ASCII)", all_ok)

    # --- Mock server round-trip ---
    print()
    print("Mock server round-trip:")

    text = "0.4% \u250a          \u2502 0.000 ms \u2503"
    raw = _make_log_line(text)
    # Split inside the first box char
    idx = raw.index(b"\xe2")
    chunk1, chunk2 = raw[:idx + 1], raw[idx + 1:]

    received = []
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]

    def _mock_handler():
        server.settimeout(5.0)
        conn, _ = server.accept()
        try:
            conn.settimeout(2.0)
            # Read the handshake hello message
            hello_buf = b""
            while b"\n" not in hello_buf:
                hello_buf += conn.recv(4096)
            hello_msg = json.loads(hello_buf.decode("utf-8"))
            resp = json.dumps({"id": hello_msg["id"], "op": "hello", "version": 1}) + "\n"
            conn.sendall(resp.encode("utf-8"))
            conn.sendall(chunk1)
            time.sleep(0.05)
            conn.sendall(chunk2)
            time.sleep(0.5)
        finally:
            conn.close()
            server.close()

    server_thread = threading.Thread(target=_mock_handler, daemon=True)
    server_thread.start()

    try:
        console = RemoteConsole("127.0.0.1", port, timeout=5.0)
        console.set_log_callback(lambda msg: received.append(msg.get("line", "")))
        connected = console.connect()
        if connected:
            deadline = time.monotonic() + 3.0
            while not received and time.monotonic() < deadline:
                time.sleep(0.05)
            console.close(permanent=True)

        _check("connect to mock server", connected)
        _check("received log line", len(received) == 1,
               f"got {len(received)} lines" if received else "no lines received")
        if received:
            _check("text matches exactly", received[0] == text,
                   f"got: {repr(received[0])}")
            _check("no replacement chars", "\ufffd" not in received[0])
    except Exception as e:
        _check("mock server test", False, str(e))

    server_thread.join(timeout=3.0)

    # --- Syntax highlighting tests ---
    print()
    print("Syntax highlighting:")

    def _strip_ansi(s):
        """Remove ANSI escape sequences to get plain text."""
        return re.sub(r'\033\[[0-9;]*m', '', s)

    def _has_ansi(s, substr):
        """Check that substr in s is wrapped in ANSI codes (i.e., highlighted)."""
        stripped = _strip_ansi(s)
        idx = stripped.find(substr)
        if idx < 0:
            return False
        # Walk the original string to find where substr starts
        plain_pos = 0
        orig_pos = 0
        while plain_pos < idx and orig_pos < len(s):
            if s[orig_pos] == '\033':
                while orig_pos < len(s) and s[orig_pos] != 'm':
                    orig_pos += 1
                orig_pos += 1  # skip 'm'
            else:
                plain_pos += 1
                orig_pos += 1
        # Check if ANSI codes exist right at the start of substr
        pos_before = orig_pos
        while orig_pos < len(s) and s[orig_pos] == '\033':
            while orig_pos < len(s) and s[orig_pos] != 'm':
                orig_pos += 1
            orig_pos += 1
        return orig_pos > pos_before

    def _format_test(msg_dict):
        """Run _format_log_msg and return (formatted, stripped)."""
        fmt = _format_log_msg(msg_dict)
        return fmt, _strip_ansi(fmt)

    # Test: category gets colored
    fmt, plain = _format_test({"cat": "LogEngine", "v": "Log", "line": "Hello"})
    _check("category [LogEngine] colored", _has_ansi(fmt, "[LogEngine]"))
    _check("category preserved in plain text", "[LogEngine] Hello" == plain)

    # Test: deterministic coloring (CRC32-based)
    fmt1, _ = _format_test({"cat": "LogRHI", "v": "Log", "line": "A"})
    fmt2, _ = _format_test({"cat": "LogRHI", "v": "Log", "line": "B"})
    # Extract the color code for [LogRHI] from both
    m1 = re.search(r'(\033\[[0-9;]*m)\[LogRHI\]', fmt1)
    m2 = re.search(r'(\033\[[0-9;]*m)\[LogRHI\]', fmt2)
    _check("category color is deterministic",
           m1 and m2 and m1.group(1) == m2.group(1),
           f"got {m1.group(1) if m1 else 'None'} vs {m2.group(1) if m2 else 'None'}")

    # Test: brackets [[ and ]] highlighted
    fmt, plain = _format_test({"cat": "LogConfig", "v": "Log",
                                "line": "CVar [[r.HZB.BuildUseCompute:1 -> 0]]"})
    _check("double brackets [[ highlighted", _has_ansi(fmt, "[["))
    _check("double brackets ]] highlighted", _has_ansi(fmt, "]]"))
    _check("no text eaten by brackets",
           plain == "[LogConfig] CVar [[r.HZB.BuildUseCompute:1 -> 0]]")

    # Test: key:value colored
    fmt, plain = _format_test({"cat": "Log", "v": "Log",
                                "line": "[[rhi.syncinterval:1 -> 0]]"})
    _check("key 'rhi.syncinterval' colored", _has_ansi(fmt, "rhi.syncinterval"))
    _check("separator ':' colored", _has_ansi(fmt, ":"))
    _check("number '1' colored", _has_ansi(fmt, "1"))
    _check("arrow '->' colored", _has_ansi(fmt, "->"))
    _check("no text eaten by key:value",
           plain == "[Log] [[rhi.syncinterval:1 -> 0]]")

    # Test: key=value colored
    fmt, plain = _format_test({"cat": "Log", "v": "Log", "line": "Width=1920"})
    _check("key 'Width' colored in key=value", _has_ansi(fmt, "Width"))
    _check("number '1920' colored in key=value", _has_ansi(fmt, "1920"))

    # Test: quoted strings colored
    fmt, plain = _format_test({"cat": "Log", "v": "Log",
                                "line": 'r.Nanite = "1"'})
    _check("quoted string '\"1\"' colored", _has_ansi(fmt, '"1"'))
    _check("no text eaten by string highlight", plain == '[Log] r.Nanite = "1"')

    # Test: numbers NOT highlighted inside identifiers
    # Use _highlight_log_text directly to avoid category prefix interference
    hl = _highlight_log_text("SpotLight77 Phase 2")
    hl_plain = _strip_ansi(hl)
    _check("number in identifier not highlighted",
           not _has_ansi(hl, "SpotLight77"))
    # Verify the raw highlighted output contains \033[36m2\033[0m (cyan number)
    _check("standalone number highlighted", f"{_NUM_COLOR}2{_RESET}" in hl)
    _check("identifier+number text preserved", hl_plain == "SpotLight77 Phase 2")

    # Test: parentheses highlighted
    fmt, _ = _format_test({"cat": "Log", "v": "Log", "line": "(155K)"})
    _check("opening paren highlighted", _has_ansi(fmt, "("))
    _check("closing paren highlighted", _has_ansi(fmt, ")"))

    # Test: verbosity label shown for errors
    fmt, plain = _format_test({"cat": "LogMat", "v": "Error",
                                "line": "Shader not found"})
    _check("error verbosity label shown", "Error:" in plain)
    _check("error text preserved", "Shader not found" in plain)

    # Test: verbosity label NOT shown for normal logs
    fmt, plain = _format_test({"cat": "LogTemp", "v": "Log", "line": "Hello"})
    _check("no verbosity label for Log level", "Log:" not in plain)

    # Test: leading [Tag] in line text colored
    fmt, plain = _format_test({"cat": "LogEngine", "v": "Log",
                                "line": "[SubSystem] Initialized"})
    _check("leading [SubSystem] tag colored", _has_ansi(fmt, "[SubSystem]"))
    _check("leading tag text preserved", "[SubSystem] Initialized" in plain)

    # Test: angle brackets highlighted
    fmt, _ = _format_test({"cat": "Log", "v": "Log",
                            "line": "<Console variable> = <value>"})
    _check("angle bracket < highlighted", _has_ansi(fmt, "<"))
    _check("angle bracket > highlighted", _has_ansi(fmt, ">"))

    # Test: standalone = highlighted (not part of key=value)
    _check("standalone = highlighted", _has_ansi(fmt, "="))

    # Test: number with unit suffix -- split coloring
    hl = _highlight_log_text("size 512KB rate 2.4MB/s time 3.5ms freq 60Hz pct 99.1%")
    hl_plain = _strip_ansi(hl)
    _check("unit text preserved",
           hl_plain == "size 512KB rate 2.4MB/s time 3.5ms freq 60Hz pct 99.1%")
    # Number portion in bright cyan, suffix in dim cyan
    _check("512 number colored", f"{_NUM_COLOR}512{_RESET}" in hl)
    _check("KB suffix colored differently", f"{_UNIT_COLOR}KB{_RESET}" in hl)
    _check("2.4 number colored", f"{_NUM_COLOR}2.4{_RESET}" in hl)
    _check("MB/s suffix colored", f"{_UNIT_COLOR}MB/s{_RESET}" in hl)
    _check("3.5 number colored", f"{_NUM_COLOR}3.5{_RESET}" in hl)
    _check("ms suffix colored", f"{_UNIT_COLOR}ms{_RESET}" in hl)
    _check("60 number colored", f"{_NUM_COLOR}60{_RESET}" in hl)
    _check("Hz suffix colored", f"{_UNIT_COLOR}Hz{_RESET}" in hl)
    _check("99.1 number colored", f"{_NUM_COLOR}99.1{_RESET}" in hl)
    _check("% suffix colored", f"{_UNIT_COLOR}%{_RESET}" in hl)

    # Test: detached unit suffix (space between number and unit)
    hl = _highlight_log_text("freed 2.4 MB, peak 1 GiB")
    _check("2.4 with space+MB colored", f"{_NUM_COLOR}2.4{_RESET}" in hl)
    _check("space+MB suffix colored", f"{_UNIT_COLOR} MB{_RESET}" in hl)
    _check("1 with space+GiB colored", f"{_NUM_COLOR}1{_RESET}" in hl)
    _check("space+GiB suffix colored", f"{_UNIT_COLOR} GiB{_RESET}" in hl)

    # Test: single-quoted string with dim quotes, bright content
    hl = _highlight_log_text("loaded 'foo.pak'")
    _check("single quote mark dim", f"{_QUOTE_COLOR}'{_RESET}" in hl)
    _check("single quote content bright", f"{_STR_COLOR}foo.pak{_RESET}" in hl)

    # Test: double-quoted string with dim quotes, bright content
    hl = _highlight_log_text('value "hello"')
    _check("double quote mark dim", f'{_QUOTE_COLOR}"{_RESET}' in hl)
    _check("double quote content bright", f"{_STR_COLOR}hello{_RESET}" in hl)

    # Test: warning verbosity
    fmt, plain = _format_test({"cat": "Log", "v": "Warning", "line": "Low memory"})
    _check("warning verbosity label shown", "Warning:" in plain)

    # Test: fatal verbosity
    fmt, plain = _format_test({"cat": "Log", "v": "Fatal", "line": "Crash"})
    _check("fatal verbosity label shown", "Fatal:" in plain)

    # Test: time units ns, us, s and px (pixels)
    hl = _highlight_log_text("latency 50ns avg 1.2us total 1.5s offset 320px")
    _check("ns suffix colored", f"{_UNIT_COLOR}ns{_RESET}" in hl)
    _check("us suffix colored", f"{_UNIT_COLOR}us{_RESET}" in hl)
    _check("s suffix colored", f"{_UNIT_COLOR}s{_RESET}" in hl)
    _check("px suffix colored", f"{_UNIT_COLOR}px{_RESET}" in hl)

    # Test: arrow -> highlighted
    hl = _highlight_log_text("value 1 -> 0")
    _check("arrow -> highlighted", f"{_ARROW_COLOR}->{_RESET}" in hl)

    # Test: numbers inside quoted strings are highlighted
    hl = _highlight_log_text('systemresolution.resx="1920"')
    _check("number inside double-quoted string colored",
           f"{_NUM_COLOR}1920{_RESET}" in hl)
    _check("quote marks still dim around number",
           f"{_QUOTE_COLOR}\"{_RESET}" in hl)
    hl = _highlight_log_text("size='512KB'")
    _check("number inside single-quoted string colored",
           f"{_NUM_COLOR}512{_RESET}" in hl)
    _check("unit suffix inside quoted string colored",
           f"{_UNIT_COLOR}KB{_RESET}" in hl)

    # Test: dimension patterns (NUMxNUM)
    hl = _highlight_log_text("Resolution 1920x1080 Volume 256x256x4")
    hl_plain = _strip_ansi(hl)
    _check("dimension text preserved",
           hl_plain == "Resolution 1920x1080 Volume 256x256x4")
    _check("dimension number colored", f"{_NUM_COLOR}1920{_RESET}" in hl)
    _check("dimension x uses arrow color", f"{_ARROW_COLOR}x{_RESET}" in hl)
    _check("dimension second number colored", f"{_NUM_COLOR}1080{_RESET}" in hl)

    # Test: dimensions inside quoted strings
    hl = _highlight_log_text('Foo="123x345"')
    hl_plain = _strip_ansi(hl)
    _check("dimension inside string text preserved",
           hl_plain == 'Foo="123x345"')
    _check("dimension inside string number colored",
           f"{_NUM_COLOR}123{_RESET}" in hl)
    _check("dimension inside string x colored",
           f"{_ARROW_COLOR}x{_RESET}" in hl)
    _check("dimension inside string second number colored",
           f"{_NUM_COLOR}345{_RESET}" in hl)

    # Test: :: is NOT treated as key:value (C++ scope resolution)
    hl = _highlight_log_text("Substrate::MaterialClassification")
    hl_plain = _strip_ansi(hl)
    _check(":: not treated as key:value",
           not _has_ansi(hl, "Substrate"))
    _check(":: text preserved",
           hl_plain == "Substrate::MaterialClassification")

    # Test: key: value with space after separator IS treated as key-value
    hl = _highlight_log_text("Textures: 0, Buffers: 1")
    _check("'Textures' colored as key with space",
           _has_ansi(hl, "Textures"))
    _check("'Buffers' colored as key with space",
           _has_ansi(hl, "Buffers"))

    # Test: key = "value" with spaces around = (CVar assignment style)
    hl = _highlight_log_text('r.DynamicRes.TestScreenPercentage = "40"')
    hl_plain = _strip_ansi(hl)
    _check("spaced = key colored",
           _has_ansi(hl, "r.DynamicRes.TestScreenPercentage"))
    _check("spaced = separator colored",
           _has_ansi(hl, " = "))
    _check("spaced = text preserved",
           hl_plain == 'r.DynamicRes.TestScreenPercentage = "40"')

    # Test: key= "value" (no space before =, space after)
    hl = _highlight_log_text('r.Foo= "40"')
    _check("key= value key colored", _has_ansi(hl, "r.Foo"))
    _check("key= value text preserved", _strip_ansi(hl) == 'r.Foo= "40"')

    # Test: key ="value" (space before =, no space after)
    hl = _highlight_log_text("r.Foo ='40'")
    _check("key =value key colored", _has_ansi(hl, "r.Foo"))

    # --- Commands and interactive module tests ---
    # These catch import/wiring errors that would only surface at runtime.
    print()
    print("Commands and interactive:")

    from .commands import (
        _handle_internal_command, INTERNAL_COMMANDS_COMMON,
        INTERNAL_COMMANDS_BASIC_ONLY, INTERNAL_COMMANDS,
        INTERNAL_HELP_TEXT_COMMON, INTERNAL_HELP_TEXT_BASIC,
    )

    # Verify command dictionaries are consistent
    _check("INTERNAL_COMMANDS is superset of COMMON",
           all(k in INTERNAL_COMMANDS for k in INTERNAL_COMMANDS_COMMON))
    _check("INTERNAL_COMMANDS is superset of BASIC_ONLY",
           all(k in INTERNAL_COMMANDS for k in INTERNAL_COMMANDS_BASIC_ONLY))
    _check("INTERNAL_COMMANDS size matches",
           len(INTERNAL_COMMANDS) == len(INTERNAL_COMMANDS_COMMON) + len(INTERNAL_COMMANDS_BASIC_ONLY))
    _check("all commands start with /",
           all(k.startswith("/") for k in INTERNAL_COMMANDS))
    _check("help text mentions /help",
           "/help" in INTERNAL_HELP_TEXT_COMMON and "/help" in INTERNAL_HELP_TEXT_BASIC)

    # Mock console for command handler tests
    class _MockConsole:
        def __init__(self):
            self.connected = True
            self.executed = []
            self.completed = []
            self.reconnected = False
        def execute(self, cmd):
            self.executed.append(cmd)
            return {"ok": True}
        def complete(self, prefix, offset=0, limit=100):
            self.completed.append(prefix)
            return [("r.Foo", "1", "Console", "A foo")], 1
        def reconnect(self):
            self.reconnected = True
        def getvar(self, name):
            return {"name": name, "value": "42", "source": "Console", "help": "test"}

    mock = _MockConsole()
    log_output = []

    # /quit returns False (exit loop)
    _check("/quit returns False",
           _handle_internal_command("/quit", mock, log_fn=log_output.append) == False)
    _check("/exit returns False",
           _handle_internal_command("/exit", mock, log_fn=log_output.append) == False)
    _check("/q returns False",
           _handle_internal_command("/q", mock, log_fn=log_output.append) == False)

    # /help returns True (continue loop) and produces output
    log_output.clear()
    _check("/help returns True",
           _handle_internal_command("/help", mock, log_fn=log_output.append) == True)
    _check("/help produces output", len(log_output) > 0)

    log_output.clear()
    _check("/h returns True",
           _handle_internal_command("/h", mock, log_fn=log_output.append) == True)

    log_output.clear()
    _check("/? returns True",
           _handle_internal_command("/?", mock, log_fn=log_output.append) == True)

    # /reconnect triggers reconnect
    mock.reconnected = False
    _handle_internal_command("/reconnect", mock, log_fn=log_output.append)
    _check("/reconnect calls reconnect()", mock.reconnected)

    # /complete with prefix
    log_output.clear()
    _handle_internal_command("/complete r.Foo", mock, log_fn=log_output.append)
    _check("/complete calls complete()", "r.Foo" in mock.completed)
    _check("/complete produces output", len(log_output) > 0)

    # /complete without args shows usage
    log_output.clear()
    _handle_internal_command("/complete", mock, log_fn=log_output.append)
    _check("/complete no-args shows usage",
           any("Usage" in str(l) for l in log_output))

    # /get with name
    log_output.clear()
    _handle_internal_command("/get r.Foo", mock, log_fn=log_output.append)
    _check("/get produces output", len(log_output) > 0)

    # /get without args shows usage
    log_output.clear()
    _handle_internal_command("/get", mock, log_fn=log_output.append)
    _check("/get no-args shows usage",
           any("Usage" in str(l) for l in log_output))

    # Unknown command
    log_output.clear()
    _handle_internal_command("/nonexistent", mock, log_fn=log_output.append)
    _check("/unknown shows error",
           any("Unknown" in str(l) for l in log_output))

    # Case insensitivity
    _check("/QUIT returns False",
           _handle_internal_command("/QUIT", mock, log_fn=log_output.append) == False)
    _check("/Help returns True",
           _handle_internal_command("/Help", mock, log_fn=log_output.append) == True)

    # --- Interactive module import validation ---
    # These verify that all cross-module references resolve correctly,
    # catching the exact class of bug (missing imports) that broke slash
    # command completion.
    print()
    print("Interactive module imports:")

    from . import interactive as _interactive_mod

    _check("interactive imports INTERNAL_COMMANDS_COMMON",
           hasattr(_interactive_mod, 'INTERNAL_COMMANDS_COMMON'))
    _check("interactive imports DANGEROUS_COMMANDS",
           hasattr(_interactive_mod, 'DANGEROUS_COMMANDS'))
    _check("interactive imports _handle_internal_command",
           hasattr(_interactive_mod, '_handle_internal_command'))
    _check("interactive imports _format_log_msg",
           hasattr(_interactive_mod, '_format_log_msg'))
    _check("interactive imports socket",
           hasattr(_interactive_mod, 'socket'))
    _check("interactive_loop is callable",
           callable(getattr(_interactive_mod, 'interactive_loop', None)))
    _check("pipe_mode is callable",
           callable(getattr(_interactive_mod, 'pipe_mode', None)))

    # If prompt_toolkit is available, verify the completer classes exist
    if getattr(_interactive_mod, 'HAS_PROMPT_TOOLKIT', False):
        _check("UECompleter class exists",
               hasattr(_interactive_mod, 'UECompleter'))
        _check("UEAutoSuggest class exists",
               hasattr(_interactive_mod, 'UEAutoSuggest'))

        # Exercise UECompleter with a mock console
        Completer_cls = getattr(_interactive_mod, 'UECompleter')
        completer = Completer_cls(mock)

        # Simulate a Document for completion
        class _MockDoc:
            def __init__(self, text):
                self.text_before_cursor = text

        # Slash command completion
        slash_results = list(completer.get_completions(_MockDoc("/he"), None))
        _check("completer returns /help for '/he'",
               any("/help" in c.text for c in slash_results))

        slash_results = list(completer.get_completions(_MockDoc("/q"), None))
        _check("completer returns /quit for '/q'",
               any("/quit" in c.text for c in slash_results))

        # Empty input returns nothing
        empty_results = list(completer.get_completions(_MockDoc(""), None))
        _check("completer returns nothing for empty input",
               len(empty_results) == 0)

        # Short input (< MIN_PREFIX) returns nothing
        short_results = list(completer.get_completions(_MockDoc("r"), None))
        _check("completer returns nothing for single char",
               len(short_results) == 0)

        # Server completion (uses mock)
        server_results = list(completer.get_completions(_MockDoc("r.Foo"), None))
        _check("completer fetches server completions",
               len(server_results) > 0)

    # --- Client module validation ---
    print()
    print("Client module:")

    _check("DEFAULT_TIMEOUT is numeric", isinstance(DEFAULT_TIMEOUT, (int, float)))
    _check("DANGEROUS_COMMANDS is a set", isinstance(DANGEROUS_COMMANDS, set))
    _check("DANGEROUS_COMMANDS contains exit", "exit" in DANGEROUS_COMMANDS)

    # Verify RemoteConsole can be instantiated without connecting
    rc = RemoteConsole("127.0.0.1", 0, timeout=1.0)
    _check("RemoteConsole instantiation", rc is not None)
    _check("RemoteConsole.host", rc.host == "127.0.0.1")
    _check("RemoteConsole.port", rc.port == 0)
    _check("RemoteConsole not connected initially", not rc._connected)

    # --- Visual output samples ---
    # Print actual colored lines so highlighting can be verified visually.
    print()
    print("Visual samples (verify colors manually):")
    print()
    samples = [
        {"cat": "LogEngine", "v": "Log", "line": "Console Help:"},
        {"cat": "LogConfig", "v": "Log", "line": "CVar [[r.HZB.BuildUseCompute:1 -> 0]] deferred"},
        {"cat": "LogDeviceProfileManager", "v": "Log", "line": "Pushing Device Profile CVar: [[rhi.syncinterval:1 -> 1]]"},
        {"cat": "None", "v": "Log", "line": 'r.Nanite = "1"      LastSetBy: Constructor'},
        {"cat": "LogMaterial", "v": "Error", "line": "Tried to access an uncooked shader map ID in a cooked application"},
        {"cat": "LogRHI", "v": "Warning", "line": "Shader compilation took 3.5s for 42 shaders"},
        {"cat": "LogRHI", "v": "Display", "line": "    \u2503     0 \u2502      0 \u2502       0 \u2502       0 \u2502  0.0% \u250a          \u2502 0.001 ms \u2503"},
        {"cat": "LogTemp", "v": "Log", "line": "[SubSystem] [Init] Loaded 155 assets (2.4 MB) from 'Content/Paks/main.pak'"},
        {"cat": "LogTemp", "v": "Log", "line": "SpotLight77 created at Position=<100.0, 200.0, 50.0> Scale=1.5"},
        {"cat": "LogTemp", "v": "Log", "line": "<Console variable> = <value>"},
        {"cat": "LogTemp", "v": "Log", "line": "Width=1920, Height=1080, RefreshRate=60"},
        {"cat": "LogTemp", "v": "Log", "line": 'systemresolution.resx="1920" systemresolution.resy="1080"'},
        {"cat": "LogTemp", "v": "Fatal", "line": "Assertion failed: IsValid()"},
        {"cat": "LogTemp", "v": "Log", "line": "Allocated 512KB, freed 2.4MB, peak 1GiB"},
        {"cat": "LogTemp", "v": "Log", "line": "Allocated 512 KB, freed 2.4 MB, peak 1 GiB"},
        {"cat": "LogTemp", "v": "Log", "line": "Upload 64Kb at 1.2Mb/s, latency 45ms, timeout 5s, refresh 60Hz"},
        {"cat": "LogTemp", "v": "Log", "line": "GPU time 145us, CPU time 2.3ms, frame 16.6ms, total 1.5s"},
        {"cat": "LogTemp", "v": "Log", "line": "Latency: min 50ns, avg 1.2us, p99 4.7ms, offset 320px"},
        {"cat": "LogTemp", "v": "Log", "line": "Pool: 256M used, 1G total, 512K free, rate 100MB/s"},
        {"cat": "LogTemp", "v": "Log", "line": "Render target 1920x1080, shadow map 256x256x4, tile 64x64"},
        {"cat": "None", "v": "Log", "line": 'r.DynamicRes.TestScreenPercentage = "40"'},
        {"cat": "None", "v": "Log", "line": "r.DynamicRes.TestScreenPercentage= '40'"},
        {"cat": "None", "v": "Log", "line": "r.DynamicRes.TestScreenPercentage =40"},
    ]
    for msg in samples:
        print(f"  {_format_log_msg(msg)}")
    print()

    # --- Summary ---
    print()
    total = passed + failed
    if failed:
        print(f"FAILED: {failed}/{total} tests failed.")
        sys.exit(1)
    else:
        print(f"OK: {total} tests passed.")
