# Step 11: VM Integration Testing with QEMU - Implementation Plan

## Overview
Create comprehensive VM-based testing infrastructure that validates the entire YakirOS system with all advanced features implemented in Steps 4-10. This includes hot-swap services, health checks, isolation, CRIU checkpointing, and live kernel upgrades in isolated VM environments.

## Architecture

### 1. Enhanced VM Test Environment
**Goals**: Extended Alpine Linux VM with all testing tools and comprehensive component configurations

**Key Components**:
- Updated VM provisioning with CRIU installation for checkpoint testing
- Multiple test component configurations exercising all YakirOS features
- Automated test runner scripts with comprehensive coverage
- Performance monitoring and benchmarking infrastructure

### 2. Test Scenarios Coverage

#### 2.1 Hot-Swap Services Testing (Step 4 Validation)
- **Echo server with FD-passing**: Maintains client connections during upgrade
- **File server with hot-swap**: Preserves file handles during service upgrade
- **Network service upgrade**: Zero-downtime upgrade with active clients
- **Test**: Continuous client connections while performing hot-swap upgrades

#### 2.2 Health Check System Testing (Step 6 Validation)
- **Command-based health checks**: Services that can pass/fail health checks on demand
- **File-based health monitoring**: Services creating/removing readiness files
- **Signal-based health reporting**: Services sending SIGUSR1 for readiness
- **Degraded state testing**: Services transitioning between ACTIVE ↔ DEGRADED ↔ FAILED
- **Test**: Monitor health transitions and automatic recovery

#### 2.3 Isolation Testing (Step 7 Validation)
- **cgroup resource limits**: Memory, CPU, process count constraints
- **Namespace isolation**: pid, mount, network, uts namespace testing
- **OOM monitoring**: Services that hit memory limits and trigger OOM handling
- **Cleanup verification**: cgroup cleanup after component termination
- **Test**: Resource limit enforcement and proper isolation boundaries

#### 2.4 Graph Analysis Testing (Step 8 Validation)
- **Dependency cycle detection**: Configurations with intentional cycles
- **Complex dependency chains**: Multi-level dependency resolution
- **Graph metrics analysis**: Component count, complexity measurements
- **Validation commands**: Testing all graphctl analysis features
- **Test**: Cycle detection accuracy and graph analysis completeness

#### 2.5 CRIU Checkpoint Testing (Step 9 Validation)
- **Process state preservation**: Long-running services with internal state
- **Three-level fallback**: CRIU → FD-passing → restart chain testing
- **Checkpoint storage management**: Storage usage, cleanup, quotas
- **Metadata persistence**: JSON metadata serialization/deserialization
- **Test**: Complete checkpoint/restore cycles with state validation

#### 2.6 Live Kernel Upgrade Testing (Step 10 Validation)
- **kexec dry-run validation**: Kernel validation without actual execution
- **Seven-phase sequence testing**: All kexec phases in safe dry-run mode
- **System readiness checks**: Prerequisites verification
- **Manifest creation**: Checkpoint manifest generation and persistence
- **Test**: Comprehensive kexec workflow validation (safe mode only)

### 3. Test Components Architecture

#### 3.1 Test Service Types
```toml
# Hot-swap capable echo server
[component]
name = "test-echo-server"
binary = "/usr/local/bin/test-echo-server"
type = "service"
handoff = "fd-passing"

[provides]
capabilities = ["test.echo"]

[requires]
capabilities = ["network.configured"]

# Health check test service
[component]
name = "test-health-service"
binary = "/usr/local/bin/test-health-service"
type = "service"

[provides]
capabilities = ["test.health"]

[lifecycle]
health_check = "/usr/local/bin/health-check-test.sh"
health_interval = 5
health_failures = 2

# Isolated test service
[component]
name = "test-isolated-service"
binary = "/usr/local/bin/test-isolated-service"
type = "service"

[resources]
memory_max = "50M"
cpu_weight = 100
pids_max = 10

[isolation]
namespaces = ["pid", "mount"]
hostname = "isolated-test"

# CRIU checkpoint test service
[component]
name = "test-checkpoint-service"
binary = "/usr/local/bin/test-checkpoint-service"
type = "service"
handoff = "checkpoint"

[checkpoint]
enabled = true
leave_running = true
memory_estimate = 32
```

#### 3.2 Test Binaries and Scripts
- **test-echo-server**: TCP echo server supporting FD-passing handoff
- **test-health-service**: Service that can simulate health failures on command
- **test-isolated-service**: Service demonstrating resource limits and namespaces
- **test-checkpoint-service**: Stateful service perfect for checkpoint/restore testing
- **health-check-test.sh**: Health check script that can be controlled via files
- **test-runner.sh**: Main automated test execution script

### 4. Implementation Phases

#### Phase 1: VM Infrastructure Enhancement (Week 1)
**Files to Create/Modify**:
- `tests/vm/setup-vm-step11.sh` - Enhanced VM provisioning with all test tools
- `tests/vm/yakiros-step11-config/` - Directory with comprehensive test components
- `tests/vm/test-binaries/` - All test service binaries and scripts
- **Deliverable**: VM boots with YakirOS + all test components configured

