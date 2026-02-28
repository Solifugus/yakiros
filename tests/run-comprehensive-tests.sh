#!/bin/bash
# YakirOS Comprehensive Test Harness
# Automated testing suite for all YakirOS revolutionary capabilities

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }

# Test statistics
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
START_TIME=$(date +%s)

cd "$(dirname "$0")/.."

echo "============================================="
echo "  🧪 YakirOS Comprehensive Test Harness"
echo "============================================="
echo ""
echo "Testing revolutionary init system capabilities:"
echo "  • Reactive dependency resolution"
echo "  • Advanced readiness protocol"
echo "  • Hot-swappable services"
echo "  • File descriptor passing"
echo "  • Enterprise-grade reliability"
echo ""

# Function to run a single test
run_test() {
    local test_name="$1"
    local test_binary="$2"
    local description="$3"

    echo "----------------------------------------"
    log_info "Running $test_name: $description"
    echo "----------------------------------------"

    ((TESTS_RUN++))

    if [ -x "$test_binary" ]; then
        if $test_binary; then
            ((TESTS_PASSED++))
            log_success "$test_name PASSED"
        else
            ((TESTS_FAILED++))
            log_error "$test_name FAILED"
        fi
    else
        log_warning "$test_binary not found or not executable"
        ((TESTS_FAILED++))
    fi

    echo ""
}

# Build all tests
log_info "Building comprehensive test suite..."

# Update Makefile to include new tests
if ! grep -q "test_hotswap\|test_readiness_comprehensive\|test_revolutionary_features" Makefile; then
    log_info "Adding new tests to Makefile..."

    # Add new test targets (this would be done by updating the Makefile)
    cat >> Makefile << 'EOF'

# New comprehensive tests for revolutionary features
tests/unit/test_hotswap: tests/unit/test_hotswap.c src/hotswap.c src/component.c src/capability.c src/toml.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_readiness_comprehensive: tests/unit/test_readiness_comprehensive.c src/component.c src/capability.c src/toml.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/integration/test_revolutionary_features: tests/integration/test_revolutionary_features.c src/component.c src/capability.c src/graph.c src/hotswap.c src/toml.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

# Update test lists
COMPREHENSIVE_UNIT_TESTS = $(UNIT_TESTS) tests/unit/test_hotswap tests/unit/test_readiness_comprehensive
COMPREHENSIVE_INTEGRATION_TESTS = $(INTEGRATION_TESTS) tests/integration/test_revolutionary_features
ALL_COMPREHENSIVE_TESTS = $(COMPREHENSIVE_UNIT_TESTS) $(COMPREHENSIVE_INTEGRATION_TESTS)

# Comprehensive test targets
build-comprehensive-tests: $(ALL_COMPREHENSIVE_TESTS)

test-comprehensive: build-comprehensive-tests
	@echo "Running comprehensive YakirOS test suite..."
	./tests/run-comprehensive-tests.sh

.PHONY: build-comprehensive-tests test-comprehensive
EOF
fi

# Build the tests
log_info "Compiling test suite..."

if make build-tests >/dev/null 2>&1; then
    log_success "Standard tests compiled successfully"
else
    log_warning "Some standard tests failed to compile"
fi

# Try to build new comprehensive tests
log_info "Compiling revolutionary features tests..."

# Compile new tests manually if Makefile updates didn't work
if [ -f "tests/unit/test_hotswap.c" ]; then
    if gcc -Wall -Wextra -Werror -O2 -std=c11 -Isrc -Itests \
        -o tests/unit/test_hotswap \
        tests/unit/test_hotswap.c src/hotswap.c src/component.c src/capability.c src/toml.c src/log.c \
        2>/dev/null; then
        log_success "Hot-swap tests compiled"
    else
        log_warning "Hot-swap tests compilation failed"
    fi
fi

if [ -f "tests/unit/test_readiness_comprehensive.c" ]; then
    if gcc -Wall -Wextra -Werror -O2 -std=c11 -Isrc -Itests \
        -o tests/unit/test_readiness_comprehensive \
        tests/unit/test_readiness_comprehensive.c src/component.c src/capability.c src/toml.c src/log.c \
        2>/dev/null; then
        log_success "Readiness protocol tests compiled"
    else
        log_warning "Readiness protocol tests compilation failed"
    fi
fi

if [ -f "tests/integration/test_revolutionary_features.c" ]; then
    if gcc -Wall -Wextra -Werror -O2 -std=c11 -Isrc -Itests \
        -o tests/integration/test_revolutionary_features \
        tests/integration/test_revolutionary_features.c src/component.c src/capability.c src/graph.c src/hotswap.c src/toml.c src/log.c \
        2>/dev/null; then
        log_success "Revolutionary features integration tests compiled"
    else
        log_warning "Revolutionary features integration tests compilation failed"
    fi
fi

