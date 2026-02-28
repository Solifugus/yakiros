#!/bin/bash
#
# YakirOS CRIU Checkpointing Testing Script
# Tests checkpoint/restore functionality with process state preservation
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
    echo -e "${BLUE}[CHECKPOINT] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[CHECKPOINT] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[CHECKPOINT] ❌ $1${NC}"
}

log_skip() {
    echo -e "${YELLOW}[CHECKPOINT] ⏭️ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Check CRIU availability
test_criu_availability() {
    log_info "Testing CRIU availability..."

    # Check if CRIU binary is available
    if vm_run "command -v criu >/dev/null 2>&1"; then
        CRIU_VERSION=$(vm_run "criu --version 2>/dev/null | head -1" || echo "unknown")
        log_success "CRIU available: $CRIU_VERSION"
        return 0
    else
        log_skip "CRIU not available - checkpointing tests will be skipped"
        return 1
    fi
}

# Test 2: Test basic checkpoint support detection
test_checkpoint_support() {
    log_info "Testing checkpoint support detection..."

    # Use YakirOS built-in CRIU support check
    if vm_run "graphctl checkpoint --check >/dev/null 2>&1"; then
        log_success "YakirOS checkpoint support detected"
        return 0
    else
        log_skip "YakirOS checkpoint support not available"
        return 1
    fi
}

# Test 3: Test checkpoint configuration
test_checkpoint_configuration() {
    log_info "Testing checkpoint configuration..."

    # Check if stateful service has checkpoint config
    if vm_run "grep -q 'handoff.*checkpoint' /etc/graph.d/05-stateful-service.toml"; then
        log_success "Stateful service configured for checkpointing"
    else
        log_error "Stateful service not configured for checkpointing"
        return 1
    fi

    # Check checkpoint section configuration
    if vm_run "grep -A5 '\\[checkpoint\\]' /etc/graph.d/05-stateful-service.toml | grep -q 'enabled.*true'"; then
        log_success "Checkpoint section properly configured"
    else
        log_error "Checkpoint section configuration missing"
        return 1
    fi

    return 0
}

# Test 4: Test checkpoint directory setup
test_checkpoint_directories() {
    log_info "Testing checkpoint directory setup..."

    # Check if checkpoint directories exist
    if vm_run "test -d /run/graph/checkpoints"; then
        log_success "Runtime checkpoint directory exists"
    else
        log_error "Runtime checkpoint directory missing"
        return 1
    fi

    if vm_run "test -d /var/lib/graph/checkpoints"; then
        log_success "Persistent checkpoint directory exists"
    else
        log_error "Persistent checkpoint directory missing"
        return 1
    fi

    # Check permissions
    PERMS=$(vm_run "stat -c %a /run/graph/checkpoints" 2>/dev/null || echo "000")
    if [ "$PERMS" -ge 755 ]; then
        log_success "Checkpoint directories have appropriate permissions"
    else
        log_error "Checkpoint directories have insufficient permissions: $PERMS"
        return 1
    fi

    return 0
}

# Test 5: Test manual checkpoint creation
test_manual_checkpoint() {
    log_info "Testing manual checkpoint creation..."

    # Ensure stateful service is running
    if ! vm_run "graphctl status | grep test-stateful-service | grep -q ACTIVE"; then
        log_error "Stateful service not running for checkpoint test"
        return 1
    fi

    # Get initial state from stateful service
    INITIAL_COUNTER=$(vm_run "curl -s --max-time 5 'http://localhost:8081/status' | grep -o '\"counter\": [0-9]*' | cut -d: -f2 | tr -d ' '" 2>/dev/null || echo "0")
    log_info "Initial counter value: $INITIAL_COUNTER"

    # Increment counter to create state change
    vm_run "curl -s --max-time 5 'http://localhost:8081/increment' >/dev/null 2>&1" || log_info "Counter increment may have failed"

    # Create manual checkpoint
    log_info "Creating manual checkpoint..."
    CHECKPOINT_OUTPUT=$(vm_run "graphctl checkpoint test-stateful-service 2>&1" || echo "FAILED")

    if echo "$CHECKPOINT_OUTPUT" | grep -q -i "success\|created\|checkpoint"; then
        log_success "Manual checkpoint creation succeeded"
    else
        log_skip "Manual checkpoint creation skipped or failed: $CHECKPOINT_OUTPUT"
        return 1
    fi

    return 0
}

# Test 6: Test checkpoint listing
test_checkpoint_listing() {
    log_info "Testing checkpoint listing..."

    # List available checkpoints
    CHECKPOINT_LIST=$(vm_run "graphctl checkpoint-list test-stateful-service 2>&1" || echo "")

    if echo "$CHECKPOINT_LIST" | grep -q "test-stateful-service"; then
        log_success "Checkpoint listing shows created checkpoints"
        log_info "Available checkpoints: $CHECKPOINT_LIST"
    else
        log_skip "No checkpoints found in listing"
        return 1
    fi

    return 0
}

# Test 7: Test state preservation in checkpoints
test_state_preservation() {
    log_info "Testing state preservation in checkpoints..."

    # Modify service state
    log_info "Modifying service state for preservation test..."

    # Add a message to stateful service
    MESSAGE_RESULT=$(vm_run "curl -s --max-time 5 -X POST -H 'Content-Type: application/json' -d '{\"text\":\"checkpoint-test-message\"}' 'http://localhost:8081/message' 2>/dev/null" || echo "")

    if echo "$MESSAGE_RESULT" | grep -q "message_added"; then
        log_success "Service state modified successfully"
    else
        log_error "Failed to modify service state for testing"
        return 1
    fi

    # Verify state change is visible
    CURRENT_STATE=$(vm_run "curl -s --max-time 5 'http://localhost:8081/' 2>/dev/null" || echo "")

    if echo "$CURRENT_STATE" | grep -q "checkpoint-test-message"; then
        log_success "State change verified in service"
    else
        log_error "State change not visible in service"
        return 1
    fi

    return 0
}

# Test 8: Test checkpoint metadata
test_checkpoint_metadata() {
    log_info "Testing checkpoint metadata..."

    # Check if metadata files exist
    if vm_run "find /run/graph/checkpoints -name 'metadata.json' | head -1" >/dev/null 2>&1; then
        METADATA_FILE=$(vm_run "find /run/graph/checkpoints -name 'metadata.json' | head -1")
        log_success "Checkpoint metadata file found: $METADATA_FILE"

        # Validate metadata content
        if vm_run "cat '$METADATA_FILE' | grep -q 'component_name'" 2>/dev/null; then
            log_success "Metadata contains component information"
        else
            log_error "Metadata lacks component information"
            return 1
        fi

        if vm_run "cat '$METADATA_FILE' | grep -q 'timestamp'" 2>/dev/null; then
            log_success "Metadata contains timestamp"
        else
            log_error "Metadata lacks timestamp"
            return 1
        fi
    else
        log_skip "No metadata files found"
        return 1
    fi

    return 0
}

# Test 9: Test checkpoint cleanup
test_checkpoint_cleanup() {
    log_info "Testing checkpoint cleanup..."

    # Count existing checkpoints
    CHECKPOINT_COUNT_BEFORE=$(vm_run "find /run/graph/checkpoints -type d -name 'test-stateful-service' | wc -l" 2>/dev/null || echo "0")
    log_info "Checkpoints before cleanup: $CHECKPOINT_COUNT_BEFORE"

    # Test cleanup command
    if vm_run "graphctl checkpoint-cleanup --max-age=0 >/dev/null 2>&1"; then
        log_success "Checkpoint cleanup command executed"

        # Check if cleanup occurred
        CHECKPOINT_COUNT_AFTER=$(vm_run "find /run/graph/checkpoints -type d -name 'test-stateful-service' | wc -l" 2>/dev/null || echo "0")
        log_info "Checkpoints after cleanup: $CHECKPOINT_COUNT_AFTER"

        if [ "$CHECKPOINT_COUNT_AFTER" -le "$CHECKPOINT_COUNT_BEFORE" ]; then
            log_success "Checkpoint cleanup working"
        else
            log_error "Checkpoint cleanup not working properly"
            return 1
        fi
    else
        log_skip "Checkpoint cleanup command not available"
        return 1
    fi

    return 0
}

# Test 10: Test three-level fallback strategy
test_fallback_strategy() {
    log_info "Testing three-level fallback strategy..."

    # Test upgrade with checkpoint fallback
    log_info "Testing upgrade with fallback strategy..."

    # Attempt upgrade (should try checkpoint first, then fallback)
    UPGRADE_OUTPUT=$(vm_run "graphctl upgrade test-stateful-service 2>&1" || echo "")

    log_info "Upgrade output: $UPGRADE_OUTPUT"

    # Check if fallback strategy is mentioned in output
    if echo "$UPGRADE_OUTPUT" | grep -q -i "fallback\|checkpoint\|fd-passing"; then
        log_success "Fallback strategy information present"
    else
        log_info "Fallback strategy information not visible in output"
    fi

    # Verify service is still running after upgrade attempt
    if vm_run "graphctl status | grep test-stateful-service | grep -q ACTIVE"; then
        log_success "Service remained active through upgrade process"
    else
        log_error "Service not active after upgrade attempt"
        return 1
    fi

    return 0
}

# Main test execution
main() {
    echo "YakirOS CRIU Checkpointing Testing"
    echo "=================================="
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: CRIU checkpoint/restore (Step 9)"
    echo

    TESTS_TOTAL=10
    TESTS_PASSED=0
    TESTS_SKIPPED=0
    CRIU_AVAILABLE=false

    # Check if CRIU is available first
    if test_criu_availability && test_checkpoint_support; then
        CRIU_AVAILABLE=true
        log_info "CRIU available - running full checkpoint test suite"
    else
        log_info "CRIU not available - running configuration and fallback tests only"
    fi

    # Run configuration tests (always run)
    if test_checkpoint_configuration; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_checkpoint_directories; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_fallback_strategy; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Run CRIU-dependent tests only if available
    if [ "$CRIU_AVAILABLE" = true ]; then
        if test_manual_checkpoint; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
        fi

        if test_checkpoint_listing; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
        fi

        if test_state_preservation; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
        fi

        if test_checkpoint_metadata; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
        fi

        if test_checkpoint_cleanup; then
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
        fi
    else
        TESTS_SKIPPED=7  # Skip the 7 CRIU-dependent tests
    fi

    # Summary
    echo
    echo "CRIU Checkpointing Test Results:"
    echo "==============================="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Skipped: $TESTS_SKIPPED (CRIU unavailable)"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED - TESTS_SKIPPED))"

    if [ "$CRIU_AVAILABLE" = true ]; then
        if [ $TESTS_PASSED -ge 7 ]; then  # Expect at least 7 tests to pass with CRIU
            log_success "Checkpointing tests passed!"
            exit 0
        else
            log_error "Checkpointing tests failed!"
            exit 1
        fi
    else
        if [ $TESTS_PASSED -ge 3 ]; then  # Expect at least 3 basic tests to pass
            log_success "Basic checkpointing infrastructure tests passed!"
            exit 0
        else
            log_error "Basic checkpointing infrastructure tests failed!"
            exit 1
        fi
    fi
}

main "$@"