#### Phase 2: Automated Test Suite (Week 2)
**Files to Create**:
- `tests/vm/test-runner.sh` - Main test orchestration script
- `tests/vm/test-hot-swap.sh` - Hot-swap functionality validation
- `tests/vm/test-health-checks.sh` - Health monitoring system validation
- `tests/vm/test-isolation.sh` - cgroup and namespace validation
- **Deliverable**: Automated test execution with pass/fail reporting

#### Phase 3: Advanced Feature Testing (Week 3)
**Files to Create**:
- `tests/vm/test-graph-analysis.sh` - Cycle detection and graph metrics testing
- `tests/vm/test-checkpointing.sh` - CRIU checkpoint/restore validation
- `tests/vm/test-kexec.sh` - Live kernel upgrade dry-run testing
- `tests/vm/performance-benchmark.sh` - Resource usage and performance monitoring
- **Deliverable**: Complete feature coverage with performance baselines

#### Phase 4: Integration and Reporting (Week 4)
**Files to Create**:
- `tests/vm/test-report-generator.sh` - Comprehensive test result reporting
- `tests/vm/continuous-testing.sh` - Long-running stability testing
- `docs/VM-TESTING.md` - Complete testing documentation and procedures
- **Deliverable**: Production-ready VM testing infrastructure

### 5. Success Criteria

#### Functional Validation
- [ ] All hot-swap upgrades complete without service downtime
- [ ] Health check system correctly detects and recovers from failures
- [ ] Isolation limits are enforced and monitored properly
- [ ] Graph analysis detects all dependency cycles accurately
- [ ] CRIU checkpoint/restore preserves complete process state (when available)
- [ ] kexec dry-run validates all upgrade prerequisites correctly
- [ ] All 100+ existing unit/integration tests continue to pass in VM

#### Performance Benchmarks
- [ ] YakirOS memory usage stays under 50MB with all features active
- [ ] Component startup time averages under 2 seconds with readiness protocol
- [ ] Hot-swap upgrades complete in under 5 seconds with zero client drops
- [ ] Health check overhead less than 1% CPU usage with 20+ monitored services
- [ ] Graph resolution handles 50+ components with complex dependencies under 1 second

#### Reliability Testing
- [ ] 24-hour continuous operation without failures or memory leaks
- [ ] Component restart cycles work correctly 1000+ times
- [ ] Resource limit enforcement prevents system instability
- [ ] VM survives component failures, network issues, and resource exhaustion

### 6. Testing Infrastructure

#### 6.1 VM Configuration
```bash
# VM Specifications for comprehensive testing
VM_RAM="4096"      # 4GB for CRIU and isolation testing
VM_CPUS="4"        # Multi-core for performance testing
VM_DISK="20G"      # Extra space for checkpoints and logs
VM_NET="user,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080,hostfwd=tcp::8081-:8081"
```

#### 6.2 Test Component Matrix
| Component | Hot-Swap | Health Check | Isolation | CRIU | Dependencies |
|-----------|----------|--------------|-----------|------|--------------|
| test-echo-server | ✅ | File | None | ✅ | network |
| test-web-server | ✅ | Command | Memory | ✅ | network, files |
| test-database | ❌ | Signal | CPU+PID | ✅ | storage |
| test-monitor | ❌ | File | Namespace | ❌ | all services |
| test-cycle-a | ❌ | None | None | ❌ | test-cycle-b |
| test-cycle-b | ❌ | None | None | ❌ | test-cycle-a |

#### 6.3 Automated Test Execution
```bash
# Complete test suite execution
./tests/vm/test-runner.sh --comprehensive

# Individual feature testing
./tests/vm/test-runner.sh --hot-swap
./tests/vm/test-runner.sh --health-checks
./tests/vm/test-runner.sh --isolation
./tests/vm/test-runner.sh --checkpointing
./tests/vm/test-runner.sh --performance

# Continuous integration testing
./tests/vm/continuous-testing.sh --duration=24h --report-interval=1h
```

### 7. Documentation and Reporting

#### 7.1 Test Result Format
```json
{
  "yakiros_version": "Step-11-VM-Testing",
  "test_run_id": "20240227-143052",
  "vm_config": {
    "ram_mb": 4096,
    "cpus": 4,
    "os": "Alpine Linux 3.19.1"
  },
  "test_results": {
    "hot_swap": {"passed": 12, "failed": 0, "duration_ms": 45230},
    "health_checks": {"passed": 15, "failed": 0, "duration_ms": 32100},
    "isolation": {"passed": 18, "failed": 0, "duration_ms": 28950},
    "graph_analysis": {"passed": 10, "failed": 0, "duration_ms": 15200},
    "checkpointing": {"passed": 8, "failed": 1, "skipped": 3, "reason": "CRIU not available"},
    "kexec": {"passed": 6, "failed": 0, "duration_ms": 12800}
  },
  "performance_metrics": {
    "yakiros_memory_kb": 47832,
    "component_startup_avg_ms": 1850,
    "hot_swap_avg_ms": 4200,
    "graph_resolution_ms": 850
  }
}
```

This comprehensive Step 11 implementation will validate that YakirOS is production-ready with all advanced features working correctly in real Linux environments.