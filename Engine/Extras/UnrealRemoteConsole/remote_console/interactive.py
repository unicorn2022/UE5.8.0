"""Interactive REPL loops for the UE Remote Console.

Rich mode (prompt_toolkit): tab-completion, ghost text, history, batched log output.
Basic mode (input()): simple fallback when prompt_toolkit is not installed.
"""

import os
import sys
import threading
import time

import socket

from .client import RemoteConsole, DANGEROUS_COMMANDS
from .commands import _handle_internal_command, INTERNAL_COMMANDS_COMMON
from .formatting import _format_log_msg


# ---------------------------------------------------------------------------

try:
    from prompt_toolkit import PromptSession
    from prompt_toolkit.completion import Completer, Completion, ThreadedCompleter
    from prompt_toolkit.history import FileHistory
    from prompt_toolkit.auto_suggest import AutoSuggest, Suggestion
    from prompt_toolkit.key_binding import KeyBindings
    from prompt_toolkit.styles import Style
    from prompt_toolkit.patch_stdout import patch_stdout
    from prompt_toolkit import print_formatted_text
    from prompt_toolkit.formatted_text import ANSI
    HAS_PROMPT_TOOLKIT = True
except ImportError as e:
    HAS_PROMPT_TOOLKIT = False
    _PT_IMPORT_ERROR = str(e)


if HAS_PROMPT_TOOLKIT:

    class UECompleter(Completer):
        """Server-assisted completer with caching."""

        PAGE_SIZE = 200
        MIN_PREFIX = 2

        def __init__(self, console: RemoteConsole):
            self.console = console
            self._cached_prefix = ""
            self._cached_results = []
            self._cached_total = 0
            self._cached_time = 0.0
            self._cache_ttl = 10.0

        def _from_cache(self, prefix: str):
            now = time.monotonic()
            if not self._cached_prefix or (now - self._cached_time) > self._cache_ttl:
                return None
            # Don't serve from cache if the cached result was empty (server may not have been ready)
            if not self._cached_results:
                return None
            if (prefix.startswith(self._cached_prefix)
                    and self._cached_total <= len(self._cached_results)):
                pl = prefix.lower()
                return [(n, v, s, h) for n, v, s, h in self._cached_results
                        if n.lower().startswith(pl)]
            return None

        def _fetch(self, prefix: str):
            try:
                results, total = self.console.complete(prefix, offset=0, limit=self.PAGE_SIZE)
            except (socket.timeout, ConnectionError, OSError, TimeoutError):
                return []
            self._cached_prefix = prefix
            self._cached_results = results
            self._cached_total = total
            self._cached_time = time.monotonic()
            return list(results)  # copy: caller may sort in-place

        def get_completions(self, document, complete_event):
            text = document.text_before_cursor.lstrip()
            if not text:
                return

            # Internal /commands
            if text.startswith("/"):
                typed = text.lower()
                for name, desc in INTERNAL_COMMANDS_COMMON.items():
                    if name.startswith(typed):
                        yield Completion(name, start_position=-len(text),
                                         display_meta=desc)
                return

            if len(text) < self.MIN_PREFIX:
                return

            results = self._from_cache(text)
            if results is None:
                results = self._fetch(text)

            # Sort: shorter names first (so "r.Nanite" comes before
            # "r.Nanite.AllowMaskedMaterials"), then alphabetically.
            results.sort(key=lambda r: (len(r[0]), r[0].lower()))

            for name, value, source, help_text in results:
                parts = []
                if value:
                    parts.append(f"={value}")
                if source:
                    parts.append(f"({source})")
                if help_text:
                    parts.append(help_text)
                meta = " ".join(parts)
                yield Completion(name, start_position=-len(text), display_meta=meta)

    class UEAutoSuggest(AutoSuggest):
        """Ghost text from the first completion match."""

        def __init__(self, completer: UECompleter):
            self._completer = completer

        def get_suggestion(self, buffer, document):
            text = document.text_before_cursor.lstrip()
            if not text or len(text) < 3:
                return None
            # Use cached results only - never block the UI with a network call.
            results = self._completer._from_cache(text)
            if results is None:
                return None
            for name, _, _, _ in results:
                if name.lower().startswith(text.lower()) and len(name) > len(text):
                    return Suggestion(name[len(text):])
            return None


