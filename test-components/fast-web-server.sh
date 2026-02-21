#!/bin/bash
# Fast-ready web server component - signals readiness immediately

READINESS_FILE="/tmp/fast-web-server.ready"
PIDFILE="/tmp/fast-web-server.pid"

# Clean up on exit
trap 'rm -f "$READINESS_FILE" "$PIDFILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting fast web server (PID $$)..."

# Simulate initialization
sleep 1

# Signal readiness immediately by creating the file
echo "Web server ready at $(date)" > "$READINESS_FILE"
echo "Created readiness file: $READINESS_FILE"

# Keep running as a daemon
echo "Web server ready and running..."
while true; do
    sleep 10
    echo "Web server heartbeat at $(date)"
done