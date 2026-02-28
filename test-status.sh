#!/bin/bash

# Test enhanced graphctl status command

echo "Testing enhanced graphctl status command..."

# Start graph-resolver in background with test config
./graph-resolver --config-dir ./test-config --control-socket /tmp/test-graph.sock &
RESOLVER_PID=$!

# Give it time to start
sleep 2

# Test the status command
echo "status" | nc -U /tmp/test-graph.sock || {
    # If nc doesn't work, try with a manual echo
    echo "status" | timeout 5 ./graphctl --socket /tmp/test-graph.sock || {
        echo "Testing with internal command..."
        timeout 5 ./graphctl status || echo "Direct connection test..."
    }
}

# Clean up
kill $RESOLVER_PID 2>/dev/null
rm -f /tmp/test-graph.sock

echo "Test completed."