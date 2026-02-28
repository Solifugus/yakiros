#!/bin/bash
#
# YakirOS Isolation Testing Script
# Tests cgroups and namespace isolation features
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
    echo -e "${BLUE}[ISOLATION] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[ISOLATION] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[ISOLATION] ❌ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Verify cgroup v2 availability
test_cgroup_availability() {
    log_info "Testing cgroup v2 availability..."

    # Check if cgroup v2 is mounted
    if vm_run "mountpoint -q /sys/fs/cgroup 2>/dev/null"; then
        log_success "cgroup v2 filesystem is mounted"
    else
        log_error "cgroup v2 filesystem not mounted"
        return 1
    fi

    # Check if YakirOS cgroup directory exists
    if vm_run "test -d /sys/fs/cgroup/graph"; then
        log_success "YakirOS cgroup directory exists"
    else
        log_error "YakirOS cgroup directory missing"
        return 1
    fi

    # Check available controllers
    CONTROLLERS=$(vm_run "cat /sys/fs/cgroup/cgroup.controllers 2>/dev/null" || echo "")
    log_info "Available cgroup controllers: $CONTROLLERS"

    if echo "$CONTROLLERS" | grep -q "memory"; then
        log_success "Memory controller available"
    else
        log_error "Memory controller not available"
        return 1
    fi

    return 0
}

# Test 2: Verify isolated service cgroup configuration
test_isolated_service_cgroups() {
    log_info "Testing isolated service cgroup configuration..."

    # Check if isolated service is running
    if ! vm_run "graphctl status | grep test-isolated-service | grep -q ACTIVE"; then
        log_error "Isolated service is not active"
        return 1
    fi

    # Check if service has its own cgroup
    if vm_run "test -d /sys/fs/cgroup/graph/test-isolated"; then
        log_success "Isolated service has dedicated cgroup"
    else
        log_error "Isolated service cgroup not found"
        return 1
    fi

    # Check memory limits
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/memory.max"; then
        MEMORY_LIMIT=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/memory.max")
        log_success "Memory limit configured: $MEMORY_LIMIT"
    else
        log_error "Memory limit not configured"
        return 1
    fi

    # Check CPU limits
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/cpu.max"; then
        CPU_LIMIT=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/cpu.max")
        log_success "CPU limit configured: $CPU_LIMIT"
    else
        log_error "CPU limit not configured"
        return 1
    fi

    return 0
}

# Test 3: Test memory limit enforcement
test_memory_limit_enforcement() {
    log_info "Testing memory limit enforcement..."

    # Get current memory usage
    MEMORY_CURRENT=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/memory.current 2>/dev/null" || echo "0")
    MEMORY_MAX=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/memory.max 2>/dev/null" || echo "max")

    log_info "Current memory usage: ${MEMORY_CURRENT} bytes"
    log_info "Memory limit: ${MEMORY_MAX}"

    # Check if memory is being tracked
    if [ "$MEMORY_CURRENT" -gt 0 ]; then
        log_success "Memory usage is being tracked"
    else
        log_error "Memory usage not being tracked properly"
        return 1
    fi

    # Check memory events for any OOM kills
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/memory.events"; then
        OOM_EVENTS=$(vm_run "grep oom_kill /sys/fs/cgroup/graph/test-isolated/memory.events | awk '{print \$2}'" || echo "0")
        if [ "$OOM_EVENTS" -eq 0 ]; then
            log_success "No OOM kills detected"
        else
            log_info "OOM kills detected: $OOM_EVENTS (may be expected for testing)"
        fi
    fi

    return 0
}

# Test 4: Test namespace isolation
test_namespace_isolation() {
    log_info "Testing namespace isolation..."

    # Get isolated service PID
    ISOLATED_PID=$(vm_run "pgrep -f test-isolated-service" || echo "")

    if [ -z "$ISOLATED_PID" ]; then
        log_error "Cannot find isolated service PID"
        return 1
    fi

    log_info "Isolated service PID: $ISOLATED_PID"

    # Check PID namespace isolation
    HOST_PID_NS=$(vm_run "readlink /proc/1/ns/pid")
    ISOLATED_PID_NS=$(vm_run "readlink /proc/$ISOLATED_PID/ns/pid" 2>/dev/null || echo "")

    if [ "$HOST_PID_NS" != "$ISOLATED_PID_NS" ] && [ -n "$ISOLATED_PID_NS" ]; then
        log_success "PID namespace isolation active"
        log_info "Host PID NS: $HOST_PID_NS"
        log_info "Isolated PID NS: $ISOLATED_PID_NS"
    else
        log_error "PID namespace isolation not active"
        return 1
    fi

    # Check UTS namespace (hostname)
    HOST_HOSTNAME=$(vm_run "hostname")
    ISOLATED_HOSTNAME=$(vm_run "nsenter -t $ISOLATED_PID -u hostname 2>/dev/null" || echo "$HOST_HOSTNAME")

    if [ "$HOST_HOSTNAME" != "$ISOLATED_HOSTNAME" ]; then
        log_success "UTS namespace isolation active (hostname: $ISOLATED_HOSTNAME)"
    else
        log_info "UTS namespace isolation not active or same hostname"
    fi

    # Check mount namespace
    HOST_MOUNT_NS=$(vm_run "readlink /proc/1/ns/mnt")
    ISOLATED_MOUNT_NS=$(vm_run "readlink /proc/$ISOLATED_PID/ns/mnt" 2>/dev/null || echo "")

    if [ "$HOST_MOUNT_NS" != "$ISOLATED_MOUNT_NS" ] && [ -n "$ISOLATED_MOUNT_NS" ]; then
        log_success "Mount namespace isolation active"
    else
        log_info "Mount namespace isolation not active"
    fi

    return 0
}

