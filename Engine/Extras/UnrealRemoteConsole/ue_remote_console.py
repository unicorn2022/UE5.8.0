#!/usr/bin/env python3
"""
UE Remote Console -- cross-platform entry point.

Auto-discovers platform-specific discovery backends by scanning
Engine/Platforms/*/Extras/UnrealRemoteConsole/discovery.py
(same convention as ushell).

Usage:
    python ue_remote_console.py                              # Auto-detect
    python ue_remote_console.py --platform <name>            # Force platform
    python ue_remote_console.py --host 10.0.0.5 --port 4600  # Direct TCP
    python ue_remote_console.py -c "stat unit"               # Single command
"""

import argparse
import importlib.util
import os
import sys
import time

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from remote_console import (
    RemoteConsole, DEFAULT_TIMEOUT, interactive_loop, pipe_mode,
    _format_log_msg, _disable_colors, _run_self_tests,
)


def _find_engine_platforms_dir():
    """Find Engine/Platforms/ by walking up from this script."""
    # Script is at Engine/Extras/UnrealRemoteConsole/ue_remote_console.py
    d = _SCRIPT_DIR
    for _ in range(5):
        candidate = os.path.join(d, 'Engine', 'Platforms')
        if os.path.isdir(candidate):
            return candidate
        # Also check if we're already inside Engine/
        candidate = os.path.join(d, 'Platforms')
        if os.path.isdir(candidate) and os.path.basename(d) == 'Engine':
            return candidate
        d = os.path.dirname(d)
    return None


