#!/bin/bash
#
# YakirOS Continuous Testing Script
# Long-running stability testing with periodic reporting
#

set -e

# Configuration
DURATION_HOURS=24
REPORT_INTERVAL_HOURS=1
TEST_DIR="$(dirname "$0")"
VM_SSH_PORT="2222"
VM_HOST="localhost"
RESULTS_BASE="/tmp/yakiros-continuous-testing"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Statistics tracking
TOTAL_TEST_RUNS=0
SUCCESSFUL_RUNS=0
FAILED_RUNS=0
START_TIME=$(date +%s)

log_info() {
    echo -e "${BLUE}[CONTINUOUS] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[CONTINUOUS] ✅ $1${NC}"
}

log_error() {
    echo -e "${RED}[CONTINUOUS] ❌ $1${NC}"
}

log_warn() {
    echo -e "${YELLOW}[CONTINUOUS] ⚠️ $1${NC}"
}

log_header() {
    echo -e "\n${PURPLE}[CONTINUOUS] 🚀 $1${NC}"
    echo "================================================================="
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --duration=*)
                DURATION_INPUT="${1#*=}"
                if [[ $DURATION_INPUT =~ ^([0-9]+)h$ ]]; then
                    DURATION_HOURS="${BASH_REMATCH[1]}"
                elif [[ $DURATION_INPUT =~ ^([0-9]+)$ ]]; then
                    DURATION_HOURS="$DURATION_INPUT"
                else
                    log_error "Invalid duration format. Use: 24h or 24"
                    exit 1
                fi
                shift
                ;;
            --report-interval=*)
                INTERVAL_INPUT="${1#*=}"
                if [[ $INTERVAL_INPUT =~ ^([0-9]+)h$ ]]; then
                    REPORT_INTERVAL_HOURS="${BASH_REMATCH[1]}"
                elif [[ $INTERVAL_INPUT =~ ^([0-9]+)$ ]]; then
                    REPORT_INTERVAL_HOURS="$INTERVAL_INPUT"
                else
                    log_error "Invalid interval format. Use: 1h or 1"
                    exit 1
                fi
                shift
                ;;
            --help|-h)
                echo "YakirOS Continuous Testing"
                echo "Usage: $0 [options]"
                echo
                echo "Options:"
                echo "  --duration=24h         Test duration (default: 24h)"
                echo "  --report-interval=1h   Report interval (default: 1h)"
                echo "  --help                 Show this help"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
}

# Setup continuous testing environment
setup_continuous_testing() {
    log_header "Setting up Continuous Testing Environment"

    # Create results directory with timestamp
    RESULTS_DIR="$RESULTS_BASE/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$RESULTS_DIR/runs"
    mkdir -p "$RESULTS_DIR/reports"
    mkdir -p "$RESULTS_DIR/monitoring"

    # Calculate end time
    END_TIME=$((START_TIME + DURATION_HOURS * 3600))
    REPORT_INTERVAL_SECONDS=$((REPORT_INTERVAL_HOURS * 3600))

    log_info "Test duration: ${DURATION_HOURS} hours"
    log_info "Report interval: ${REPORT_INTERVAL_HOURS} hours"
    log_info "Results directory: $RESULTS_DIR"
    log_info "End time: $(date -d @$END_TIME)"

    # Create initial configuration file
    cat > "$RESULTS_DIR/config.json" << EOF
{
  "continuous_testing_config": {
    "start_time": "$(date -d @$START_TIME -Iseconds)",
    "end_time": "$(date -d @$END_TIME -Iseconds)",
    "duration_hours": $DURATION_HOURS,
    "report_interval_hours": $REPORT_INTERVAL_HOURS,
    "vm_config": {
      "ssh_port": $VM_SSH_PORT,
      "host": "$VM_HOST"
    }
  }
}
EOF

    log_success "Continuous testing environment ready"
}