# Test 5: Test process count limits
test_process_limits() {
    log_info "Testing process count limits..."

    # Check if PID limits are configured
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/pids.max"; then
        PID_LIMIT=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/pids.max")
        PID_CURRENT=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/pids.current")

        log_success "Process limits configured: ${PID_CURRENT}/${PID_LIMIT} processes"

        # Check if we're within reasonable limits
        if [ "$PID_CURRENT" -le "$PID_LIMIT" ]; then
            log_success "Process count within limits"
        else
            log_error "Process count exceeds limits"
            return 1
        fi
    else
        log_info "PID limits not configured (may be optional)"
    fi

    return 0
}

# Test 6: Test cgroup cleanup
test_cgroup_cleanup() {
    log_info "Testing cgroup cleanup behavior..."

    # Record current cgroup directories
    BEFORE_DIRS=$(vm_run "ls -1 /sys/fs/cgroup/graph/ | wc -l" 2>/dev/null || echo "0")
    log_info "Cgroup directories before: $BEFORE_DIRS"

    # Start a temporary test service (simulated)
    log_info "Creating temporary cgroup for testing..."
    vm_run "mkdir -p /sys/fs/cgroup/graph/test-cleanup 2>/dev/null || true"
    vm_run "echo \$\$ > /sys/fs/cgroup/graph/test-cleanup/cgroup.procs 2>/dev/null || true"

    # Verify temporary cgroup exists
    if vm_run "test -d /sys/fs/cgroup/graph/test-cleanup"; then
        log_success "Temporary cgroup created"

        # Clean up the temporary cgroup
        vm_run "rmdir /sys/fs/cgroup/graph/test-cleanup 2>/dev/null || true"

        # Verify cleanup
        if ! vm_run "test -d /sys/fs/cgroup/graph/test-cleanup"; then
            log_success "Cgroup cleanup working"
        else
            log_error "Cgroup cleanup failed"
            return 1
        fi
    else
        log_info "Could not create temporary cgroup (permissions or unsupported)"
    fi

    return 0
}

# Test 7: Test resource monitoring
test_resource_monitoring() {
    log_info "Testing resource monitoring..."

    # Check CPU usage statistics
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/cpu.stat"; then
        CPU_STATS=$(vm_run "cat /sys/fs/cgroup/graph/test-isolated/cpu.stat | head -3")
        log_success "CPU statistics available"
        log_info "CPU stats preview: $(echo "$CPU_STATS" | tr '\n' ';')"
    else
        log_error "CPU statistics not available"
        return 1
    fi

    # Check memory statistics
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/memory.stat"; then
        MEMORY_STATS=$(vm_run "grep -E '(anon|file|cache)' /sys/fs/cgroup/graph/test-isolated/memory.stat | head -3")
        log_success "Memory statistics available"
        log_info "Memory stats preview: $(echo "$MEMORY_STATS" | tr '\n' ';')"
    else
        log_error "Memory statistics not available"
        return 1
    fi

    # Check I/O statistics if available
    if vm_run "test -f /sys/fs/cgroup/graph/test-isolated/io.stat"; then
        log_success "I/O statistics available"
    else
        log_info "I/O statistics not available (may not be configured)"
    fi

    return 0
}

# Main test execution
main() {
    echo "YakirOS Isolation Testing"
    echo "========================="
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: cgroups and namespace isolation (Step 7)"
    echo

    TESTS_TOTAL=7
    TESTS_PASSED=0

    # Run all tests
    if test_cgroup_availability; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_isolated_service_cgroups; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_memory_limit_enforcement; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_namespace_isolation; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_process_limits; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_cgroup_cleanup; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_resource_monitoring; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Summary
    echo
    echo "Isolation Test Results:"
    echo "======================="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"

    if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
        log_success "All isolation tests passed!"
        exit 0
    else
        log_error "Some isolation tests failed!"
        exit 1
    fi
}

main "$@"