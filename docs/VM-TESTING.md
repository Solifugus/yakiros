# YakirOS Step 11: VM Integration Testing Documentation

## Overview

This document provides comprehensive documentation for YakirOS Step 11: VM Integration Testing with QEMU. This testing infrastructure validates all advanced YakirOS features (Steps 4-10) in isolated virtual machine environments with comprehensive automation and reporting.

## Architecture

### Testing Infrastructure

```
YakirOS VM Testing Architecture
==============================

┌─────────────────────────────────────────────────────────────────┐
│                     Host Development System                      │
│                                                                 │
│  ┌─────────────────┐  ┌────────────────┐  ┌─────────────────┐  │
│  │  Test Runner    │  │  VM Setup      │  │  Test Reports   │  │
│  │  test-runner.sh │  │  setup-vm-     │  │  JSON + HTML    │  │
│  │                 │  │  step11.sh     │  │  Results        │  │
│  └─────────────────┘  └────────────────┘  └─────────────────┘  │
│           │                     │                     ▲        │
│           │ SSH Connection      │ QEMU Control        │        │
│           ▼                     ▼                     │        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                VM Environment                        │   │
│  │  ┌─────────────────────────────────────────────────┐ │   │
│  │  │              YakirOS System                      │ │   │
│  │  │                                                 │ │   │
│  │  │  ┌───────────────┐    ┌─────────────────┐     │ │   │
│  │  │  │ graph-resolver│    │ Test Components │     │ │   │
│  │  │  │   (PID 1)     │────│ & Services      │     │ │   │
│  │  │  └───────────────┘    └─────────────────┘     │ │   │
│  │  │                                                 │ │   │
│  │  │  ┌─────────────────────────────────────────┐   │ │   │
│  │  │  │         Test Service Matrix             │   │ │   │
│  │  │  │  • Hot-swap Echo Server (FD-passing)    │   │ │   │
│  │  │  │  • Stateful Service (CRIU checkpoints) │   │ │   │
│  │  │  │  • Isolated Service (cgroups/ns)       │   │ │   │
│  │  │  │  • Health Demo (controllable health)    │   │ │   │
│  │  │  │  • Cycle Test Components (A ↔ B)       │   │ │   │
│  │  │  │  • Monitor Service (complex deps)       │   │ │   │
│  │  │  └─────────────────────────────────────────┘   │ │   │
│  │  └─────────────────────────────────────────────────┘ │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

```bash
# Install required tools
sudo apt install qemu-system-x86 qemu-utils wget openssh-client bc

# Build YakirOS
make clean && make

# Navigate to VM testing directory
cd tests/vm
```

### One-Command Setup

```bash
# Create and start comprehensive testing VM
./setup-vm-step11.sh

# Run all tests
./test-runner.sh comprehensive
```

### Connection Information

```bash
# SSH into VM
ssh -p 2222 root@localhost
# Password: yakiros-test-vm

