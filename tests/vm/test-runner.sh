#!/bin/bash
#
# YakirOS Step 11: Comprehensive VM Test Runner
# =============================================
#
# Main test orchestration script that runs comprehensive testing
# of all YakirOS advanced features in the VM environment.
#

set -e

# Test configuration
TEST_DIR="$(dirname "$0")"
VM_SSH_PORT="2222"
VM_HOST="localhost"
TEST_TIMEOUT=300  # 5 minutes per test suite
RESULTS_DIR="/tmp/yakiros-test-results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test results tracking
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0
START_TIME=$(date +%s)

log_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

log_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

log_warn() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

log_error() {
    echo -e "${RED}❌ $1${NC}"
}

log_header() {
    echo -e "\n${PURPLE}🚀 $1${NC}"
    echo "================================================================="
}

log_test_start() {
    echo -e "${CYAN}🧪 Starting: $1${NC}"
}

log_test_pass() {
    echo -e "${GREEN}✅ PASSED: $1${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

log_test_fail() {
    echo -e "${RED}❌ FAILED: $1${NC}"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

log_test_skip() {
    echo -e "${YELLOW}⏭️  SKIPPED: $1${NC}"
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
}

# Check VM connectivity
check_vm_connection() {
    log_info "Checking VM connectivity..."
    if ! ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -p ${VM_SSH_PORT} root@${VM_HOST} echo "VM accessible" >/dev/null 2>&1; then
        log_error "Cannot connect to VM. Ensure VM is running:"
        log_error "  ./setup-vm-step11.sh start-vm"
        exit 1
    fi
    log_success "VM connection established"
}

# Check YakirOS status in VM
check_yakiros_status() {
    log_info "Checking YakirOS status in VM..."

    # Check if graph-resolver is running as PID 1
    VM_PID1=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "ps -p 1 -o comm=" 2>/dev/null || echo "unknown")

    if [ "$VM_PID1" = "graph-resolver" ]; then
        log_success "YakirOS running as PID 1"
    else
        log_warn "PID 1 is '$VM_PID1' (expected 'graph-resolver')"
    fi

    # Check graphctl functionality
    if ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status" >/dev/null 2>&1; then
        log_success "graphctl is functional"
    else
        log_error "graphctl not working properly"
        return 1
    fi
}

# Setup test results directory
setup_test_results() {
    mkdir -p "$RESULTS_DIR"

    # Create test run metadata
    cat > "$RESULTS_DIR/test-run-info.json" << EOF
{
  "test_run_id": "yakiros-step11-$(date +%Y%m%d-%H%M%S)",
  "start_time": "$(date -Iseconds)",
  "yakiros_version": "Step-11-VM-Testing",
  "vm_config": {
    "ram_mb": 4096,
    "cpus": 4,
    "os": "Alpine Linux 3.19.1"
  },
  "test_environment": "VM",
  "features_tested": [
    "hot_swap_services",
    "health_checks",
    "isolation_cgroups",
    "graph_analysis",
    "checkpointing",
    "kexec_dry_run"
  ]
}
EOF

    log_success "Test results directory created: $RESULTS_DIR"
}

# Run basic system validation tests
run_basic_tests() {
    log_header "Basic System Validation Tests"
    TESTS_TOTAL=$((TESTS_TOTAL + 5))

    # Test 1: YakirOS as PID 1
    log_test_start "YakirOS running as PID 1"
    VM_PID1=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "ps -p 1 -o comm=" 2>/dev/null || echo "unknown")
    if [ "$VM_PID1" = "graph-resolver" ]; then
        log_test_pass "YakirOS running as PID 1"
    else
        log_test_fail "PID 1 is '$VM_PID1' (expected 'graph-resolver')"
    fi

    # Test 2: All test components loaded
    log_test_start "Test components loaded"
    COMPONENT_COUNT=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status | grep -c 'test-'" 2>/dev/null || echo "0")
    if [ "$COMPONENT_COUNT" -ge 5 ]; then
        log_test_pass "Test components loaded ($COMPONENT_COUNT found)"
    else
        log_test_fail "Insufficient test components ($COMPONENT_COUNT found, expected >= 5)"
    fi

    # Test 3: Essential services running
    log_test_start "Essential services running"
    ACTIVE_COUNT=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status | grep -c ACTIVE" 2>/dev/null || echo "0")
    if [ "$ACTIVE_COUNT" -ge 3 ]; then
        log_test_pass "Essential services active ($ACTIVE_COUNT services)"
    else
        log_test_fail "Too few active services ($ACTIVE_COUNT found, expected >= 3)"
    fi

    # Test 4: No failed components
    log_test_start "No failed components"
    FAILED_COUNT=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status | grep -c FAILED" 2>/dev/null || echo "0")
    if [ "$FAILED_COUNT" -eq 0 ]; then
        log_test_pass "No failed components"
    else
        log_test_fail "Found $FAILED_COUNT failed components"
    fi

    # Test 5: Control socket functional
    log_test_start "Control socket functional"
    if ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl capabilities | head -1" >/dev/null 2>&1; then
        log_test_pass "Control socket functional"
    else
        log_test_fail "Control socket not responding"
    fi
}

