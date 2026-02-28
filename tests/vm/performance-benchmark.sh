#!/bin/bash
#
# YakirOS Performance Benchmarking Script
# Measures resource usage and performance characteristics
#

set -e

VM_SSH_PORT="${1:-2222}"
VM_HOST="localhost"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[PERF] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[PERF] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[PERF] ❌ $1${NC}"
}

log_metric() {
    echo -e "${CYAN}[PERF] 📊 $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: YakirOS memory usage baseline
test_yakiros_memory_usage() {
    log_info "Measuring YakirOS memory usage..."

    # Get graph-resolver memory usage
    YAKIROS_VSZ=$(vm_run "ps -o pid,vsz,rss -p 1 | tail -1 | awk '{print \$2}'")
    YAKIROS_RSS=$(vm_run "ps -o pid,vsz,rss -p 1 | tail -1 | awk '{print \$3}'")

    log_metric "YakirOS Virtual Memory: ${YAKIROS_VSZ}KB"
    log_metric "YakirOS Resident Memory: ${YAKIROS_RSS}KB"

    # Performance targets
    if [ $YAKIROS_RSS -lt 51200 ]; then  # Less than 50MB RSS
        log_success "YakirOS memory usage excellent (${YAKIROS_RSS}KB < 50MB)"
    elif [ $YAKIROS_RSS -lt 102400 ]; then  # Less than 100MB RSS
        log_success "YakirOS memory usage good (${YAKIROS_RSS}KB < 100MB)"
    else
        log_error "YakirOS memory usage high (${YAKIROS_RSS}KB >= 100MB)"
        return 1
    fi

    return 0
}

# Test 2: Component startup time measurement
test_component_startup_time() {
    log_info "Measuring component startup times..."

    # Restart a test service and measure startup time
    log_info "Restarting test-health-demo for timing measurement..."

    START_TIME=$(date +%s%N)
    vm_run "pkill -f test-health-demo || true"

    # Wait for service to restart and become ready
    local attempts=0
    while [ $attempts -lt 60 ]; do  # 30 second timeout
        if vm_run "graphctl status | grep test-health-demo | grep -q ACTIVE"; then
            END_TIME=$(date +%s%N)
            STARTUP_TIME_NS=$((END_TIME - START_TIME))
            STARTUP_TIME_MS=$((STARTUP_TIME_NS / 1000000))
            break
        fi
        sleep 0.5
        attempts=$((attempts + 1))
    done

    if [ $attempts -lt 60 ]; then
        log_metric "Component startup time: ${STARTUP_TIME_MS}ms"

        if [ $STARTUP_TIME_MS -lt 2000 ]; then  # Less than 2 seconds
            log_success "Component startup time excellent (${STARTUP_TIME_MS}ms)"
        elif [ $STARTUP_TIME_MS -lt 5000 ]; then  # Less than 5 seconds
            log_success "Component startup time good (${STARTUP_TIME_MS}ms)"
        else
            log_error "Component startup time slow (${STARTUP_TIME_MS}ms >= 5s)"
            return 1
        fi
    else
        log_error "Component failed to start within timeout"
        return 1
    fi

    return 0
}

# Test 3: Graph resolution performance
test_graph_resolution_performance() {
    log_info "Measuring graph resolution performance..."

    # Measure time for graph resolution
    START_TIME=$(date +%s%N)
    vm_run "graphctl resolve >/dev/null 2>&1"
    END_TIME=$(date +%s%N)

    RESOLUTION_TIME_NS=$((END_TIME - START_TIME))
    RESOLUTION_TIME_MS=$((RESOLUTION_TIME_NS / 1000000))

    log_metric "Graph resolution time: ${RESOLUTION_TIME_MS}ms"

    if [ $RESOLUTION_TIME_MS -lt 1000 ]; then  # Less than 1 second
        log_success "Graph resolution time excellent (${RESOLUTION_TIME_MS}ms)"
    elif [ $RESOLUTION_TIME_MS -lt 3000 ]; then  # Less than 3 seconds
        log_success "Graph resolution time good (${RESOLUTION_TIME_MS}ms)"
    else
        log_error "Graph resolution time slow (${RESOLUTION_TIME_MS}ms >= 3s)"
        return 1
    fi

    return 0
}

# Test 4: Hot-swap upgrade performance
test_hotswap_performance() {
    log_info "Measuring hot-swap upgrade performance..."

    # Ensure echo server is running
    if ! vm_run "graphctl status | grep test-echo-server | grep -q ACTIVE"; then
        log_error "Echo server not running for hot-swap performance test"
        return 1
    fi

    # Measure hot-swap upgrade time
    START_TIME=$(date +%s%N)
    vm_run "graphctl upgrade test-echo-server >/dev/null 2>&1"
    END_TIME=$(date +%s%N)

    UPGRADE_TIME_NS=$((END_TIME - START_TIME))
    UPGRADE_TIME_MS=$((UPGRADE_TIME_NS / 1000000))

    log_metric "Hot-swap upgrade time: ${UPGRADE_TIME_MS}ms"

    if [ $UPGRADE_TIME_MS -lt 5000 ]; then  # Less than 5 seconds
        log_success "Hot-swap upgrade time excellent (${UPGRADE_TIME_MS}ms)"
    elif [ $UPGRADE_TIME_MS -lt 10000 ]; then  # Less than 10 seconds
        log_success "Hot-swap upgrade time good (${UPGRADE_TIME_MS}ms)"
    else
        log_error "Hot-swap upgrade time slow (${UPGRADE_TIME_MS}ms >= 10s)"
        return 1
    fi

    return 0
}

# Test 5: Health check overhead measurement
test_health_check_overhead() {
    log_info "Measuring health check system overhead..."

    # Get CPU usage before health check measurement
    CPU_BEFORE=$(vm_run "top -bn2 -d1 | grep graph-resolver | tail -1 | awk '{print \$9}'" | cut -d'%' -f1 || echo "0.0")

    # Let health checks run for a measurement period
    log_info "Monitoring health check overhead for 30 seconds..."
    sleep 30

    # Get CPU usage after health check period
    CPU_AFTER=$(vm_run "top -bn2 -d1 | grep graph-resolver | tail -1 | awk '{print \$9}'" | cut -d'%' -f1 || echo "0.0")

    log_metric "YakirOS CPU usage: ${CPU_AFTER}%"

    # Convert to integer for comparison
    CPU_USAGE=$(echo "$CPU_AFTER" | cut -d'.' -f1)

    if [ "$CPU_USAGE" -lt 2 ]; then  # Less than 2% CPU
        log_success "Health check overhead low (${CPU_AFTER}% CPU)"
    elif [ "$CPU_USAGE" -lt 5 ]; then  # Less than 5% CPU
        log_success "Health check overhead acceptable (${CPU_AFTER}% CPU)"
    else
        log_error "Health check overhead high (${CPU_AFTER}% CPU)"
        return 1
    fi

    return 0
}

# Test 6: System responsiveness under load
test_system_responsiveness() {
    log_info "Testing system responsiveness under load..."

    # Generate background load
    log_info "Generating background load..."
    vm_run 'for i in {1..4}; do (while true; do echo "load" >/dev/null; done) & done; echo $! > /tmp/load_pids'

    sleep 5  # Let load build up

    # Measure graphctl response time under load
    START_TIME=$(date +%s%N)
    vm_run "graphctl status >/dev/null 2>&1"
    END_TIME=$(date +%s%N)

    RESPONSE_TIME_NS=$((END_TIME - START_TIME))
    RESPONSE_TIME_MS=$((RESPONSE_TIME_NS / 1000000))

    # Clean up background load
    vm_run "killall -9 sh 2>/dev/null || true"

    log_metric "Response time under load: ${RESPONSE_TIME_MS}ms"

    if [ $RESPONSE_TIME_MS -lt 2000 ]; then  # Less than 2 seconds
        log_success "System responsive under load (${RESPONSE_TIME_MS}ms)"
    elif [ $RESPONSE_TIME_MS -lt 5000 ]; then  # Less than 5 seconds
        log_success "System acceptable under load (${RESPONSE_TIME_MS}ms)"
    else
        log_error "System slow under load (${RESPONSE_TIME_MS}ms >= 5s)"
        return 1
    fi

    return 0
}

# Test 7: Memory stability over time
test_memory_stability() {
    log_info "Testing memory stability over time..."

    # Get initial memory usage
    MEMORY_INITIAL=$(vm_run "ps -o rss -p 1 | tail -1")
    log_metric "Initial YakirOS memory: ${MEMORY_INITIAL}KB"

    # Perform various operations to stress memory
    log_info "Stressing system with operations..."
    for i in {1..10}; do
        vm_run "graphctl status >/dev/null 2>&1"
        vm_run "graphctl capabilities >/dev/null 2>&1"
        vm_run "graphctl analyze >/dev/null 2>&1"
        sleep 1
    done

    # Get final memory usage
    MEMORY_FINAL=$(vm_run "ps -o rss -p 1 | tail -1")
    log_metric "Final YakirOS memory: ${MEMORY_FINAL}KB"

    MEMORY_GROWTH=$((MEMORY_FINAL - MEMORY_INITIAL))
    log_metric "Memory growth: ${MEMORY_GROWTH}KB"

    if [ $MEMORY_GROWTH -lt 1000 ]; then  # Less than 1MB growth
        log_success "Memory stable (${MEMORY_GROWTH}KB growth)"
    elif [ $MEMORY_GROWTH -lt 5000 ]; then  # Less than 5MB growth
        log_success "Memory growth acceptable (${MEMORY_GROWTH}KB growth)"
    else
        log_error "Memory growth concerning (${MEMORY_GROWTH}KB growth)"
        return 1
    fi

    return 0
}

# Test 8: I/O performance characteristics
test_io_performance() {
    log_info "Testing I/O performance characteristics..."

    # Test log writing performance
    START_TIME=$(date +%s%N)
    vm_run "for i in {1..100}; do echo 'Performance test log entry' | logger; done"
    END_TIME=$(date +%s%N)

    IO_TIME_NS=$((END_TIME - START_TIME))
    IO_TIME_MS=$((IO_TIME_NS / 1000000))

    log_metric "100 log writes time: ${IO_TIME_MS}ms"

    if [ $IO_TIME_MS -lt 1000 ]; then  # Less than 1 second for 100 writes
        log_success "I/O performance excellent (${IO_TIME_MS}ms for 100 writes)"
    elif [ $IO_TIME_MS -lt 3000 ]; then  # Less than 3 seconds
        log_success "I/O performance good (${IO_TIME_MS}ms for 100 writes)"
    else
        log_error "I/O performance slow (${IO_TIME_MS}ms for 100 writes)"
        return 1
    fi

    return 0
}

# Test 9: Concurrent operation performance
test_concurrent_operations() {
    log_info "Testing concurrent operation performance..."

    # Run multiple graphctl commands concurrently
    START_TIME=$(date +%s%N)
    vm_run 'graphctl status >/dev/null & graphctl capabilities >/dev/null & graphctl tree test-monitor >/dev/null & wait'
    END_TIME=$(date +%s%N)

    CONCURRENT_TIME_NS=$((END_TIME - START_TIME))
    CONCURRENT_TIME_MS=$((CONCURRENT_TIME_NS / 1000000))

    log_metric "Concurrent operations time: ${CONCURRENT_TIME_MS}ms"

    if [ $CONCURRENT_TIME_MS -lt 3000 ]; then  # Less than 3 seconds
        log_success "Concurrent operations fast (${CONCURRENT_TIME_MS}ms)"
    elif [ $CONCURRENT_TIME_MS -lt 6000 ]; then  # Less than 6 seconds
        log_success "Concurrent operations acceptable (${CONCURRENT_TIME_MS}ms)"
    else
        log_error "Concurrent operations slow (${CONCURRENT_TIME_MS}ms >= 6s)"
        return 1
    fi

    return 0
}

# Test 10: Resource usage comparison
test_resource_comparison() {
    log_info "Testing resource usage comparison..."

    # Get system resource information
    TOTAL_MEMORY=$(vm_run "free -m | awk 'NR==2{print \$2}'")
    YAKIROS_MEMORY_MB=$((YAKIROS_RSS / 1024))
    MEMORY_PERCENTAGE=$((YAKIROS_MEMORY_MB * 100 / TOTAL_MEMORY))

    log_metric "System total memory: ${TOTAL_MEMORY}MB"
    log_metric "YakirOS memory usage: ${YAKIROS_MEMORY_MB}MB (${MEMORY_PERCENTAGE}%)"

    if [ $MEMORY_PERCENTAGE -lt 2 ]; then  # Less than 2% of system memory
        log_success "Memory usage excellent (${MEMORY_PERCENTAGE}% of system memory)"
    elif [ $MEMORY_PERCENTAGE -lt 5 ]; then  # Less than 5% of system memory
        log_success "Memory usage good (${MEMORY_PERCENTAGE}% of system memory)"
    else
        log_error "Memory usage high (${MEMORY_PERCENTAGE}% of system memory)"
        return 1
    fi

    # Check process count
    YAKIROS_THREADS=$(vm_run "ps -o nlwp -p 1 | tail -1")
    log_metric "YakirOS thread count: $YAKIROS_THREADS"

    if [ $YAKIROS_THREADS -lt 10 ]; then
        log_success "Thread usage efficient ($YAKIROS_THREADS threads)"
    else
        log_error "Thread usage high ($YAKIROS_THREADS threads)"
        return 1
    fi

    return 0
}

# Generate performance report
generate_performance_report() {
    log_info "Generating performance report..."

    REPORT_FILE="/tmp/yakiros-performance-report.json"

    vm_run "cat > $REPORT_FILE << 'EOF'
{
  \"yakiros_performance_report\": {
    \"timestamp\": \"$(date -Iseconds)\",
    \"test_environment\": \"VM\",
    \"system_info\": {
      \"total_memory_mb\": $TOTAL_MEMORY,
      \"cpu_cores\": $(nproc),
      \"os\": \"$(uname -sr)\"
    },
    \"yakiros_metrics\": {
      \"memory_rss_kb\": $YAKIROS_RSS,
      \"memory_percentage\": $MEMORY_PERCENTAGE,
      \"thread_count\": $YAKIROS_THREADS,
      \"component_startup_ms\": $STARTUP_TIME_MS,
      \"graph_resolution_ms\": $RESOLUTION_TIME_MS,
      \"hotswap_upgrade_ms\": $UPGRADE_TIME_MS
    },
    \"performance_targets\": {
      \"memory_rss_target_kb\": 51200,
      \"startup_time_target_ms\": 2000,
      \"resolution_time_target_ms\": 1000,
      \"upgrade_time_target_ms\": 5000
    }
  }
}
EOF"

    log_success "Performance report generated: $REPORT_FILE"
}

# Main test execution
main() {
    echo "YakirOS Performance Benchmarking"
    echo "================================"
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: System performance and resource usage"
    echo

    TESTS_TOTAL=10
    TESTS_PASSED=0

    # Initialize variables for report generation
    YAKIROS_RSS=0
    TOTAL_MEMORY=0
    YAKIROS_THREADS=0
    STARTUP_TIME_MS=0
    RESOLUTION_TIME_MS=0
    UPGRADE_TIME_MS=0

    # Run all tests
    if test_yakiros_memory_usage; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_component_startup_time; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_graph_resolution_performance; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_hotswap_performance; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_health_check_overhead; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_system_responsiveness; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_memory_stability; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_io_performance; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_concurrent_operations; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_resource_comparison; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    generate_performance_report

    # Summary
    echo
    echo "Performance Benchmark Results:"
    echo "=============================="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"

    if [ $TESTS_PASSED -ge 8 ]; then  # Expect at least 8/10 performance tests to pass
        log_success "Performance benchmarks passed!"
        exit 0
    else
        log_error "Performance benchmarks failed!"
        exit 1
    fi
}

main "$@"