"""
remote_console -- Cross-platform client package for UE Remote Console Server.

Platform-specific discovery backends live under
Engine/Platforms/<Platform>/Extras/UnrealRemoteConsole/discovery.py
and are auto-discovered at runtime (same convention as ushell).
"""

import sys

if sys.platform == "win32":
    def _setup_windows_console():
        """Configure Windows console for UTF-8 output and ANSI color codes."""
        import ctypes
        ctypes.windll.kernel32.SetConsoleOutputCP(65001)
        kernel32 = ctypes.windll.kernel32
        for handle_id in (-11, -12):
            handle = kernel32.GetStdHandle(handle_id)
            mode = ctypes.c_ulong()
            if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                kernel32.SetConsoleMode(handle, mode.value | 0x0004)
        for stream in ("stdout", "stderr"):
            s = getattr(sys, stream)
            if hasattr(s, "reconfigure"):
                s.reconfigure(encoding="utf-8", errors="replace")
    _setup_windows_console()
    del _setup_windows_console

from .client import RemoteConsole, DEFAULT_TIMEOUT, DANGEROUS_COMMANDS
from .interactive import interactive_loop, pipe_mode
from .formatting import _format_log_msg, _disable_colors
from .tests import _run_self_tests