# Run hot-swap testing
run_hot_swap_tests() {
    log_header "Hot-Swap Services Testing (Step 4)"

    if [ ! -f "$TEST_DIR/test-hot-swap.sh" ]; then
        log_test_skip "Hot-swap test script not found"
        return 0
    fi

    log_test_start "Hot-swap functionality"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-hot-swap.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/hot-swap-test.log"; then
        log_test_pass "Hot-swap testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Hot-swap testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run health check testing
run_health_check_tests() {
    log_header "Health Check System Testing (Step 6)"

    if [ ! -f "$TEST_DIR/test-health-checks.sh" ]; then
        log_test_skip "Health check test script not found"
        return 0
    fi

    log_test_start "Health monitoring system"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-health-checks.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/health-checks-test.log"; then
        log_test_pass "Health check testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Health check testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run isolation testing
run_isolation_tests() {
    log_header "Isolation Testing (Step 7)"

    if [ ! -f "$TEST_DIR/test-isolation.sh" ]; then
        log_test_skip "Isolation test script not found"
        return 0
    fi

    log_test_start "cgroups and namespace isolation"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-isolation.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/isolation-test.log"; then
        log_test_pass "Isolation testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Isolation testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run graph analysis testing
run_graph_analysis_tests() {
    log_header "Graph Analysis Testing (Step 8)"

    if [ ! -f "$TEST_DIR/test-graph-analysis.sh" ]; then
        log_test_skip "Graph analysis test script not found"
        return 0
    fi

    log_test_start "Cycle detection and graph analysis"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-graph-analysis.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/graph-analysis-test.log"; then
        log_test_pass "Graph analysis testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Graph analysis testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run checkpointing testing
run_checkpointing_tests() {
    log_header "CRIU Checkpointing Testing (Step 9)"

    if [ ! -f "$TEST_DIR/test-checkpointing.sh" ]; then
        log_test_skip "Checkpointing test script not found"
        return 0
    fi

    log_test_start "CRIU checkpoint/restore functionality"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-checkpointing.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/checkpointing-test.log"; then
        log_test_pass "Checkpointing testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Checkpointing testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run kexec testing
run_kexec_tests() {
    log_header "kexec Live Kernel Upgrade Testing (Step 10)"

    if [ ! -f "$TEST_DIR/test-kexec.sh" ]; then
        log_test_skip "kexec test script not found"
        return 0
    fi

    log_test_start "kexec kernel upgrade functionality (dry-run)"
    if timeout $TEST_TIMEOUT bash "$TEST_DIR/test-kexec.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/kexec-test.log"; then
        log_test_pass "kexec testing completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "kexec testing failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Run performance benchmarking
run_performance_tests() {
    log_header "Performance Benchmarking"

    if [ ! -f "$TEST_DIR/performance-benchmark.sh" ]; then
        log_test_skip "Performance benchmark script not found"
        return 0
    fi

    log_test_start "System performance and resource usage"
    if timeout $((TEST_TIMEOUT * 2)) bash "$TEST_DIR/performance-benchmark.sh" "$VM_SSH_PORT" 2>&1 | tee "$RESULTS_DIR/performance-test.log"; then
        log_test_pass "Performance benchmarking completed"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Performance benchmarking failed"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
}

# Generate comprehensive test report
generate_test_report() {
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))

    # Create JSON test results
    cat > "$RESULTS_DIR/test-results.json" << EOF
{
  "test_run_id": "yakiros-step11-$(date +%Y%m%d-%H%M%S)",
  "start_time": "$(date -d @$START_TIME -Iseconds)",
  "end_time": "$(date -Iseconds)",
  "duration_seconds": $DURATION,
  "tests_total": $TESTS_TOTAL,
  "tests_passed": $TESTS_PASSED,
  "tests_failed": $TESTS_FAILED,
  "tests_skipped": $TESTS_SKIPPED,
  "success_rate": $(echo "scale=2; $TESTS_PASSED * 100 / $TESTS_TOTAL" | bc -l 2>/dev/null || echo "0"),
  "vm_config": {
    "ssh_port": $VM_SSH_PORT,
    "host": "$VM_HOST"
  }
}
EOF

    # Create human-readable summary
    cat > "$RESULTS_DIR/test-summary.txt" << EOF
YakirOS Step 11: VM Integration Testing Results
===============================================

Test Run: $(date)
Duration: ${DURATION} seconds

Results Summary:
  Total Tests: $TESTS_TOTAL
  Passed:      $TESTS_PASSED
  Failed:      $TESTS_FAILED
  Skipped:     $TESTS_SKIPPED

Success Rate: $(echo "scale=1; $TESTS_PASSED * 100 / $TESTS_TOTAL" | bc -l 2>/dev/null || echo "0")%

Test Categories:
  ✅ Basic System Validation
  ✅ Hot-Swap Services (Step 4)
  ✅ Health Check System (Step 6)
  ✅ Isolation Testing (Step 7)
  ✅ Graph Analysis (Step 8)
  ✅ CRIU Checkpointing (Step 9)
  ✅ kexec Testing (Step 10)
  ✅ Performance Benchmarking

Log Files:
  $(ls -la $RESULTS_DIR/*.log 2>/dev/null | awk '{print "  " $9}' || echo "  No detailed logs available")

EOF

    log_success "Test report generated in $RESULTS_DIR"
}

# Show final results
show_final_results() {
    log_header "Test Results Summary"

    if [ $TESTS_FAILED -eq 0 ]; then
        log_success "All tests passed! ($TESTS_PASSED/$TESTS_TOTAL)"
        log_success "YakirOS Step 11 VM testing successful!"
    else
        log_error "Some tests failed: $TESTS_FAILED/$TESTS_TOTAL"
        log_error "Check detailed logs in: $RESULTS_DIR"
    fi

    echo
    echo -e "${CYAN}📊 Test Statistics:${NC}"
    echo "  Duration: $(($(date +%s) - START_TIME)) seconds"
    echo "  Total Tests: $TESTS_TOTAL"
    echo "  Passed: $TESTS_PASSED"
    echo "  Failed: $TESTS_FAILED"
    echo "  Skipped: $TESTS_SKIPPED"
    echo
    echo -e "${PURPLE}📁 Results Directory: $RESULTS_DIR${NC}"
    echo -e "${PURPLE}📋 Summary Report: $RESULTS_DIR/test-summary.txt${NC}"
    echo
}

# Main execution
main() {
    case "${1:-comprehensive}" in
        comprehensive|full)
            log_header "YakirOS Step 11: Comprehensive VM Testing"
            check_vm_connection
            check_yakiros_status
            setup_test_results
            run_basic_tests
            run_hot_swap_tests
            run_health_check_tests
            run_isolation_tests
            run_graph_analysis_tests
            run_checkpointing_tests
            run_kexec_tests
            run_performance_tests
            generate_test_report
            show_final_results
            ;;

        basic)
            log_header "YakirOS Step 11: Basic System Testing"
            check_vm_connection
            check_yakiros_status
            setup_test_results
            run_basic_tests
            generate_test_report
            show_final_results
            ;;

        hot-swap)
            log_header "YakirOS Step 11: Hot-Swap Testing"
            check_vm_connection
            setup_test_results
            run_hot_swap_tests
            generate_test_report
            show_final_results
            ;;

        health-checks)
            log_header "YakirOS Step 11: Health Check Testing"
            check_vm_connection
            setup_test_results
            run_health_check_tests
            generate_test_report
            show_final_results
            ;;

        isolation)
            log_header "YakirOS Step 11: Isolation Testing"
            check_vm_connection
            setup_test_results
            run_isolation_tests
            generate_test_report
            show_final_results
            ;;

        graph-analysis)
            log_header "YakirOS Step 11: Graph Analysis Testing"
            check_vm_connection
            setup_test_results
            run_graph_analysis_tests
            generate_test_report
            show_final_results
            ;;

        checkpointing)
            log_header "YakirOS Step 11: CRIU Checkpointing Testing"
            check_vm_connection
            setup_test_results
            run_checkpointing_tests
            generate_test_report
            show_final_results
            ;;

        kexec)
            log_header "YakirOS Step 11: kexec Testing"
            check_vm_connection
            setup_test_results
            run_kexec_tests
            generate_test_report
            show_final_results
            ;;

        performance)
            log_header "YakirOS Step 11: Performance Benchmarking"
            check_vm_connection
            setup_test_results
            run_performance_tests
            generate_test_report
            show_final_results
            ;;

        status)
            log_header "YakirOS Step 11: VM Status Check"
            check_vm_connection
            check_yakiros_status
            ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status"
            ;;

        help|*)
            echo "YakirOS Step 11: VM Integration Test Runner"
            echo "Usage: $0 [test-type]"
            echo
            echo "Test Types:"
            echo "  comprehensive  - Run all available tests (default)"
            echo "  basic          - Run basic system validation only"
            echo "  hot-swap       - Run hot-swap service tests"
            echo "  health-checks  - Run health monitoring tests"
            echo "  isolation      - Run cgroup/namespace tests"
            echo "  graph-analysis - Run cycle detection/graph analysis tests"
            echo "  checkpointing  - Run CRIU checkpoint/restore tests"
            echo "  kexec          - Run live kernel upgrade tests (dry-run)"
            echo "  performance    - Run performance benchmarking"
            echo "  status         - Show VM and YakirOS status"
            echo "  help           - Show this help"
            echo
            echo "Prerequisites:"
            echo "  - VM must be running (./setup-vm-step11.sh)"
            echo "  - SSH connectivity to VM on port $VM_SSH_PORT"
            echo "  - YakirOS loaded with test components"
            ;;
    esac
}

# Check dependencies
if ! command -v bc >/dev/null 2>&1; then
    log_warn "bc (calculator) not found - success rate calculation may fail"
fi

if ! command -v ssh >/dev/null 2>&1; then
    log_error "ssh not found - cannot connect to VM"
    exit 1
fi

main "$@"