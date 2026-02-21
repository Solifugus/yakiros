#!/bin/bash
# Test script for readiness-aware components

set -e

echo "Setting up readiness protocol test..."

# Clean up any previous test files
rm -f /tmp/fast-web-server.ready /tmp/slow-database.ready /tmp/failing-service.ready
rm -f /tmp/api-service.health /tmp/flaky-service.health
rm -f /tmp/*.pid

# Create test graph directory
mkdir -p /tmp/yakiros/graph.d

# Copy component configurations
echo "Copying test component configurations..."
cp test-components/*.toml /tmp/yakiros/graph.d/

echo "Test components configured in /tmp/yakiros/graph.d/"
echo ""
echo "To test:"
echo "1. Run: sudo ./graph-resolver"
echo "2. In another terminal: ./graphctl status"
echo ""
echo "Expected behavior:"
echo "- fast-web-server: Ready in ~2 seconds"
echo "- slow-database: Ready in ~10 seconds"
echo "- api-service: Ready in ~4 seconds"
echo "- log-daemon: Ready in ~3 seconds"
echo "- flaky-service: Randomly succeeds/fails"
echo "- failing-service: Times out after 8 seconds"
echo ""
echo "Test setup complete!"