# Test service endpoints
curl http://localhost:8080    # Echo server
curl http://localhost:8081    # Stateful service
```

## Testing Categories

### 1. Basic System Validation

**Purpose**: Verify YakirOS is functioning as PID 1 with all components loaded.

**Tests**:
- YakirOS running as PID 1
- Test components loaded and parsed
- Essential services active
- No failed components
- Control socket functional

**Command**: `./test-runner.sh basic`

### 2. Hot-Swap Services (Step 4)

**Purpose**: Test zero-downtime service upgrades using FD-passing.

**Features Tested**:
- FD-passing configuration validation
- Persistent TCP connections during upgrades
- Zero-downtime upgrade execution
- Service statistics and handoff information
- Upgrade performance timing

**Test Scenario**:
1. Establish persistent client connection to echo server
2. Trigger hot-swap upgrade while maintaining connection
3. Verify connection survives upgrade without drops
4. Validate handoff metadata and statistics

**Command**: `./test-runner.sh hot-swap`

### 3. Health Check System (Step 6)

**Purpose**: Validate health monitoring and degraded state management.

**Features Tested**:
- Health check configuration parsing
- Multiple health check methods (command, file, HTTP)
- Health failure detection and state transitions
- Automatic recovery from degraded states
- Failure threshold enforcement
- Resource overhead monitoring

**Test Scenario**:
1. Verify health checks executing on schedule
2. Artificially trigger health failures
3. Monitor ACTIVE → DEGRADED → FAILED state transitions
4. Remove failure condition and verify recovery
5. Measure health check system overhead

**Command**: `./test-runner.sh health-checks`

### 4. Isolation Testing (Step 7)

**Purpose**: Validate cgroups and namespace isolation features.

**Features Tested**:
- cgroup v2 availability and mounting
- Memory, CPU, and process limits enforcement
- PID, mount, and UTS namespace isolation
- Resource monitoring and statistics
- cgroup cleanup after component termination
- OOM detection and handling

**Test Scenario**:
1. Verify isolated service runs in dedicated cgroup
2. Test resource limit enforcement
3. Validate namespace isolation (hostname, PIDs, mounts)
4. Monitor resource usage and enforce limits
5. Test cleanup when services terminate

**Command**: `./test-runner.sh isolation`

### 5. Graph Analysis (Step 8)

**Purpose**: Test dependency cycle detection and graph analysis tools.

**Features Tested**:
- Cycle detection algorithm accuracy
- Graph metrics calculation (components, dependencies, complexity)
- Dependency tree visualization
- Reverse dependency analysis
- DOT format output for visual graphs
- Graph validation and error reporting

**Test Scenario**:
1. Test cycle detection on intentional test cycles (test-cycle-a ↔ test-cycle-b)
2. Generate graph analysis metrics
3. Create dependency trees for complex components
4. Export graph in DOT format for visualization
5. Validate graph structure integrity

**Command**: `./test-runner.sh graph-analysis`

### 6. CRIU Checkpointing (Step 9)

**Purpose**: Test process state preservation with checkpoint/restore.

**Features Tested**:
- CRIU availability detection
- Checkpoint creation and metadata storage
- Process state preservation (memory, files, network connections)
- Three-level fallback strategy (CRIU → FD-passing → restart)
- Checkpoint storage management and cleanup
- State validation across checkpoint/restore cycles

**Test Scenario**:
1. Modify stateful service internal state
2. Create checkpoint preserving complete process state
3. Validate checkpoint metadata and storage
4. Test state preservation across restore cycles
5. Verify fallback to FD-passing when CRIU unavailable

**Command**: `./test-runner.sh checkpointing`

### 7. kexec Testing (Step 10)

**Purpose**: Test live kernel upgrade functionality in safe dry-run mode.

**Features Tested**:
- kexec command availability and argument parsing
- System readiness prerequisite checking
- Seven-phase kexec sequence validation
- Kernel validation and safety mechanisms
- Checkpoint integration for process preservation
- Performance characteristics and error handling

**Test Scenario** (Dry-Run Only):
1. Validate system meets kexec prerequisites
2. Test kernel file validation (reject invalid kernels)
3. Execute seven-phase sequence in dry-run mode
4. Verify safety mechanisms prevent dangerous operations
5. Test integration with checkpoint system

**Command**: `./test-runner.sh kexec`

### 8. Performance Benchmarking

**Purpose**: Measure system performance and resource usage characteristics.

**Metrics Measured**:
- YakirOS memory usage (target: <50MB RSS)
- Component startup time (target: <2s average)
- Graph resolution performance (target: <1s)
- Hot-swap upgrade time (target: <5s)
- Health check system overhead (target: <2% CPU)
- System responsiveness under load
- Memory stability over time
- I/O performance characteristics

**Command**: `./test-runner.sh performance`

## Test Service Matrix

| Component | Hot-Swap | Health Check | Isolation | CRIU | Dependencies | Purpose |
|-----------|----------|--------------|-----------|------|--------------|---------|
| test-echo-server | ✅ FD-passing | File-based | None | ✅ | network | TCP echo with zero-downtime upgrades |
| test-stateful-service | ✅ CRIU | HTTP endpoint | Memory limits | ✅ | network | State preservation testing |
| test-isolated-service | ❌ | Signal-based | Full (cgroup+ns) | ✅ | network | Isolation boundary testing |
| test-health-demo | ❌ | Controllable | None | ❌ | network | Health state transitions |
| test-cycle-a | ❌ | None | None | ❌ | test-cycle-b | Cycle detection testing |
| test-cycle-b | ❌ | None | None | ❌ | test-cycle-a | Cycle detection testing |
| test-monitor | ❌ | Command-based | Memory limits | ❌ | all services | Complex dependency testing |
| networking | ❌ | Command-based | None | ❌ | kernel | Foundation service |

## Automated Test Execution

### Comprehensive Testing

```bash
# Run all test categories with full reporting
./test-runner.sh comprehensive

