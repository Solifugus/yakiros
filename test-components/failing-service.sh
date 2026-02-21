#!/bin/bash
# Failing service component - never signals readiness (for testing timeouts)

PIDFILE="/tmp/failing-service.pid"

# Clean up on exit
trap 'rm -f "$PIDFILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting failing service (PID $$)..."

# Simulate a service that starts but never becomes ready
echo "Initializing service..."
sleep 2
echo "Waiting for external dependency that never comes..."

# Keep running but never signal readiness
while true; do
    sleep 10
    echo "Still waiting for readiness condition at $(date)"
done