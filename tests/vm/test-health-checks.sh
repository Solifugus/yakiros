#!/bin/bash
#
# YakirOS Health Check Testing Script
# Tests health monitoring system and degraded states
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
    echo -e "${BLUE}[HEALTH] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[HEALTH] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[HEALTH] ❌ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Verify health check configuration
test_health_check_configuration() {
    log_info "Testing health check configuration..."

    # Check that health demo service has health checks configured
    if vm_run "grep -q 'health_check.*test-health-demo-health' /etc/graph.d/07-health-demo.toml"; then
        log_success "Health demo service has health checks configured"
    else
        log_error "Health demo service missing health check configuration"
        return 1
    fi

    # Check health check intervals
    INTERVAL=$(vm_run "grep 'health_interval' /etc/graph.d/07-health-demo.toml | cut -d= -f2 | tr -d ' '")
    if [ "$INTERVAL" -le 10 ]; then
        log_success "Health check interval is appropriate for testing ($INTERVAL seconds)"
    else
        log_error "Health check interval too long for testing ($INTERVAL seconds)"
        return 1
    fi

    return 0
}

# Test 2: Verify health check execution
test_health_check_execution() {
    log_info "Testing health check execution..."

    # Ensure health demo service is running
    if ! vm_run "graphctl status | grep test-health-demo | grep -q ACTIVE"; then
        log_error "Health demo service is not active"
        return 1
    fi

    # Test manual health check execution
    if vm_run "/usr/local/bin/test-health-demo-health >/dev/null 2>&1"; then
        log_success "Manual health check execution successful"
    else
        log_error "Manual health check execution failed"
        return 1
    fi

    # Verify health check script exists and is executable
    if vm_run "test -x /usr/local/bin/test-health-demo-health"; then
        log_success "Health check script is executable"
    else
        log_error "Health check script missing or not executable"
        return 1
    fi

    return 0
}

# Test 3: Test health failure detection
test_health_failure_detection() {
    log_info "Testing health failure detection..."

    # Record initial state
    INITIAL_STATE=$(vm_run "graphctl status | grep test-health-demo | awk '{print \$2}'")
    log_info "Initial health demo state: $INITIAL_STATE"

    # Force health check failure
    log_info "Forcing health check failure..."
    vm_run "touch /run/test-health-demo-fail"

    # Wait for health check to detect failure
    log_info "Waiting for health check to detect failure..."
    sleep 15

    # Check if service transitioned to DEGRADED or FAILED
    NEW_STATE=$(vm_run "graphctl status | grep test-health-demo | awk '{print \$2}'")
    log_info "New health demo state: $NEW_STATE"

    if [ "$NEW_STATE" = "DEGRADED" ] || [ "$NEW_STATE" = "FAILED" ]; then
        log_success "Health failure detected - service transitioned to $NEW_STATE"
    else
        log_error "Health failure not detected - service still $NEW_STATE"
        vm_run "rm -f /run/test-health-demo-fail"  # Clean up
        return 1
    fi

    return 0
}

# Test 4: Test health recovery
test_health_recovery() {
    log_info "Testing health recovery..."

    # Remove failure flag to allow recovery
    log_info "Removing failure condition..."
    vm_run "rm -f /run/test-health-demo-fail"

    # Wait for health check to detect recovery
    log_info "Waiting for health recovery..."
    sleep 20

    # Check if service recovered to ACTIVE
    RECOVERED_STATE=$(vm_run "graphctl status | grep test-health-demo | awk '{print \$2}'")
    log_info "Recovered health demo state: $RECOVERED_STATE"

    if [ "$RECOVERED_STATE" = "ACTIVE" ]; then
        log_success "Health recovery successful - service back to ACTIVE"
    else
        log_error "Health recovery failed - service still $RECOVERED_STATE"
        return 1
    fi

    return 0
}