def _discover_backends():
    """Scan Engine/Platforms/*/Extras/UnrealRemoteConsole/discovery.py"""
    platforms_dir = _find_engine_platforms_dir()
    if not platforms_dir:
        return {}

    backends = {}
    for platform_name in os.listdir(platforms_dir):
        discovery_path = os.path.join(
            platforms_dir, platform_name, 'Extras', 'UnrealRemoteConsole', 'discovery.py')
        if not os.path.isfile(discovery_path):
            continue
        try:
            spec = importlib.util.spec_from_file_location(
                f"remote_console_discovery_{platform_name}", discovery_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            required = ('PLATFORM_NAME', 'PLATFORM_ALIASES', 'is_available', 'discover', 'resolve')
            if not all(hasattr(mod, a) for a in required):
                continue
            for alias in mod.PLATFORM_ALIASES:
                backends[alias.lower()] = mod
            backends[mod.PLATFORM_NAME.lower()] = mod
        except Exception as e:
            print(f"Warning: Failed to load {discovery_path}: {e}", file=sys.stderr)
    return backends


def _probe_port(host, port, timeout=1.0):
    """Try a TCP connect to check if something is listening. Returns True if reachable."""
    import socket
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect((host, port))
            return True
        except (OSError, socket.timeout):
            return False
        finally:
            try:
                sock.close()
            except OSError:
                pass
    except Exception:
        return False


def _discover_all_targets(backends, args=None, quiet=False):
    """Discover targets across all available platform backends.

    Runs SDK discovery sequentially (fast, local), then probes all discovered
    targets' ports in parallel to verify the UE process is actually running.
    Returns a list of (platform_mod, target_info) tuples.
    """
    from concurrent.futures import ThreadPoolExecutor, as_completed

    # Phase 1: gather candidates from all backends (SDK tool calls, fast)
    candidates = []  # (mod, target_info, host, port, label)
    checked_platforms = []
    for mod in dict.fromkeys(backends.values()):
        try:
            if not mod.is_available():
                continue
            checked_platforms.append(mod.PLATFORM_NAME)
            targets = mod.discover(args=args, quiet=True)
            for t in targets:
                host, port, label = mod.resolve(t)
                candidates.append((mod, t, host, port, label))
            if not targets and not quiet:
                print(f"  {mod.PLATFORM_NAME}: no targets found", file=sys.stderr)
        except Exception:
            continue

    if not candidates:
        if not quiet and not checked_platforms:
            print("  No platform SDKs available.", file=sys.stderr)
        return []

    # Phase 2: probe all candidates in parallel, cancel remaining on first success
    results = []
    with ThreadPoolExecutor(max_workers=len(candidates)) as pool:
        future_to_candidate = {
            pool.submit(_probe_port, host, port): (mod, t, host, port, label)
            for mod, t, host, port, label in candidates
        }
        try:
            for future in as_completed(future_to_candidate):
                mod, t, host, port, label = future_to_candidate[future]
                try:
                    if future.result():
                        results.append((mod, t))
                    elif not quiet:
                        print(f"  {label or mod.PLATFORM_NAME}: devkit found but port {port} not responding (game not running?)",
                              file=sys.stderr)
                except Exception:
                    pass
        finally:
            # Cancel any probes still in flight (they'll finish on their own
            # timeout but we won't block waiting for them)
            for f in future_to_candidate:
                f.cancel()

    return results


def _select_target(all_targets):
    """Present a selection menu when multiple targets are found.

    Returns the selected (platform_mod, target_info) tuple, or None if aborted.
    """
    print("\nMultiple targets found:", file=sys.stderr)
    for i, (mod, info) in enumerate(all_targets, 1):
        host, port, label = mod.resolve(info)
        print(f"  [{i}] {label or mod.PLATFORM_NAME} ({host}:{port})", file=sys.stderr)

    while True:
        try:
            choice = input(f"\nSelect target [1-{len(all_targets)}]: ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nAborted.", file=sys.stderr)
            return None
        try:
            idx = int(choice)
            if 1 <= idx <= len(all_targets):
                return all_targets[idx - 1]
        except ValueError:
            pass
        print(f"Invalid choice. Enter a number between 1 and {len(all_targets)}.", file=sys.stderr)


def main():
    backends = _discover_backends()

    parser = argparse.ArgumentParser(
        description="UE Remote Console",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                              Auto-detect platform and connect
  %(prog)s --platform <name>            Force platform backend
  %(prog)s --host 10.0.0.5 --port 4600  Direct TCP (any platform)
  %(prog)s -c "stat unit"               Single command
""")
    parser.add_argument("--host", help="Target IP (bypasses platform discovery)")
    parser.add_argument("--port", type=int, help="Target port (bypasses platform discovery)")
    parser.add_argument("--platform", help="Force platform backend (see --list-platforms)")
    parser.add_argument("-c", "--command", action="append",
                        help="Execute command(s) and exit")
    parser.add_argument("-t", "--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help=f"Response timeout in seconds (default: {DEFAULT_TIMEOUT})")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Diagnostic debug output")
    parser.add_argument("--debug-log-raw", metavar="FILE",
                        help="Log raw bytes received from server to FILE")
    parser.add_argument("--debug-log-decoded", metavar="FILE",
                        help="Log decoded text to FILE (UTF-8)")
    parser.add_argument("--test", action="store_true",
                        help="Run self-tests and exit")
    parser.add_argument("--list-platforms", action="store_true",
                        help="List discovered platform backends and exit")
    parser.add_argument("--no-color", action="store_true",
                        help="Disable colored output")

    for mod in dict.fromkeys(backends.values()):
        if hasattr(mod, 'add_arguments'):
            try:
                mod.add_arguments(parser)
            except Exception:
                pass

    args = parser.parse_args()

    if args.no_color:
        _disable_colors()

    if args.test:
        _run_self_tests()
        return

    if args.list_platforms:
        seen = set()
        for mod in backends.values():
            if id(mod) not in seen:
                seen.add(id(mod))
                avail = "available" if mod.is_available() else "not available"
                aliases = ", ".join(mod.PLATFORM_ALIASES)
                print(f"  {mod.PLATFORM_NAME:<20s} ({avail})  aliases: {aliases}")
        if not backends:
            print("  No platform backends found.")
        return

    # Resolve platform
    platform_mod = None
    if args.host and args.port:
        pass
    elif args.platform:
        platform_mod = backends.get(args.platform.lower())
        if not platform_mod:
            avail = ", ".join(sorted(set(m.PLATFORM_NAME.lower() for m in backends.values())))
            print(f"Error: Unknown platform '{args.platform}'. Available: {avail or 'none'}",
                  file=sys.stderr)
            sys.exit(1)

    # Discover target
    host, port, label = args.host, args.port, None
    if host and not port:
        print("Error: --port is required with --host.", file=sys.stderr)
        sys.exit(1)
    if port and not host:
        print("Error: --host is required with --port.", file=sys.stderr)
        sys.exit(1)

    if not host or not port:
        if platform_mod:
            # Explicit --platform: discover only from that backend
            print(f"Using {platform_mod.PLATFORM_NAME} discovery...", file=sys.stderr)
            targets = platform_mod.discover(args=args)
            if not targets:
                print(f"No targets found. Retrying every 1s (Ctrl+C to abort)...", file=sys.stderr)
                while not targets:
                    try:
                        time.sleep(1.0)
                    except KeyboardInterrupt:
                        print("\nAborted.", file=sys.stderr)
                        sys.exit(1)
                    targets = platform_mod.discover(args=args, quiet=True)
            target_info = targets[0]
            host, port, label = platform_mod.resolve(target_info)
        else:
            # Auto-detect: discover across ALL platforms, present menu if multiple
            print("Scanning for targets...", file=sys.stderr)
            all_targets = _discover_all_targets(backends, args=args)
            if not all_targets:
                print("No targets found. Retrying every 1s (Ctrl+C to abort)...", file=sys.stderr)
                while not all_targets:
                    try:
                        time.sleep(1.0)
                    except KeyboardInterrupt:
                        print("\nAborted.", file=sys.stderr)
                        sys.exit(1)
                    all_targets = _discover_all_targets(backends, args=args, quiet=True)

            if len(all_targets) == 1:
                platform_mod, target_info = all_targets[0]
            else:
                selection = _select_target(all_targets)
                if selection is None:
                    sys.exit(1)
                platform_mod, target_info = selection

            host, port, label = platform_mod.resolve(target_info)

        print(f"Found {label or 'target'} at {host}:{port}", file=sys.stderr)

    console = RemoteConsole(host, port, timeout=args.timeout, verbose=args.verbose,
                            log_file=args.debug_log_raw, decoded_log_file=args.debug_log_decoded)
    if label:
        console.label = label

    if platform_mod and not args.host:
        console.set_endpoint_discovery(
            lambda: _rediscover(platform_mod, args))

    print(f"Connecting to {host}:{port}...", file=sys.stderr)

    if not console.connect(quiet=True):
        print("Connection failed, retrying every 1s (Ctrl+C to abort)...", file=sys.stderr)
        while not console.connect(quiet=True):
            try:
                time.sleep(1.0)
            except KeyboardInterrupt:
                print("\nAborted.", file=sys.stderr)
                sys.exit(1)
            console.try_rediscover_endpoint()

    print("Connected.", file=sys.stderr)

    try:
        if args.command:
            _last_log_time = [time.monotonic()]
            def _cmd_log_cb(msg):
                print(f"  {_format_log_msg(msg)}")
                _last_log_time[0] = time.monotonic()
            console.set_log_callback(_cmd_log_cb)
            for cmd in args.command:
                resp = console.execute(cmd)
                if resp.get("error"):
                    print(f"Error: {resp['error']}")
                _last_log_time[0] = time.monotonic()
            while time.monotonic() - _last_log_time[0] < 0.15:
                time.sleep(0.05)
        elif sys.stdin.isatty():
            interactive_loop(console)
        else:
            pipe_mode(console)
    except (ConnectionError, TimeoutError) as e:
        print(f"Connection lost: {e}", file=sys.stderr)
    finally:
        console.close(permanent=True)


def _rediscover(platform_mod, args):
    targets = platform_mod.discover(args=args, quiet=True)
    if targets:
        return platform_mod.resolve(targets[0])
    return None, None, None


if __name__ == "__main__":
    main()
