#!/bin/bash
# Log daemon component - uses signal-based readiness (SIGUSR1)

PIDFILE="/tmp/log-daemon.pid"

# Clean up on exit
trap 'rm -f "$PIDFILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting log daemon (PID $$)..."

# Simulate initialization
echo "Opening log files..."
sleep 1
echo "Starting syslog listener..."
sleep 2

# Signal readiness by sending SIGUSR1 to parent process
# Note: In real use, the component would signal the graph-resolver (parent)
if [ -n "$PPID" ] && [ "$PPID" -ne 1 ]; then
    echo "Sending SIGUSR1 to parent process $PPID"
    kill -USR1 "$PPID" 2>/dev/null || echo "Could not signal parent (this is expected in test mode)"
fi

echo "Log daemon ready and running..."

# Keep running as a daemon
while true; do
    sleep 8
    echo "Processing logs at $(date)"
done