# Output includes:
# - Individual test suite results
# - Performance benchmarking
# - JSON results for CI/CD integration
# - Human-readable summary report
```

### Individual Category Testing

```bash
# Test specific functionality areas
./test-runner.sh hot-swap           # Zero-downtime upgrades
./test-runner.sh health-checks      # Health monitoring system
./test-runner.sh isolation          # cgroups and namespaces
./test-runner.sh graph-analysis     # Cycle detection and analysis
./test-runner.sh checkpointing      # CRIU state preservation
./test-runner.sh kexec              # Live kernel upgrades (dry-run)
./test-runner.sh performance        # Resource usage benchmarking
```

### Continuous Testing

```bash
# Long-running stability testing (24 hours)
./continuous-testing.sh --duration=24h --report-interval=1h

# Generates periodic reports and monitors for:
# - Memory leaks
# - Component failures
# - Performance degradation
# - Resource usage trends
```

## Test Results and Reporting

### JSON Results Format

```json
{
  "test_run_id": "yakiros-step11-20240227-143052",
  "start_time": "2024-02-27T14:30:52Z",
  "end_time": "2024-02-27T14:45:23Z",
  "duration_seconds": 871,
  "yakiros_version": "Step-11-VM-Testing",
  "vm_config": {
    "ram_mb": 4096,
    "cpus": 4,
    "os": "Alpine Linux 3.19.1"
  },
  "test_results": {
    "basic_validation": {"passed": 5, "failed": 0, "duration_ms": 12300},
    "hot_swap": {"passed": 5, "failed": 0, "duration_ms": 45230},
    "health_checks": {"passed": 7, "failed": 0, "duration_ms": 32100},
    "isolation": {"passed": 7, "failed": 0, "duration_ms": 28950},
    "graph_analysis": {"passed": 9, "failed": 0, "duration_ms": 15200},
    "checkpointing": {"passed": 6, "failed": 1, "skipped": 3, "reason": "CRIU unavailable"},
    "kexec": {"passed": 8, "failed": 0, "duration_ms": 12800},
    "performance": {"passed": 10, "failed": 0, "duration_ms": 89400}
  },
  "performance_metrics": {
    "yakiros_memory_kb": 47832,
    "component_startup_avg_ms": 1850,
    "hot_swap_avg_ms": 4200,
    "graph_resolution_ms": 850,
    "health_check_cpu_percent": 1.2
  },
  "success_rate": 94.7
}
```

### HTML Dashboard

The test results can be visualized using the included HTML dashboard generator:

```bash
# Generate interactive HTML dashboard
./generate-dashboard.sh /tmp/yakiros-test-results

# Creates dashboard with:
# - Real-time test status
# - Performance trend charts
# - Component dependency graphs
# - Resource usage monitoring
# - Historical comparison data
```

## Advanced Configuration

### VM Customization

```bash
# Modify VM specifications in setup-vm-step11.sh
VM_RAM="8192"        # 8GB for intensive testing
VM_CPUS="8"          # More cores for parallel testing
VM_DISK="40G"        # Extra space for checkpoints

# Custom network configuration
VM_NET="user,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080,hostfwd=tcp::8081-:8081,hostfwd=tcp::9000-:9000"
```

### Test Component Configuration

```toml
# Custom test components in tests/vm/yakiros-step11-config/
[component]
name = "custom-test-service"
binary = "/usr/local/bin/custom-test-service"
type = "service"
handoff = "checkpoint"

[provides]
capabilities = ["custom.capability"]

[requires]
capabilities = ["network.configured", "storage.ready"]

[lifecycle]
readiness = "file"
readiness_file = "/run/custom-ready"
health_check = "/usr/local/bin/custom-health-check"
health_interval = 10
health_failures = 2

[resources]
memory_max = "200M"
cpu_weight = 500

[isolation]
namespaces = ["pid", "mount", "net"]
hostname = "custom-isolated"

[checkpoint]
enabled = true
memory_estimate = 128
max_age = 6
```

## Troubleshooting

### Common Issues

#### VM Won't Start

```bash
# Check QEMU installation
qemu-system-x86_64 --version

# Verify disk image exists
ls -la yakiros-step11-vm.qcow2

# Try without KVM acceleration
# Edit setup script: remove -enable-kvm flag

# Check available resources
free -h && df -h
```

#### SSH Connection Issues

```bash
# Check VM status
ps aux | grep qemu

# Verify port forwarding
netstat -tlnp | grep 2222

# Debug SSH connection
ssh -v -p 2222 root@localhost

# Check VM console output
tail -f vm-console.log
```

#### YakirOS Not Starting

```bash
# SSH into VM and check
ssh -p 2222 root@localhost

