#!/bin/bash
# Health check script for API service

HEALTH_FILE="/tmp/api-service.health"

# Check if the service is ready
if [ -f "$HEALTH_FILE" ] && [ "$(cat "$HEALTH_FILE" 2>/dev/null)" = "READY" ]; then
    echo "API service is healthy"
    exit 0
else
    echo "API service not ready"
    exit 1
fi