def _interactive_loop_rich(console: RemoteConsole):
    """Rich interactive mode with prompt_toolkit."""

    # Build prompt string: "TargetName> " or "ue@host:port> "
    if console.label:
        prompt_str = f"{console.label}> "
    else:
        prompt_str = f"ue@{console.host}:{console.port}> "

    reconnected_event = threading.Event()  # signaled when connection is restored
    is_disconnected = threading.Event()    # stays set while disconnected

    completer = UECompleter(console)
    threaded_completer = ThreadedCompleter(completer)
    history_file = os.path.expanduser("~/.ue_remote_console_history")

    style = Style.from_dict({
        "completion-menu.completion":             "bg:#333333 #ffffff",
        "completion-menu.completion.current":     "bg:#555555 #ffffff bold",
        "completion-menu.meta.completion":        "bg:#333333 #aaaaaa",
        "completion-menu.meta.completion.current": "bg:#555555 #cccccc",
        "auto-suggest":                           "#666666",
    })

    bindings = KeyBindings()

    @bindings.add("escape")
    def _on_escape(event):
        buf = event.app.current_buffer
        if buf.complete_state:
            buf.cancel_completion()

    @bindings.add("pagedown")
    def _on_pagedown(event):
        buf = event.app.current_buffer
        if buf.complete_state:
            for _ in range(10):
                buf.complete_next()

    @bindings.add("pageup")
    def _on_pageup(event):
        buf = event.app.current_buffer
        if buf.complete_state:
            for _ in range(10):
                buf.complete_previous()

    @bindings.add("c-l")
    def _on_ctrl_l(event):
        buf = event.app.current_buffer
        if buf.text:
            buf.reset()
        else:
            # Clear screen and reposition cursor to top, then invalidate
            # the renderer so prompt_toolkit redraws the prompt line.
            event.app.renderer.output.write_raw("\033[2J\033[H")
            event.app.renderer.output.flush()
            event.app.renderer.reset()
            event.app.invalidate()

    @bindings.add("c-h")  # Backspace sends ^H
    @bindings.add("backspace")
    def _on_backspace(event):
        buf = event.app.current_buffer
        if buf.document.cursor_position > 0:
            buf.delete_before_cursor()
        # else: do nothing (no bell)

    pending_dangerous = ""

    session = PromptSession(
        history=FileHistory(history_file),
        auto_suggest=UEAutoSuggest(completer),
        completer=threaded_completer,
        complete_while_typing=True,
        style=style,
        key_bindings=bindings,
        reserve_space_for_menu=12,
        enable_system_prompt=False,
        enable_open_in_editor=False,
    )

    def _on_disconnect():
        is_disconnected.set()
        reconnected_event.clear()
        # Flush any buffered log lines so they appear before the disconnect message.
        _flush_log_buf()
        print("  [disconnected -- waiting for reconnection...]")
        # Abort the active prompt so the user can't type into a dead connection.
        # session.app is only set while prompt() is blocking.
        app = session.app
        if app:
            app.exit(result="")

    def _on_reconnect():
        is_disconnected.clear()
        reconnected_event.set()
        print("  [reconnected]")

    console.enable_auto_reconnect(on_disconnect=_on_disconnect, on_reconnect=_on_reconnect)

    print("Type console commands. Tab: complete. Up/Down: history. /help: commands. Ctrl+D to exit.")
    print()

    console.set_log_callback(None)  # placeholder, set inside patch_stdout below

    _log_flush_stop = threading.Event()
    _log_flush_thread = None
    try:
      with patch_stdout(raw=True):
        # Set the log callback *inside* patch_stdout so that print_formatted_text
        # uses the patched output and renders ANSI colors correctly.
        #
        # Log lines are buffered and flushed in batches to avoid the overhead
        # of per-line print_formatted_text() calls through prompt_toolkit's
        # rendering pipeline. This makes a large difference for commands that
        # produce hundreds of log lines (e.g., ProfileGPU: 589 lines went
        # from >2s to ~0.3s).
        _log_buf = []
        _log_buf_lock = threading.Lock()

        def _flush_log_buf():
            """Write all buffered log lines to the terminal in one shot."""
            with _log_buf_lock:
                if not _log_buf:
                    return
                batch = "".join(_log_buf)
                _log_buf.clear()
            # Write directly to stdout -- the ANSI escapes are already
            # embedded in the formatted strings. sys.stdout is patched by
            # patch_stdout() so this cooperates with prompt_toolkit.
            sys.stdout.write(batch)
            sys.stdout.flush()

        def _on_log(msg):
            if is_disconnected.is_set():
                return  # Discard logs after disconnect to avoid stale output
            formatted = f"  {_format_log_msg(msg)}\n"
            with _log_buf_lock:
                _log_buf.append(formatted)

        # Background thread to flush log buffer at regular intervals.
        # 50ms gives a good balance between responsiveness and batching.
        def _log_flush_loop():
            while not _log_flush_stop.is_set():
                _flush_log_buf()
                _log_flush_stop.wait(0.05)
            _flush_log_buf()  # final flush

        _log_flush_thread = threading.Thread(target=_log_flush_loop, daemon=True)
        _log_flush_thread.start()
        console.set_log_callback(_on_log)
        while True:
            # Block while disconnected - don't show input prompt until connected.
            # Use short sleeps so Ctrl+C can be delivered on Windows (Event.wait
            # swallows KeyboardInterrupt on some Python/OS combinations).
            if is_disconnected.is_set():
                try:
                    while not reconnected_event.is_set():
                        if not is_disconnected.is_set():
                            break
                        time.sleep(0.2)
                except KeyboardInterrupt:
                    print()
                    break
                reconnected_event.clear()
                continue

            try:
                cmd = session.prompt(prompt_str).strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not cmd:
                continue

            # Reject input if we lost connection between prompt display and submit.
            if is_disconnected.is_set():
                print("  [not connected]")
                continue

            if cmd.startswith("/"):
                if not _handle_internal_command(cmd, console, rich_mode=True):
                    break
                continue

            # Dangerous command confirmation
            first_word = cmd.split()[0].lower()
            if first_word in DANGEROUS_COMMANDS:
                if cmd == pending_dangerous:
                    pending_dangerous = ""
                else:
                    pending_dangerous = cmd
                    print(f"  WARNING: '{cmd}' may disrupt the target. Enter same command again to confirm.")
                    print()
                    continue
            else:
                pending_dangerous = ""

            try:
                start = time.monotonic()
                resp = console.execute(cmd)
                elapsed = time.monotonic() - start
                if resp.get("error"):
                    print(f"  Error: {resp['error']}")
                print(f"  ({elapsed:.3f}s)")
            except TimeoutError as e:
                print(f"  Timed out: {e} (server may be paused on a breakpoint)", file=sys.stderr)
            except ConnectionError as e:
                print(f"  Connection lost: {e}", file=sys.stderr)
                print("  Waiting for reconnection (Ctrl+D to exit)...", file=sys.stderr)
                # Ensure disconnected state is visible even if the callback
                # hasn't fired yet (race between reader thread and main thread).
                is_disconnected.set()
    finally:
        _log_flush_stop.set()
        if _log_flush_thread is not None:
            _log_flush_thread.join(timeout=1.0)




