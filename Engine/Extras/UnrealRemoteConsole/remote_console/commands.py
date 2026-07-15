"""Internal slash commands and handler for the UE Remote Console REPL."""

from __future__ import annotations

import os
import time
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .client import RemoteConsole

from .formatting import _format_log_msg


INTERNAL_COMMANDS_COMMON = {
    "/help":      "Show this help",
    "/h":         "Show this help",
    "/?":         "Show this help",
    "/quit":      "Disconnect and exit",
    "/exit":      "Disconnect and exit",
    "/q":         "Disconnect and exit",
    "/reconnect": "Reconnect to the target",
    "/clear":     "Clear the console screen",
    "/silent":    "Execute a command silently (full processing, no output). Usage: /silent <cmd>",
}

INTERNAL_COMMANDS_BASIC_ONLY = {
    "/complete":  "Show matching commands with help text.  Usage: /complete <prefix>",
    "/get":       "Get a CVar's current value.  Usage: /get <cvar>",
}

INTERNAL_COMMANDS = {**INTERNAL_COMMANDS_COMMON, **INTERNAL_COMMANDS_BASIC_ONLY}

INTERNAL_HELP_TEXT_COMMON = """  Internal commands (prefix with /):
    /help, /h, /?          Show this help
    /quit, /exit, /q       Disconnect and exit
    /reconnect             Reconnect to the target
    /clear                 Clear the console screen
    /silent <cmd>          Execute command silently (full processing, no output)

  Everything else is sent as a UE console command."""

INTERNAL_HELP_TEXT_BASIC = """  Internal commands (prefix with /):
    /help, /h, /?          Show this help
    /quit, /exit, /q       Disconnect and exit
    /complete <prefix>     Show matching commands with help text
    /get <cvar>            Get a CVar's current value
    /reconnect             Reconnect to the target
    /clear                 Clear the console screen
    /silent <cmd>          Execute command silently (full processing, no output)

  Everything else is sent as a UE console command."""




def _handle_internal_command(cmd: str, console: RemoteConsole, log_fn=print, rich_mode=False) -> bool:
    """Handle a /command. Returns True to continue the loop, False to exit."""
    internal = cmd[1:].strip()
    args_str = ""
    if " " in internal:
        internal, args_str = internal.split(" ", 1)
    internal = internal.lower()

    if internal in ("quit", "exit", "q"):
        return False
    elif internal in ("help", "h", "?"):
        log_fn(INTERNAL_HELP_TEXT_COMMON if rich_mode else INTERNAL_HELP_TEXT_BASIC)
    elif internal == "complete":
        if not args_str:
            log_fn("  Usage: /complete <prefix>")
        else:
            results, total = console.complete(args_str)
            if results:
                max_name = max(len(name) for name, _, _, _ in results)
                max_name = max(max_name, 20)
                for name, value, source, help_text in results:
                    parts = []
                    if value:
                        parts.append(f"={value}")
                    if source:
                        parts.append(f"({source})")
                    if help_text:
                        parts.append(help_text)
                    meta = " ".join(parts)
                    if meta:
                        log_fn(f"  {name:<{max_name}}  {meta}")
                    else:
                        log_fn(f"  {name}")
                if total > len(results):
                    log_fn(f"  ... showing {len(results)} of {total} matches")
                else:
                    log_fn(f"  ({total} matches)")
            else:
                log_fn("  (no matches)")
    elif internal == "get":
        if not args_str:
            log_fn("  Usage: /get <cvar_name>")
        else:
            resp = console.getvar(args_str)
            if "error" in resp:
                log_fn(f"  {resp['error']}")
            else:
                parts = [f"{resp.get('name', args_str)} = \"{resp.get('value', '')}\""]
                source = resp.get("source", "")
                if source:
                    parts.append(f"(SetBy: {source})")
                help_text = resp.get("help", "")
                if help_text:
                    parts.append(f"Help: {help_text}")
                log_fn(f"  {' '.join(parts)}")
    elif internal == "clear":
        if rich_mode:
            from prompt_toolkit.shortcuts import clear
            clear()
        else:
            os.system("cls" if os.name == "nt" else "clear")
    elif internal == "reconnect":
        log_fn("  Reconnecting...")
        console.reconnect()
        if console.connected:
            log_fn("  Reconnected.")
        else:
            log_fn("  Reconnect failed. Auto-reconnect will keep trying.")
    elif internal == "silent":
        if not args_str:
            log_fn("  Usage: /silent <command>")
        else:
            # Execute the command through the full pipeline (including log
            # formatting and syntax highlighting on the callback thread),
            # but suppress the final print.  This is useful for benchmarking
            # the formatting path or executing commands whose output is not
            # needed interactively.
            #
            # Because output arrives asynchronously via the log channel, we
            # keep the silent callback active until 100ms of silence (no new
            # log messages), which indicates the burst has finished.
            last_msg_time = time.monotonic()
            msg_count = 0
            saved_callback = console._log_callback
            def _silent_callback(msg):
                nonlocal last_msg_time, msg_count
                # Run the full formatting pipeline, discard result.
                _format_log_msg(msg)
                last_msg_time = time.monotonic()
                msg_count += 1
            console._log_callback = _silent_callback
            try:
                start = time.monotonic()
                resp = console.execute(args_str)
                if resp.get("error"):
                    log_fn(f"  Error: {resp['error']}")
                # Wait until 100ms of silence on the log stream.
                last_msg_time = time.monotonic()
                while True:
                    time.sleep(0.02)
                    if time.monotonic() - last_msg_time >= 0.1:
                        break
                elapsed = time.monotonic() - start
                log_fn(f"  (silent: {msg_count} log lines processed in {elapsed:.3f}s)")
            finally:
                console._log_callback = saved_callback
    else:
        log_fn(f"  Unknown command: /{internal}  (type /help for list)")
    return True
