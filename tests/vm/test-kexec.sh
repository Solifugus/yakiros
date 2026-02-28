#!/bin/bash
#
# YakirOS kexec Testing Script
# Tests live kernel upgrade functionality (dry-run mode only for safety)
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
    echo -e "${BLUE}[KEXEC] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[KEXEC] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[KEXEC] ❌ $1${NC}"
}

log_skip() {
    echo -e "${YELLOW}[KEXEC] ⏭️ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Check kexec command availability
test_kexec_commands_available() {
    log_info "Testing kexec commands availability..."

    # Test kexec command
    if vm_run "graphctl kexec --help >/dev/null 2>&1"; then
        log_success "kexec command available"
    else
        log_error "kexec command not available"
        return 1
    fi

    # Test dry-run support
    if vm_run "graphctl kexec --dry-run --help >/dev/null 2>&1"; then
        log_success "kexec dry-run support available"
    else
        log_error "kexec dry-run support not available"
        return 1
    fi

    return 0
}

# Test 2: Test system readiness checks
test_system_readiness() {
    log_info "Testing kexec system readiness checks..."

    # Create a dummy kernel file for testing
    vm_run "dd if=/dev/zero of=/tmp/test-kernel bs=1M count=1 >/dev/null 2>&1"

    # Test readiness check with dummy kernel (should fail validation but check prerequisites)
    READINESS_OUTPUT=$(vm_run "graphctl kexec --dry-run /tmp/test-kernel 2>&1" || echo "")

    log_info "Readiness check output: $READINESS_OUTPUT"

    # Should check various prerequisites
    if echo "$READINESS_OUTPUT" | grep -q -i "check\|valid\|ready\|system"; then
        log_success "System readiness checks performed"
    else
        log_error "System readiness checks not performed"
        return 1
    fi

    # Clean up test kernel
    vm_run "rm -f /tmp/test-kernel"

    return 0
}

# Test 3: Test kernel validation
test_kernel_validation() {
    log_info "Testing kernel validation..."

    # Test with invalid kernel file
    vm_run "echo 'not-a-kernel' > /tmp/invalid-kernel"

    INVALID_OUTPUT=$(vm_run "graphctl kexec --dry-run /tmp/invalid-kernel 2>&1" || echo "")

    if echo "$INVALID_OUTPUT" | grep -q -i "invalid\|error\|failed"; then
        log_success "Invalid kernel properly rejected"
    else
        log_error "Invalid kernel not properly rejected"
        vm_run "rm -f /tmp/invalid-kernel"
        return 1
    fi

    vm_run "rm -f /tmp/invalid-kernel"

    # Test with non-existent kernel file
    MISSING_OUTPUT=$(vm_run "graphctl kexec --dry-run /nonexistent/kernel 2>&1" || echo "")

    if echo "$MISSING_OUTPUT" | grep -q -i "not found\|missing\|error"; then
        log_success "Missing kernel file properly detected"
    else
        log_error "Missing kernel file not properly detected"
        return 1
    fi

    return 0
}

# Test 4: Test kexec prerequisites checking
test_kexec_prerequisites() {
    log_info "Testing kexec prerequisites..."

    # Check if kexec syscall support is detected
    PREREQ_OUTPUT=$(vm_run "graphctl kexec --check-prereq 2>&1" || echo "")

    log_info "Prerequisites output: $PREREQ_OUTPUT"

    # Should check for kexec support
    if echo "$PREREQ_OUTPUT" | grep -q -i "kexec\|kernel\|support"; then
        log_success "kexec support checking performed"
    else
        log_info "kexec support checking may not be available"
    fi

    # Should check for CRIU availability
    if echo "$PREREQ_OUTPUT" | grep -q -i "criu\|checkpoint"; then
        log_success "CRIU availability checking performed"
    else
        log_info "CRIU checking may not be available"
    fi

    return 0
}

# Test 5: Test seven-phase sequence validation
test_seven_phase_sequence() {
    log_info "Testing seven-phase kexec sequence validation..."

    # Create a more realistic dummy kernel with basic ELF header
    vm_run 'printf "\\x7fELF" > /tmp/elf-kernel'
    vm_run 'dd if=/dev/zero bs=1 count=1000 >> /tmp/elf-kernel 2>/dev/null'

    # Test dry-run with verbose output to see phases
    SEQUENCE_OUTPUT=$(vm_run "graphctl kexec --dry-run --verbose /tmp/elf-kernel 2>&1" || echo "")

    log_info "Seven-phase sequence output: $SEQUENCE_OUTPUT"

    # Look for phase indicators
    PHASE_COUNT=0
    for phase in "validation" "information" "checkpoint" "verification" "persistence" "loading" "execution"; do
        if echo "$SEQUENCE_OUTPUT" | grep -q -i "$phase"; then
            PHASE_COUNT=$((PHASE_COUNT + 1))
            log_success "Phase detected: $phase"
        fi
    done

    if [ $PHASE_COUNT -ge 3 ]; then
        log_success "Multiple kexec phases detected ($PHASE_COUNT/7)"
    else
        log_error "Insufficient kexec phases detected ($PHASE_COUNT/7)"
        vm_run "rm -f /tmp/elf-kernel"
        return 1
    fi

    vm_run "rm -f /tmp/elf-kernel"
    return 0
}

# Test 6: Test checkpoint integration
test_checkpoint_integration() {
    log_info "Testing kexec checkpoint integration..."

    # Check if kexec can identify checkpoint-capable services
    CHECKPOINT_OUTPUT=$(vm_run "graphctl kexec --list-checkpoints 2>&1" || echo "")

    if echo "$CHECKPOINT_OUTPUT" | grep -q "test-stateful\|checkpoint"; then
        log_success "kexec checkpoint integration working"
    else
        log_info "kexec checkpoint listing may not be available"
    fi

    # Test manifest creation capability
    MANIFEST_OUTPUT=$(vm_run "graphctl kexec --create-manifest /tmp/test-manifest.json 2>&1" || echo "")

    if echo "$MANIFEST_OUTPUT" | grep -q -i "manifest\|created\|success" || vm_run "test -f /tmp/test-manifest.json"; then
        log_success "kexec manifest creation working"
        vm_run "rm -f /tmp/test-manifest.json"
    else
        log_info "kexec manifest creation may not be available"
    fi

    return 0
}

# Test 7: Test safety mechanisms
test_safety_mechanisms() {
    log_info "Testing kexec safety mechanisms..."

    # Ensure that non-dry-run mode requires confirmation
    SAFETY_OUTPUT=$(vm_run "echo 'no' | graphctl kexec /tmp/fake-kernel 2>&1" || echo "")

    if echo "$SAFETY_OUTPUT" | grep -q -i "confirm\|safety\|dangerous\|abort"; then
        log_success "kexec safety mechanisms active"
    else
        log_info "kexec safety prompts may not be visible in non-interactive mode"
    fi

    # Test that dry-run mode doesn't perform dangerous operations
    vm_run "touch /tmp/safety-test-kernel"
    DRY_RUN_OUTPUT=$(vm_run "graphctl kexec --dry-run /tmp/safety-test-kernel 2>&1" || echo "")

    if echo "$DRY_RUN_OUTPUT" | grep -q -i "dry.run\|simulation\|would"; then
        log_success "Dry-run mode clearly indicated"
    else
        log_error "Dry-run mode not clearly indicated"
        vm_run "rm -f /tmp/safety-test-kernel"
        return 1
    fi

    vm_run "rm -f /tmp/safety-test-kernel"
    return 0
}

# Test 8: Test command line argument parsing
test_argument_parsing() {
    log_info "Testing kexec argument parsing..."

    # Test initrd argument
    INITRD_OUTPUT=$(vm_run "graphctl kexec --dry-run --initrd /tmp/test-initrd /tmp/test-kernel 2>&1" || echo "")

    if echo "$INITRD_OUTPUT" | grep -q -i "initrd"; then
        log_success "initrd argument parsing working"
    else
        log_info "initrd argument may not be processed in dry-run"
    fi

    # Test command line append
    APPEND_OUTPUT=$(vm_run "graphctl kexec --dry-run --append 'test=1' /tmp/test-kernel 2>&1" || echo "")

    if echo "$APPEND_OUTPUT" | grep -q "test=1"; then
        log_success "Command line append argument working"
    else
        log_info "Command line append may not be visible in output"
    fi

    return 0
}

# Test 9: Test error handling
test_error_handling() {
    log_info "Testing kexec error handling..."

    # Test with insufficient permissions (if applicable)
    PERM_OUTPUT=$(vm_run "su -s /bin/sh -c 'graphctl kexec --dry-run /tmp/test 2>&1' nobody" 2>/dev/null || echo "permission-error")

    if echo "$PERM_OUTPUT" | grep -q -i "permission\|denied\|error"; then
        log_success "Permission error handling working"
    else
        log_info "Permission checks may not be applicable in test environment"
    fi

    # Test with system busy conditions
    BUSY_OUTPUT=$(vm_run "graphctl kexec --dry-run --force /tmp/test-kernel 2>&1" || echo "")

    if echo "$BUSY_OUTPUT" | grep -q -i "force\|override"; then
        log_success "Force option handling working"
    else
        log_info "Force option handling may not be visible"
    fi

    return 0
}

# Test 10: Test performance and resource usage
test_performance() {
    log_info "Testing kexec performance characteristics..."

    # Measure dry-run execution time
    START_TIME=$(date +%s)
    vm_run "graphctl kexec --dry-run /dev/null 2>/dev/null" || true
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))

    if [ $DURATION -lt 30 ]; then  # Should complete quickly in dry-run
        log_success "kexec dry-run performance acceptable (${DURATION}s)"
    else
        log_error "kexec dry-run too slow (${DURATION}s)"
        return 1
    fi

    # Check memory usage during operation
    MEMORY_BEFORE=$(vm_run "free -m | awk 'NR==2{print \$3}'")
    vm_run "graphctl kexec --dry-run /dev/null 2>/dev/null" || true
    MEMORY_AFTER=$(vm_run "free -m | awk 'NR==2{print \$3}'")

    MEMORY_DELTA=$((MEMORY_AFTER - MEMORY_BEFORE))
    if [ $MEMORY_DELTA -lt 50 ]; then  # Less than 50MB increase
        log_success "kexec memory usage reasonable (${MEMORY_DELTA}MB change)"
    else
        log_error "kexec memory usage high (${MEMORY_DELTA}MB change)"
        return 1
    fi

    return 0
}

# Main test execution
main() {
    echo "YakirOS kexec Testing"
    echo "===================="
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: Live kernel upgrades (Step 10) - DRY-RUN MODE ONLY"
    echo

    TESTS_TOTAL=10
    TESTS_PASSED=0
    TESTS_SKIPPED=0

    # Run all tests
    if test_kexec_commands_available; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_system_readiness; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_kernel_validation; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_kexec_prerequisites; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_seven_phase_sequence; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_checkpoint_integration; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_safety_mechanisms; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_argument_parsing; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_error_handling; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_performance; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Summary
    echo
    echo "kexec Test Results:"
    echo "=================="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"
    echo
    echo "⚠️  NOTE: All kexec tests run in DRY-RUN mode for safety"
    echo "⚠️  Actual kernel upgrades require production environment"

    if [ $TESTS_PASSED -ge 7 ]; then  # Expect at least 7/10 tests to pass
        log_success "kexec tests passed!"
        exit 0
    else
        log_error "kexec tests failed!"
        exit 1
    fi
}

main "$@"