#!/bin/bash
# Slow-ready database component - takes 10 seconds to signal readiness

READINESS_FILE="/tmp/slow-database.ready"
PIDFILE="/tmp/slow-database.pid"

# Clean up on exit
trap 'rm -f "$READINESS_FILE" "$PIDFILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting slow database (PID $$)..."

# Simulate slow initialization (schema setup, data loading, etc.)
echo "Initializing database schema..."
sleep 3
echo "Loading initial data..."
sleep 4
echo "Starting connection listeners..."
sleep 3

# Signal readiness after 10 seconds total
echo "Database ready at $(date)" > "$READINESS_FILE"
echo "Created readiness file: $READINESS_FILE"

# Keep running as a daemon
echo "Database ready and running..."
while true; do
    sleep 15
    echo "Database heartbeat at $(date)"
done