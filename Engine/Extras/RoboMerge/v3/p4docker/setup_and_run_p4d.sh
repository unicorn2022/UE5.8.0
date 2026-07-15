#!/bin/bash
set -e

# Pre-create server.id so p4d starts with the ID already set (no topology warning)
echo "FunctionalTestServer" > /p4root/server.id

# Start p4d
p4d &
sleep 3

# Continue running perforce server -- tail the log
tail -f $P4LOGS