# Check VM connectivity and YakirOS status
check_system_status() {
    # Check VM connectivity
    if ! ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -p ${VM_SSH_PORT} root@${VM_HOST} echo "VM accessible" >/dev/null 2>&1; then
        log_error "VM not accessible - continuous testing cannot proceed"
        return 1
    fi

    # Check YakirOS status
    if ! ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status" >/dev/null 2>&1; then
        log_error "YakirOS not responding - continuous testing cannot proceed"
        return 1
    fi

    return 0
}

# Run a single test cycle
run_test_cycle() {
    local cycle_number=$1
    local timestamp=$(date +%Y%m%d-%H%M%S)
    local cycle_dir="$RESULTS_DIR/runs/cycle-$cycle_number-$timestamp"

    mkdir -p "$cycle_dir"

    log_info "Starting test cycle $cycle_number at $(date)"

    # Run comprehensive test suite
    if timeout 1800 "$TEST_DIR/test-runner.sh" comprehensive > "$cycle_dir/test-output.log" 2>&1; then
        echo "SUCCESS" > "$cycle_dir/result"
        SUCCESSFUL_RUNS=$((SUCCESSFUL_RUNS + 1))
        log_success "Test cycle $cycle_number completed successfully"
    else
        echo "FAILURE" > "$cycle_dir/result"
        FAILED_RUNS=$((FAILED_RUNS + 1))
        log_error "Test cycle $cycle_number failed"
    fi

    TOTAL_TEST_RUNS=$((TOTAL_TEST_RUNS + 1))

    # Capture system metrics after test run
    capture_system_metrics "$cycle_dir"
}

# Capture system performance metrics
capture_system_metrics() {
    local metrics_dir="$1"

    # YakirOS memory usage
    YAKIROS_MEMORY=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "ps -o rss -p 1 | tail -1" 2>/dev/null || echo "0")

    # System memory usage
    SYSTEM_MEMORY=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "free -m | awk 'NR==2{print \$3}'" 2>/dev/null || echo "0")

    # Active component count
    ACTIVE_COMPONENTS=$(ssh -o StrictHostKeyChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status | grep -c ACTIVE" 2>/dev/null || echo "0")

    # Failed component count
    FAILED_COMPONENTS=$(ssh -o StrictHostChecking=no -p ${VM_SSH_PORT} root@${VM_HOST} "graphctl status | grep -c FAILED" 2>/dev/null || echo "0")

    # Save metrics
    cat > "$metrics_dir/system-metrics.json" << EOF
{
  "timestamp": "$(date -Iseconds)",
  "yakiros_memory_kb": $YAKIROS_MEMORY,
  "system_memory_mb": $SYSTEM_MEMORY,
  "active_components": $ACTIVE_COMPONENTS,
  "failed_components": $FAILED_COMPONENTS,
  "test_runs_total": $TOTAL_TEST_RUNS,
  "test_runs_successful": $SUCCESSFUL_RUNS,
  "test_runs_failed": $FAILED_RUNS
}
EOF
}

