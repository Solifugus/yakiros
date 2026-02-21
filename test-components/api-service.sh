#!/bin/bash
# API service component - uses command-based readiness checking

PIDFILE="/tmp/api-service.pid"
HEALTH_FILE="/tmp/api-service.health"

# Clean up on exit
trap 'rm -f "$PIDFILE" "$HEALTH_FILE"; exit' EXIT TERM

# Write PID file
echo $$ > "$PIDFILE"

echo "Starting API service (PID $$)..."

# Simulate initialization
echo "Loading API configuration..."
sleep 2
echo "Starting HTTP server..."
sleep 2

# Create health status file to indicate we're ready for health checks
echo "READY" > "$HEALTH_FILE"
echo "API service ready for health checks"

# Keep running as a daemon
echo "API service ready and running..."
while true; do
    sleep 12
    echo "API service heartbeat at $(date)"
done