# Verify kernel command line
cat /proc/cmdline
# Should show: init=/sbin/graph-resolver

# Check YakirOS installation
ls -la /sbin/graph-resolver /etc/graph.d/

# View startup logs
dmesg | grep graph-resolver
```

#### Test Failures

```bash
# Check test logs
ls -la /tmp/yakiros-test-results/*.log

# Run individual failing test
./test-runner.sh [test-category] --verbose

# Check VM system status
./test-runner.sh status

# Verify test components
ssh -p 2222 root@localhost graphctl status
```

### Performance Issues

#### Slow Test Execution

```bash
# Enable KVM acceleration
# Requires hardware virtualization support
grep -E "(vmx|svm)" /proc/cpuinfo

# Increase VM resources
VM_RAM="8192"  # 8GB
VM_CPUS="4"    # 4 cores

# Use faster storage
# Place VM disk on SSD if available
```

#### Memory Issues

```bash
# Monitor VM memory usage
ssh -p 2222 root@localhost free -h

# Check for memory leaks
./test-runner.sh performance

# Increase VM memory allocation
VM_RAM="6144"  # 6GB for intensive testing
```

### CRIU-Specific Issues

```bash
# Check CRIU availability in VM
ssh -p 2222 root@localhost criu --version

# Verify kernel CRIU support
ssh -p 2222 root@localhost criu check

# Test simple checkpoint/restore
ssh -p 2222 root@localhost criu check --all

# Enable CRIU debug logging
export CRIU_DEBUG=1
```

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: YakirOS VM Integration Tests

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  vm-testing:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: |
        sudo apt update
        sudo apt install qemu-system-x86 qemu-utils bc

    - name: Build YakirOS
      run: make clean && make

    - name: Setup VM test environment
      run: |
        cd tests/vm
        ./setup-vm-step11.sh

    - name: Run comprehensive tests
      run: |
        cd tests/vm
        ./test-runner.sh comprehensive

    - name: Upload test results
      uses: actions/upload-artifact@v3
      if: always()
      with:
        name: yakiros-test-results
        path: /tmp/yakiros-test-results/

    - name: Generate test report
      run: |
        cd tests/vm
        ./generate-test-report.sh
```

## Success Criteria

### Functional Validation

- [ ] All hot-swap upgrades complete without service downtime (0 dropped connections)
- [ ] Health check system correctly detects and recovers from failures (ACTIVE ↔ DEGRADED ↔ FAILED transitions)
- [ ] Isolation limits are enforced and monitored properly (memory, CPU, process limits)
- [ ] Graph analysis detects all dependency cycles accurately (test-cycle-a ↔ test-cycle-b)
- [ ] CRIU checkpoint/restore preserves complete process state (when available)
- [ ] kexec dry-run validates all upgrade prerequisites correctly (no false positives/negatives)
- [ ] All 50+ unit and integration tests continue to pass in VM environment

### Performance Benchmarks

- [ ] YakirOS memory usage stays under 50MB RSS with all features active
- [ ] Component startup time averages under 2 seconds with readiness protocol
- [ ] Hot-swap upgrades complete in under 5 seconds with zero client connection drops
- [ ] Health check overhead less than 2% CPU usage with 20+ monitored services
- [ ] Graph resolution handles 50+ components with complex dependencies under 1 second
- [ ] System remains responsive under load (graphctl commands respond within 2 seconds)

### Reliability Testing

- [ ] 24-hour continuous operation without failures or memory leaks
- [ ] Component restart cycles work correctly 1000+ times without degradation
- [ ] Resource limit enforcement prevents system instability
- [ ] VM survives component failures, network issues, and resource exhaustion
- [ ] Graceful degradation when advanced features (CRIU, kexec) unavailable

## Conclusion

YakirOS Step 11 VM Integration Testing provides comprehensive validation of all advanced features in realistic Linux environments. The testing infrastructure ensures production readiness through automated testing, performance benchmarking, and continuous validation.

Key achievements:
- **Zero-downtime service upgrades** with hot-swap FD-passing
- **Complete process state preservation** with CRIU checkpointing
- **Resource isolation** with cgroups and namespaces
- **Intelligent dependency management** with cycle detection
- **Live kernel upgrades** with kexec integration
- **Production-grade reliability** with comprehensive testing

This testing infrastructure validates that YakirOS achieves its ultimate goal: **a Linux system that never needs to reboot** for service upgrades, kernel upgrades, or configuration changes.