def _interactive_loop_basic(console: RemoteConsole):
    """Simple input() REPL with no TUI."""

    # Build prompt string
    if console.label:
        prompt_str = f"{console.label}> "
    else:
        prompt_str = f"ue@{console.host}:{console.port}> "

    reconnected_event = threading.Event()
    is_disconnected = threading.Event()

    def _on_disconnect():
        is_disconnected.set()
        reconnected_event.clear()
        print("  [disconnected -- waiting for reconnection...]", file=sys.stderr)

    def _on_reconnect():
        is_disconnected.clear()
        reconnected_event.set()
        print("  [reconnected]")

    console.enable_auto_reconnect(on_disconnect=_on_disconnect, on_reconnect=_on_reconnect)

    print("Type console commands. /help for internal commands. Ctrl+C to exit.")
    print()

    # Print log lines directly from the callback thread.
    # Basic mode uses input() which doesn't control the terminal,
    # so interleaved output is acceptable.
    def _on_log(msg):
        print(f"  {_format_log_msg(msg)}")

    console.set_log_callback(_on_log)

    while True:
        # Block while disconnected - don't show input prompt until connected.
        if is_disconnected.is_set():
            try:
                while not reconnected_event.is_set():
                    if not is_disconnected.is_set():
                        break
                    time.sleep(0.2)
            except KeyboardInterrupt:
                print()
                break
            reconnected_event.clear()
            continue

        try:
            cmd = input(prompt_str).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not cmd:
            continue

        # Reject input if we lost connection between prompt display and submit.
        if is_disconnected.is_set():
            print("  [not connected]")
            continue

        try:
            if cmd.startswith("/"):
                if not _handle_internal_command(cmd, console):
                    break
            else:
                first_word = cmd.split()[0].lower()
                if first_word in DANGEROUS_COMMANDS:
                    try:
                        confirm = input(
                            f"  Send '{cmd}' to remote? This may disrupt the target. [y/N] "
                        ).strip().lower()
                    except (EOFError, KeyboardInterrupt):
                        print()
                        continue
                    if confirm not in ("y", "yes"):
                        print("  (cancelled)")
                        print()
                        continue
                start = time.monotonic()
                resp = console.execute(cmd)
                elapsed = time.monotonic() - start
                if resp.get("error"):
                    print(f"  Error: {resp['error']}")
                print(f"  ({elapsed:.3f}s)")
        except TimeoutError as e:
            print(f"  Timed out: {e} (server may be paused on a breakpoint)", file=sys.stderr)
        except ConnectionError as e:
            print(f"  Connection lost: {e}", file=sys.stderr)
            print("  Waiting for reconnection (Ctrl+C to exit)...", file=sys.stderr)
            is_disconnected.set()


def interactive_loop(console: RemoteConsole):
    """Interactive REPL - rich mode with prompt_toolkit, or basic fallback."""
    if HAS_PROMPT_TOOLKIT:
        _interactive_loop_rich(console)
    else:
        err = globals().get("_PT_IMPORT_ERROR", "not installed")
        print("Tip: Install 'prompt_toolkit' for auto-complete, ghost text, and history:",
              file=sys.stderr)
        print("  pip install prompt_toolkit", file=sys.stderr)
        print(f"  (import error: {err})", file=sys.stderr)
        print(file=sys.stderr)
        _interactive_loop_basic(console)


def pipe_mode(console: RemoteConsole):
    """Read commands from stdin (non-interactive, for scripting)."""
    for raw_line in sys.stdin:
        cmd = raw_line.strip()
        if not cmd or cmd.startswith("#"):
            continue
        try:
            resp = console.execute(cmd)
            if resp.get("error"):
                print(f"Error: {resp['error']}")
        except (ConnectionError, TimeoutError) as e:
            print(f"Error: {e}", file=sys.stderr)
            break
