#!/bin/bash
# Health check script for flaky service

HEALTH_FILE="/tmp/flaky-service.health"

# Check the service status
if [ -f "$HEALTH_FILE" ]; then
    status=$(cat "$HEALTH_FILE" 2>/dev/null)
    case "$status" in
        "READY")
            echo "Flaky service is healthy"
            exit 0
            ;;
        "STARTING")
            echo "Flaky service is still starting"
            exit 1
            ;;
        "FAILED")
            echo "Flaky service has failed"
            exit 1
            ;;
        *)
            echo "Flaky service status unknown"
            exit 1
            ;;
    esac
else
    echo "Flaky service health file not found"
    exit 1
fi