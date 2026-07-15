#!/usr/bin/env python3
import shlex, subprocess, sys

if len(sys.argv) < 3:
    raise RuntimeError(
        "expected one argument containing the build arg value provided at runtime, and at least one fixed argument"
    )

# Expand the arguments that were provided via the Docker build arg and append them to the fixed arguments
expanded = shlex.split(sys.argv[1])
combined = sys.argv[2:] + expanded

# Print and run the combined command
print(combined, file=sys.stderr, flush=True)
code = subprocess.run(combined, check=False).returncode
sys.exit(code)