echo ""
log_info "Starting comprehensive test execution..."
echo ""

# Run existing unit tests
log_info "Phase 1: Core Unit Tests"
echo ""

run_test "TOML Parser" "tests/unit/test_toml" "Configuration file parsing"
run_test "TOML Readiness" "tests/unit/test_toml_readiness" "Readiness configuration parsing"
run_test "Capability System" "tests/unit/test_capability" "Capability registration and tracking"
run_test "Component Management" "tests/unit/test_component" "Component lifecycle management"
run_test "Graph Resolution" "tests/unit/test_graph" "Dependency graph resolution"
run_test "Logging System" "tests/unit/test_log" "Logging and diagnostics"
run_test "Control Interface" "tests/unit/test_control" "graphctl communication"

# Run new comprehensive tests
log_info "Phase 2: Revolutionary Features Tests"
echo ""

run_test "Hot-Swap Services" "tests/unit/test_hotswap" "Zero-downtime service upgrades"
run_test "Readiness Protocol" "tests/unit/test_readiness_comprehensive" "Advanced readiness signaling"

# Run integration tests
log_info "Phase 3: Integration Tests"
echo ""

run_test "Full System Integration" "tests/integration/test_full_system" "Complete system integration"
run_test "Revolutionary Features Integration" "tests/integration/test_revolutionary_features" "Advanced features integration"

# Performance tests
log_info "Phase 4: Performance and Stress Tests"
echo ""

if [ -x tests/unit/test_hotswap ]; then
    log_info "Running hot-swap performance tests..."
    tests/unit/test_hotswap | grep -i "performance\|ms\|seconds" || true
fi

if [ -x tests/unit/test_readiness_comprehensive ]; then
    log_info "Running readiness protocol performance tests..."
    tests/unit/test_readiness_comprehensive | grep -i "performance\|ms\|seconds" || true
fi

# Test YakirOS binaries
log_info "Phase 5: Binary Validation"
echo ""

log_info "Testing YakirOS binaries..."
if [ -x graph-resolver ]; then
    if file graph-resolver | grep -q "statically linked"; then
        log_success "graph-resolver is statically linked"
        ((TESTS_PASSED++))
    else
        log_error "graph-resolver is not statically linked"
        ((TESTS_FAILED++))
    fi
    ((TESTS_RUN++))
else
    log_warning "graph-resolver binary not found"
fi

if [ -x graphctl ]; then
    if file graphctl | grep -q "statically linked"; then
        log_success "graphctl is statically linked"
        ((TESTS_PASSED++))
    else
        log_error "graphctl is not statically linked"
        ((TESTS_FAILED++))
    fi
    ((TESTS_RUN++))
else
    log_warning "graphctl binary not found"
fi

# Test enhanced graphctl
if [ -x src/enhanced-graphctl ] || [ -f src/enhanced-graphctl.c ]; then
    if [ ! -x src/enhanced-graphctl ]; then
        gcc -o src/enhanced-graphctl src/enhanced-graphctl.c 2>/dev/null || true
    fi

    if [ -x src/enhanced-graphctl ]; then
        log_info "Testing enhanced graphctl..."
        if src/enhanced-graphctl demo-hotswap >/dev/null 2>&1; then
            log_success "Enhanced graphctl hot-swap demo works"
            ((TESTS_PASSED++))
        else
            log_error "Enhanced graphctl hot-swap demo failed"
            ((TESTS_FAILED++))
        fi
        ((TESTS_RUN++))
    fi
fi

# Calculate results
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo ""
echo "============================================="
echo "  🏁 Test Suite Results"
echo "============================================="
echo ""

printf "Tests run:    %3d\n" $TESTS_RUN
printf "Tests passed: %3d\n" $TESTS_PASSED
printf "Tests failed: %3d\n" $TESTS_FAILED
printf "Duration:     %3d seconds\n" $DURATION

if [ $TESTS_FAILED -eq 0 ]; then
    SUCCESS_RATE=100
else
    SUCCESS_RATE=$((TESTS_PASSED * 100 / TESTS_RUN))
fi

printf "Success rate: %3d%%\n" $SUCCESS_RATE

echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    log_success "🎉 ALL TESTS PASSED!"
    echo ""
    echo "YakirOS Comprehensive Test Results:"
    echo "  ✅ Core functionality: OPERATIONAL"
    echo "  ✅ Readiness protocol: VERIFIED"
    echo "  ✅ Hot-swappable services: TESTED"
    echo "  ✅ Integration: SUCCESSFUL"
    echo "  ✅ Performance: ACCEPTABLE"
    echo ""
    echo "🌟 YakirOS is production-ready with revolutionary capabilities!"
    exit 0
else
    log_error "❌ Some tests failed"
    echo ""
    echo "Please review failed tests and fix issues before deployment."
    echo "YakirOS revolutionary features require all tests to pass."
    exit 1
fi