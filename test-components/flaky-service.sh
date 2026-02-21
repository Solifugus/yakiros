#!/bin/bash
# Flaky service component - health check sometimes fails

PIDFILE="/tmp/flaky-service.pid"
HEALTH_FILE="/tmp/flaky-service.health"

# Clean up on exit
trap 'rm -f "$PIDFILE" "$HEALTH_FILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting flaky service (PID $$)..."

# Simulate initialization
sleep 3

# Create health file but mark as STARTING
echo "STARTING" > "$HEALTH_FILE"
echo "Flaky service initializing..."

# After a delay, randomly become ready or fail
sleep 5
if [ $(($RANDOM % 2)) -eq 0 ]; then
    echo "READY" > "$HEALTH_FILE"
    echo "Flaky service became ready"
else
    echo "FAILED" > "$HEALTH_FILE"
    echo "Flaky service failed to start properly"
fi

# Keep running
while true; do
    sleep 10
    echo "Flaky service heartbeat at $(date)"
done