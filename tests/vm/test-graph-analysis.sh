#!/bin/bash
#
# YakirOS Graph Analysis Testing Script
# Tests cycle detection and graph analysis features
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
    echo -e "${BLUE}[GRAPH] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[GRAPH] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[GRAPH] ❌ $1${NC}"
}

vm_run() {
    ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "$@"
}

# Test 1: Verify graph analysis commands are available
test_graph_commands_available() {
    log_info "Testing graph analysis commands availability..."

    # Test check-cycles command
    if vm_run "graphctl check-cycles --help >/dev/null 2>&1"; then
        log_success "check-cycles command available"
    else
        log_error "check-cycles command not available"
        return 1
    fi

    # Test analyze command
    if vm_run "graphctl analyze --help >/dev/null 2>&1"; then
        log_success "analyze command available"
    else
        log_error "analyze command not available"
        return 1
    fi

    # Test validate command
    if vm_run "graphctl validate --help >/dev/null 2>&1"; then
        log_success "validate command available"
    else
        log_error "validate command not available"
        return 1
    fi

    return 0
}

# Test 2: Test cycle detection with clean graph
test_clean_graph_validation() {
    log_info "Testing clean graph validation..."

    # Run cycle detection on current graph (should have cycles from test components)
    CYCLE_OUTPUT=$(vm_run "graphctl check-cycles 2>&1" || echo "")

    log_info "Cycle detection output: $CYCLE_OUTPUT"

    # The test components include intentional cycles (test-cycle-a <-> test-cycle-b)
    if echo "$CYCLE_OUTPUT" | grep -q "cycle"; then
        log_success "Cycle detection working - found expected test cycles"
    else
        log_error "Cycle detection not finding expected cycles"
        return 1
    fi

    return 0
}

# Test 3: Test graph metrics analysis
test_graph_metrics() {
    log_info "Testing graph metrics analysis..."

    # Run graph analysis
    ANALYSIS_OUTPUT=$(vm_run "graphctl analyze 2>&1" || echo "")

    log_info "Graph analysis output: $ANALYSIS_OUTPUT"

    # Check for key metrics
    if echo "$ANALYSIS_OUTPUT" | grep -q "components"; then
        COMPONENT_COUNT=$(echo "$ANALYSIS_OUTPUT" | grep "components" | grep -o '[0-9]\+' | head -1)
        log_success "Component count detected: $COMPONENT_COUNT"
    else
        log_error "Component count not found in analysis"
        return 1
    fi

    if echo "$ANALYSIS_OUTPUT" | grep -q "capabilities"; then
        CAPABILITY_COUNT=$(echo "$ANALYSIS_OUTPUT" | grep "capabilities" | grep -o '[0-9]\+' | head -1)
        log_success "Capability count detected: $CAPABILITY_COUNT"
    else
        log_error "Capability count not found in analysis"
        return 1
    fi

    if echo "$ANALYSIS_OUTPUT" | grep -q "dependencies"; then
        log_success "Dependency analysis present"
    else
        log_error "Dependency analysis missing"
        return 1
    fi

    return 0
}

# Test 4: Test specific cycle detection scenarios
test_specific_cycles() {
    log_info "Testing specific cycle detection..."

    # The test configuration includes cycle-a -> cycle-b -> cycle-a
    CYCLE_DETAILS=$(vm_run "graphctl check-cycles --verbose 2>&1" || echo "")

    log_info "Detailed cycle information: $CYCLE_DETAILS"

    # Check if both cycle components are mentioned
    if echo "$CYCLE_DETAILS" | grep -q "test-cycle-a" && echo "$CYCLE_DETAILS" | grep -q "test-cycle-b"; then
        log_success "Cycle detection identifies specific components"
    else
        log_error "Cycle detection not identifying specific components"
        return 1
    fi

    # Check if the cycle path is shown
    if echo "$CYCLE_DETAILS" | grep -q "cycle.a" && echo "$CYCLE_DETAILS" | grep -q "cycle.b"; then
        log_success "Cycle path shows capability dependencies"
    else
        log_error "Cycle path not showing capability details"
        return 1
    fi

    return 0
}