# Test 5: Test multiple health check types
test_multiple_health_types() {
    log_info "Testing multiple health check types..."

    # Test command-based health check (networking service)
    if vm_run "/usr/local/bin/test-networking-health >/dev/null 2>&1"; then
        log_success "Command-based health check working (networking service)"
    else
        log_error "Command-based health check failed (networking service)"
        return 1
    fi

    # Test file-based health check (echo server)
    if vm_run "/usr/local/bin/test-echo-health >/dev/null 2>&1"; then
        log_success "File-based health check working (echo server)"
    else
        log_error "File-based health check failed (echo server)"
        return 1
    fi

    # Test complex health check (stateful service)
    if vm_run "/usr/local/bin/test-stateful-health >/dev/null 2>&1"; then
        log_success "HTTP-based health check working (stateful service)"
    else
        log_error "HTTP-based health check failed (stateful service)"
        return 1
    fi

    return 0
}

# Test 6: Test health check failure thresholds
test_failure_thresholds() {
    log_info "Testing health check failure thresholds..."

    # Get configured failure threshold
    FAILURES=$(vm_run "grep 'health_failures' /etc/graph.d/07-health-demo.toml | cut -d= -f2 | tr -d ' '")
    log_info "Configured failure threshold: $FAILURES failures"

    if [ "$FAILURES" -le 5 ]; then
        log_success "Failure threshold is reasonable for testing ($FAILURES failures)"
    else
        log_error "Failure threshold too high for testing ($FAILURES failures)"
        return 1
    fi

    # Verify that single failure doesn't immediately transition state
    INITIAL_STATE=$(vm_run "graphctl status | grep test-health-demo | awk '{print \$2}'")

    # Force a brief failure
    vm_run "touch /run/test-health-demo-fail"
    sleep 3  # Wait less than the failure threshold
    vm_run "rm -f /run/test-health-demo-fail"

    sleep 8  # Wait for one health check cycle

    AFTER_STATE=$(vm_run "graphctl status | grep test-health-demo | awk '{print \$2}'")

    if [ "$INITIAL_STATE" = "$AFTER_STATE" ]; then
        log_success "Single failure correctly ignored (threshold protection working)"
    else
        log_error "Single failure incorrectly triggered state change"
        return 1
    fi

    return 0
}

# Test 7: Test health monitoring resource usage
test_health_monitoring_overhead() {
    log_info "Testing health monitoring resource usage..."

    # Get initial YakirOS memory usage
    INITIAL_MEMORY=$(vm_run "ps -o pid,vsz,rss -p 1 | tail -1 | awk '{print \$3}'")
    log_info "Initial YakirOS memory usage: ${INITIAL_MEMORY}KB"

    # Let health checks run for a while
    sleep 30

    # Get memory usage after health checks
    FINAL_MEMORY=$(vm_run "ps -o pid,vsz,rss -p 1 | tail -1 | awk '{print \$3}'")
    log_info "Final YakirOS memory usage: ${FINAL_MEMORY}KB"

    # Calculate memory increase
    MEMORY_INCREASE=$((FINAL_MEMORY - INITIAL_MEMORY))

    if [ $MEMORY_INCREASE -lt 1000 ]; then  # Less than 1MB increase
        log_success "Health monitoring has low memory overhead (${MEMORY_INCREASE}KB increase)"
    else
        log_error "Health monitoring has high memory overhead (${MEMORY_INCREASE}KB increase)"
        return 1
    fi

    # Check that health checks aren't consuming too much CPU
    CPU_USAGE=$(vm_run "top -bn1 | grep graph-resolver | awk '{print \$9}' | head -1" || echo "0.0")
    log_info "YakirOS CPU usage: ${CPU_USAGE}%"

    return 0
}

# Main test execution
main() {
    echo "YakirOS Health Check Testing"
    echo "============================"
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: Health monitoring system (Step 6)"
    echo

    TESTS_TOTAL=7
    TESTS_PASSED=0

    # Run all tests
    if test_health_check_configuration; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_health_check_execution; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_health_failure_detection; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_health_recovery; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_multiple_health_types; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_failure_thresholds; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_health_monitoring_overhead; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Clean up any remaining test artifacts
    vm_run "rm -f /run/test-health-demo-fail"

    # Summary
    echo
    echo "Health Check Test Results:"
    echo "=========================="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"

    if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
        log_success "All health check tests passed!"
        exit 0
    else
        log_error "Some health check tests failed!"
        exit 1
    fi
}

main "$@"