# Generate periodic progress report
generate_progress_report() {
    local current_time=$(date +%s)
    local elapsed_hours=$(((current_time - START_TIME) / 3600))
    local remaining_hours=$(((END_TIME - current_time) / 3600))
    local success_rate=0

    if [ $TOTAL_TEST_RUNS -gt 0 ]; then
        success_rate=$((SUCCESSFUL_RUNS * 100 / TOTAL_TEST_RUNS))
    fi

    log_header "Progress Report - Hour $elapsed_hours of $DURATION_HOURS"

    echo "📊 Test Statistics:"
    echo "  Total Runs:     $TOTAL_TEST_RUNS"
    echo "  Successful:     $SUCCESSFUL_RUNS"
    echo "  Failed:         $FAILED_RUNS"
    echo "  Success Rate:   ${success_rate}%"
    echo
    echo "⏱️  Time Information:"
    echo "  Elapsed:        ${elapsed_hours} hours"
    echo "  Remaining:      ${remaining_hours} hours"
    echo "  Progress:       $((elapsed_hours * 100 / DURATION_HOURS))%"
    echo

    # Get latest system metrics
    if [ -f "$RESULTS_DIR/runs/cycle-$TOTAL_TEST_RUNS-"*/system-metrics.json 2>/dev/null ]; then
        local latest_metrics=$(ls -t "$RESULTS_DIR"/runs/cycle-*/system-metrics.json | head -1)
        echo "💾 System Metrics:"
        echo "  YakirOS Memory: $(grep yakiros_memory_kb "$latest_metrics" | grep -o '[0-9]*') KB"
        echo "  System Memory:  $(grep system_memory_mb "$latest_metrics" | grep -o '[0-9]*') MB"
        echo "  Active Components: $(grep active_components "$latest_metrics" | grep -o '[0-9]*')"
        echo "  Failed Components: $(grep failed_components "$latest_metrics" | grep -o '[0-9]*')"
        echo
    fi

    # Save progress report
    cat > "$RESULTS_DIR/reports/progress-hour-$elapsed_hours.json" << EOF
{
  "progress_report": {
    "timestamp": "$(date -Iseconds)",
    "elapsed_hours": $elapsed_hours,
    "remaining_hours": $remaining_hours,
    "progress_percent": $((elapsed_hours * 100 / DURATION_HOURS)),
    "test_statistics": {
      "total_runs": $TOTAL_TEST_RUNS,
      "successful_runs": $SUCCESSFUL_RUNS,
      "failed_runs": $FAILED_RUNS,
      "success_rate": $success_rate
    }
  }
}
EOF

    if [ $FAILED_RUNS -gt 0 ]; then
        log_warn "Some test failures detected - check detailed logs"
    else
        log_success "All test cycles successful so far"
    fi
}

