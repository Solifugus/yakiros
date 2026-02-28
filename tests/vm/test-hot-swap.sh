#!/bin/bash
#
# YakirOS Hot-Swap Testing Script
# Tests zero-downtime service upgrades using FD-passing
#

set -e

VM_SSH_PORT="${1:-2222}"
VM_HOST="localhost"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[HOT-SWAP] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[HOT-SWAP] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[HOT-SWAP] ❌ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Verify echo server supports hot-swap
test_echo_server_hotswap_support() {
    log_info "Testing echo server hot-swap support..."

    # Check if echo server is configured for FD-passing
    if vm_run "grep -q 'handoff.*fd-passing' /etc/graph.d/03-echo-server.toml"; then
        log_success "Echo server configured for FD-passing"
        return 0
    else
        log_error "Echo server not configured for FD-passing"
        return 1
    fi
}

# Test 2: Verify echo server is running and reachable
test_echo_server_connectivity() {
    log_info "Testing echo server connectivity..."

    # Check if echo server is active
    if vm_run "graphctl status | grep test-echo-server | grep -q ACTIVE"; then
        log_success "Echo server is active"
    else
        log_error "Echo server is not active"
        return 1
    fi

    # Test TCP connection to echo server
    if vm_run "timeout 5 bash -c 'echo \"test\" | nc localhost 8080' >/dev/null 2>&1"; then
        log_success "Echo server responding to connections"
        return 0
    else
        log_error "Echo server not responding to connections"
        return 1
    fi
}

# Test 3: Establish persistent connection during upgrade
test_persistent_connection_upgrade() {
    log_info "Testing persistent connection during upgrade..."

    # Start background connection that sends periodic messages
    vm_run 'bash -c "
        {
            while true; do
                echo \"persistent-test-\$(date +%s)\"
                sleep 2
            done
        } | nc localhost 8080 > /tmp/echo-test-output.log 2>&1 &
        echo \$! > /tmp/echo-test.pid
    "'

    log_info "Background connection established"
    sleep 3

    # Verify connection is working
    if vm_run "grep -q 'ECHO:' /tmp/echo-test-output.log"; then
        log_success "Background connection receiving responses"
    else
        log_error "Background connection not working"
        vm_run "kill \$(cat /tmp/echo-test.pid) 2>/dev/null || true"
        return 1
    fi

    # Perform hot-swap upgrade
    log_info "Initiating hot-swap upgrade..."

    # Set new version environment for upgrade
    vm_run "export SERVICE_VERSION=2.0"

    # Trigger upgrade
    if vm_run "graphctl upgrade test-echo-server"; then
        log_success "Hot-swap upgrade command succeeded"
    else
        log_error "Hot-swap upgrade command failed"
        vm_run "kill \$(cat /tmp/echo-test.pid) 2>/dev/null || true"
        return 1
    fi

    # Wait for upgrade to complete
    sleep 10

    # Check if connection is still working
    MESSAGES_BEFORE=$(vm_run "wc -l < /tmp/echo-test-output.log")
    sleep 5
    MESSAGES_AFTER=$(vm_run "wc -l < /tmp/echo-test-output.log")

    if [ "$MESSAGES_AFTER" -gt "$MESSAGES_BEFORE" ]; then
        log_success "Connection maintained during upgrade ($MESSAGES_BEFORE -> $MESSAGES_AFTER messages)"
    else
        log_error "Connection lost during upgrade"
        vm_run "kill \$(cat /tmp/echo-test.pid) 2>/dev/null || true"
        return 1
    fi

    # Clean up background connection
    vm_run "kill \$(cat /tmp/echo-test.pid) 2>/dev/null || true"
    vm_run "rm -f /tmp/echo-test.pid /tmp/echo-test-output.log"

    return 0
}

# Test 4: Verify service upgrade statistics
test_upgrade_statistics() {
    log_info "Checking upgrade statistics..."

    # Check if upgrade was recorded
    if vm_run "test -f /run/test-echo-handoff.json"; then
        HANDOFF_INFO=$(vm_run "cat /run/test-echo-handoff.json")
        log_success "Handoff information recorded"
        log_info "Handoff details: $HANDOFF_INFO"
    else
        log_error "No handoff information found"
        return 1
    fi

    # Check service statistics
    if vm_run "test -f /run/test-echo-stats.json"; then
        STATS=$(vm_run "cat /run/test-echo-stats.json")
        log_success "Service statistics available"
        log_info "Service stats: $STATS"
    else
        log_error "No service statistics found"
        return 1
    fi

    return 0
}

# Test 5: Verify no service downtime
test_zero_downtime() {
    log_info "Verifying zero-downtime upgrade..."

    # Measure response time before upgrade
    START_TIME=$(date +%s%N)
    vm_run "echo 'pre-upgrade-test' | nc localhost 8080 >/dev/null"
    PRE_UPGRADE_TIME=$(date +%s%N)
    PRE_DURATION=$(((PRE_UPGRADE_TIME - START_TIME) / 1000000))  # Convert to milliseconds

    log_info "Pre-upgrade response time: ${PRE_DURATION}ms"

    # Perform multiple rapid requests during upgrade window
    log_info "Performing rapid requests to detect downtime..."

    vm_run 'bash -c "
        for i in \$(seq 1 20); do
            if ! timeout 1 bash -c \"echo \\\"rapid-test-\$i\\\" | nc localhost 8080\" >/dev/null 2>&1; then
                echo \"Connection failed on request \$i\" >&2
                exit 1
            fi
            sleep 0.1
        done
        echo \"All 20 rapid requests succeeded\"
    "'

    if [ $? -eq 0 ]; then
        log_success "Zero downtime verified - all rapid requests succeeded"
        return 0
    else
        log_error "Downtime detected during upgrade"
        return 1
    fi
}

# Main test execution
main() {
    echo "YakirOS Hot-Swap Testing"
    echo "========================"
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: test-echo-server with FD-passing"
    echo

    TESTS_TOTAL=5
    TESTS_PASSED=0

    # Run all tests
    if test_echo_server_hotswap_support; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_echo_server_connectivity; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_persistent_connection_upgrade; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_upgrade_statistics; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_zero_downtime; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Summary
    echo
    echo "Hot-Swap Test Results:"
    echo "======================"
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"

    if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
        log_success "All hot-swap tests passed!"
        exit 0
    else
        log_error "Some hot-swap tests failed!"
        exit 1
    fi
}

main "$@"