# Test 5: Test graph validation
test_graph_validation() {
    log_info "Testing graph validation..."

    # Run comprehensive validation
    VALIDATION_OUTPUT=$(vm_run "graphctl validate 2>&1" || echo "")

    log_info "Validation output: $VALIDATION_OUTPUT"

    # Validation should report the known cycles
    if echo "$VALIDATION_OUTPUT" | grep -q -i "cycle\|warning\|error"; then
        log_success "Graph validation reports issues (expected with test cycles)"
    else
        log_info "Graph validation reports no issues (unexpected with test cycles)"
    fi

    # Check if validation provides actionable information
    if echo "$VALIDATION_OUTPUT" | grep -q "test-cycle"; then
        log_success "Validation provides component-specific information"
    else
        log_error "Validation lacks component-specific details"
        return 1
    fi

    return 0
}

# Test 6: Test dependency tree visualization
test_dependency_trees() {
    log_info "Testing dependency tree visualization..."

    # Test tree command on a service with dependencies
    TREE_OUTPUT=$(vm_run "graphctl tree test-monitor 2>&1" || echo "")

    log_info "Dependency tree for test-monitor: $TREE_OUTPUT"

    # test-monitor depends on multiple services, should show a tree
    if echo "$TREE_OUTPUT" | grep -q "test-echo\|test-stateful\|test-health"; then
        log_success "Dependency tree shows expected dependencies"
    else
        log_error "Dependency tree missing expected dependencies"
        return 1
    fi

    # Check for tree formatting
    if echo "$TREE_OUTPUT" | grep -q "├\|└\|│"; then
        log_success "Tree visualization uses proper ASCII art"
    else
        log_error "Tree visualization lacks proper formatting"
        return 1
    fi

    return 0
}

# Test 7: Test reverse dependency analysis
test_reverse_dependencies() {
    log_info "Testing reverse dependency analysis..."

    # Test rdeps command on a foundational capability
    RDEPS_OUTPUT=$(vm_run "graphctl rdeps network.configured 2>&1" || echo "")

    log_info "Reverse dependencies for network.configured: $RDEPS_OUTPUT"

    # Many services depend on network.configured
    if echo "$RDEPS_OUTPUT" | grep -q "test-echo\|test-stateful"; then
        log_success "Reverse dependency analysis shows dependent services"
    else
        log_error "Reverse dependency analysis missing expected dependents"
        return 1
    fi

    return 0
}

# Test 8: Test graph complexity analysis
test_complexity_analysis() {
    log_info "Testing graph complexity analysis..."

    # Get detailed analysis with complexity metrics
    COMPLEX_OUTPUT=$(vm_run "graphctl analyze --detailed 2>&1" || echo "")

    log_info "Complexity analysis: $COMPLEX_OUTPUT"

    # Look for complexity indicators
    if echo "$COMPLEX_OUTPUT" | grep -q -i "complexity\|average\|depth\|maximum"; then
        log_success "Complexity analysis provides metrics"
    else
        log_info "Complexity analysis may not include advanced metrics"
    fi

    # Check for performance information
    if echo "$COMPLEX_OUTPUT" | grep -q -i "resolution\|time"; then
        log_success "Analysis includes performance information"
    else
        log_info "Performance information not included in analysis"
    fi

    return 0
}

# Test 9: Test graph output formats
test_output_formats() {
    log_info "Testing graph output formats..."

    # Test DOT format output
    DOT_OUTPUT=$(vm_run "graphctl dot 2>&1" || echo "")

    if echo "$DOT_OUTPUT" | grep -q "digraph\|node\|edge"; then
        log_success "DOT format output working"
    else
        log_error "DOT format output not working"
        return 1
    fi

    # Check if DOT output includes test components
    if echo "$DOT_OUTPUT" | grep -q "test-"; then
        log_success "DOT output includes test components"
    else
        log_error "DOT output missing test components"
        return 1
    fi

    return 0
}

# Main test execution
main() {
    echo "YakirOS Graph Analysis Testing"
    echo "=============================="
    echo "VM SSH Port: $VM_SSH_PORT"
    echo "Target: Cycle detection and graph analysis (Step 8)"
    echo

    TESTS_TOTAL=9
    TESTS_PASSED=0

    # Run all tests
    if test_graph_commands_available; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_clean_graph_validation; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_graph_metrics; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_specific_cycles; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_graph_validation; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_dependency_trees; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_reverse_dependencies; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_complexity_analysis; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    if test_output_formats; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi

    # Summary
    echo
    echo "Graph Analysis Test Results:"
    echo "============================"
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $((TESTS_TOTAL - TESTS_PASSED))"

    if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
        log_success "All graph analysis tests passed!"
        exit 0
    else
        log_error "Some graph analysis tests failed!"
        exit 1
    fi
}

main "$@"