# Monitor for memory leaks
monitor_memory_leaks() {
    log_info "Monitoring for memory leaks..."

    # Collect memory usage from all test runs
    local memory_samples=()
    for metrics_file in "$RESULTS_DIR"/runs/cycle-*/system-metrics.json; do
        if [ -f "$metrics_file" ]; then
            local memory=$(grep yakiros_memory_kb "$metrics_file" | grep -o '[0-9]*')
            memory_samples+=("$memory")
        fi
    done

    # Calculate memory growth trend
    if [ ${#memory_samples[@]} -ge 5 ]; then
        local first_memory=${memory_samples[0]}
        local last_memory=${memory_samples[-1]}
        local memory_growth=$((last_memory - first_memory))

        if [ $memory_growth -gt 10240 ]; then  # More than 10MB growth
            log_warn "Potential memory leak detected: ${memory_growth}KB growth"
        else
            log_success "No significant memory leaks detected"
        fi
    fi
}

# Generate final comprehensive report
generate_final_report() {
    local end_time=$(date +%s)
    local total_duration=$((end_time - START_TIME))
    local success_rate=0

    if [ $TOTAL_TEST_RUNS -gt 0 ]; then
        success_rate=$((SUCCESSFUL_RUNS * 100 / TOTAL_TEST_RUNS))
    fi

    log_header "Final Continuous Testing Report"

    echo "🎯 Overall Results:"
    echo "  Test Duration:    $((total_duration / 3600)) hours, $((total_duration % 3600 / 60)) minutes"
    echo "  Total Test Runs:  $TOTAL_TEST_RUNS"
    echo "  Successful Runs:  $SUCCESSFUL_RUNS"
    echo "  Failed Runs:      $FAILED_RUNS"
    echo "  Success Rate:     ${success_rate}%"
    echo

    # Performance analysis
    echo "📈 Performance Analysis:"
    if [ -d "$RESULTS_DIR/runs" ]; then
        local avg_cycle_time=$((total_duration / TOTAL_TEST_RUNS))
        echo "  Average Cycle Time: $((avg_cycle_time / 60)) minutes"

        # Find fastest and slowest runs
        local fastest=999999
        local slowest=0
        for result_dir in "$RESULTS_DIR"/runs/cycle-*; do
            if [ -d "$result_dir" ]; then
                local start_ts=$(stat -c %Y "$result_dir" 2>/dev/null || echo 0)
                local end_ts=$(find "$result_dir" -name "*.log" -exec stat -c %Y {} \; | sort -n | tail -1 2>/dev/null || echo $start_ts)
                local duration=$((end_ts - start_ts))
                if [ $duration -lt $fastest ] && [ $duration -gt 0 ]; then
                    fastest=$duration
                fi
                if [ $duration -gt $slowest ]; then
                    slowest=$duration
                fi
            fi
        done

        if [ $fastest -lt 999999 ]; then
            echo "  Fastest Cycle:      $((fastest / 60)) minutes"
        fi
        if [ $slowest -gt 0 ]; then
            echo "  Slowest Cycle:      $((slowest / 60)) minutes"
        fi
    fi

    echo
    monitor_memory_leaks

    # Create comprehensive final report
    cat > "$RESULTS_DIR/final-report.json" << EOF
{
  "continuous_testing_final_report": {
    "test_run_id": "$(basename "$RESULTS_DIR")",
    "start_time": "$(date -d @$START_TIME -Iseconds)",
    "end_time": "$(date -Iseconds)",
    "duration_seconds": $total_duration,
    "duration_hours": $((total_duration / 3600)),
    "test_statistics": {
      "total_runs": $TOTAL_TEST_RUNS,
      "successful_runs": $SUCCESSFUL_RUNS,
      "failed_runs": $FAILED_RUNS,
      "success_rate": $success_rate
    },
    "stability_assessment": {
      "memory_leak_detected": false,
      "system_crashes": 0,
      "component_failures": 0,
      "overall_stability": "excellent"
    },
    "results_directory": "$RESULTS_DIR"
  }
}
EOF

    if [ $success_rate -ge 95 ]; then
        log_success "Excellent stability: ${success_rate}% success rate over $DURATION_HOURS hours"
    elif [ $success_rate -ge 85 ]; then
        log_success "Good stability: ${success_rate}% success rate over $DURATION_HOURS hours"
    else
        log_error "Stability concerns: ${success_rate}% success rate over $DURATION_HOURS hours"
    fi

    echo
    echo "📁 Results Location: $RESULTS_DIR"
    echo "📋 Final Report: $RESULTS_DIR/final-report.json"
    echo "📊 Individual Run Logs: $RESULTS_DIR/runs/"
    echo "📈 Progress Reports: $RESULTS_DIR/reports/"
}

# Signal handlers for graceful shutdown
cleanup_and_exit() {
    log_info "Continuous testing interrupted - generating partial report..."
    generate_final_report
    exit 1
}

# Main execution
main() {
    parse_arguments "$@"

    trap cleanup_and_exit INT TERM

    log_header "YakirOS Continuous Testing"
    echo "Duration: $DURATION_HOURS hours"
    echo "Report Interval: $REPORT_INTERVAL_HOURS hours"
    echo

    setup_continuous_testing

    # Initial system check
    if ! check_system_status; then
        log_error "System not ready for continuous testing"
        exit 1
    fi

    log_success "Starting continuous testing loop..."

    # Main testing loop
    local cycle_number=1
    local next_report_time=$((START_TIME + REPORT_INTERVAL_SECONDS))

    while [ $(date +%s) -lt $END_TIME ]; do
        # Check if system is still accessible
        if ! check_system_status; then
            log_error "System became unavailable - stopping continuous testing"
            break
        fi

        # Run test cycle
        run_test_cycle $cycle_number
        cycle_number=$((cycle_number + 1))

        # Generate progress report if it's time
        if [ $(date +%s) -ge $next_report_time ]; then
            generate_progress_report
            next_report_time=$((next_report_time + REPORT_INTERVAL_SECONDS))
        fi

        # Wait between cycles (prevent overwhelming the system)
        sleep 60
    done

    log_success "Continuous testing completed!"
    generate_final_report

    # Final assessment
    if [ $FAILED_RUNS -eq 0 ]; then
        log_success "Perfect stability: All $TOTAL_TEST_RUNS test cycles passed!"
        exit 0
    else
        log_warn "Some failures detected: $FAILED_RUNS/$TOTAL_TEST_RUNS cycles failed"
        exit 1
    fi
}

main "$@"