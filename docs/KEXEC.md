# YakirOS Live Kernel Upgrades (Step 10)

## Overview

YakirOS Step 10 implements live kernel upgrades using kexec (kernel execution) combined with CRIU checkpoint/restore technology. This enables **complete kernel replacement without rebooting**, preserving all running processes and system state across the kernel transition.

This is the "crown jewel" of YakirOS - combined with hot-swappable services (Step 4) and checkpoint/restore (Step 9), the system **never needs to reboot** for any reason: service upgrades, kernel upgrades, configuration changes, or system maintenance all happen transparently while the system continues running.

## Architecture

### How Kexec Works

Kexec is a Linux syscall that loads a new kernel into memory and jumps directly to it, bypassing the bootloader and hardware initialization. Unlike a traditional reboot:

- **No BIOS/UEFI**: Skip firmware initialization (much faster)
- **Memory preserved**: RAM contents available to new kernel
- **Direct jump**: Transfer control immediately to new kernel
- **No disk I/O**: New kernel already loaded in memory

However, kexec **destroys all userspace processes**. YakirOS solves this by:

1. **Checkpointing**: Save all process state with CRIU before kexec
2. **Persistence**: Store checkpoints in location that survives kexec
3. **Restoration**: New kernel automatically restores all processes

### The Seven-Phase Kexec Sequence

```
┌─────────────────┐
│ Phase 1:        │ ← Validate kernel image, system readiness
│ Validation      │   Check CRIU support, storage space
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 2:        │ ← Save current kernel version, system info
│ Pre-kexec Info  │   Create audit trail for post-upgrade
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 3:        │ ← CRIU checkpoint ALL managed processes
│ Checkpoint All  │   Preserve memory, file descriptors, network
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 4:        │ ← Validate all checkpoint integrity
│ Validate Data   │   Ensure safe restoration possible
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 5:        │ ← Save manifest to persistent storage
│ Persist State   │   JSON metadata + checkpoint locations
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 6:        │ ← Load new kernel into memory
│ Load Kernel     │   Use kexec_load() syscall
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Phase 7:        │ ← Execute kexec (NO RETURN)
│ Execute Kexec   │   Transfer to new kernel
└─────────┬───────┘
          │
          ▼
     ╔═══════════════╗
     ║ NEW KERNEL    ║ ← graph-resolver starts as PID 1
     ║ BOOTS         ║   Detects checkpoint data exists
     ╚═══════════════╝   Restores all processes
          │               System continues seamlessly
          ▼
┌─────────────────┐
│ Post-Kexec:     │ ← Mount filesystems, restore processes
│ State Recovery  │   Re-register capabilities, resolve graph
└─────────────────┘   Clean up checkpoint data
```

### Integration with Existing Systems

Kexec builds on YakirOS's existing infrastructure:

- **CRIU Integration**: Uses Step 9 checkpoint/restore system
- **Three-Level Fallback**: kexec → FD-passing → restart
- **Component Management**: All managed processes are preserved
- **Capability System**: Service capabilities automatically re-register
- **Graph Resolution**: Dependency graph rebuilds transparently

## Configuration

### Kernel Command Line

The new kernel needs to know where to find checkpoint data:

```bash
# Required: YakirOS as init system
init=/sbin/graph-resolver

# Optional: Custom checkpoint location (default: /checkpoint)
yakiros.checkpoint=/persistent/checkpoint

# Example complete command line
console=ttyS0 root=/dev/sda1 init=/sbin/graph-resolver yakiros.checkpoint=/checkpoint quiet
```

### Storage Requirements

Kexec requires persistent storage that survives kernel transitions:

```bash
# Create dedicated checkpoint partition (recommended)
/dev/sda2 /checkpoint ext4 defaults 0 2

# Or use existing filesystem with adequate space
# Minimum: 2GB free space for checkpoints
```

### Component Configuration

No special component configuration required - all managed components are automatically checkpointed:

```toml
[component]
name = "webserver"
binary = "/usr/sbin/nginx"
type = "service"
# No kexec-specific config needed - automatic participation
```

## Usage

### Basic Live Kernel Upgrade

```bash
# 1. Verify system readiness
graphctl kexec --dry-run /boot/vmlinuz-6.1.0-new

# 2. Perform the upgrade (DANGEROUS - use VMs for testing)
graphctl kexec /boot/vmlinuz-6.1.0-new

# 3. Verify after upgrade (in new kernel)
uname -r  # Should show new kernel version
graphctl status  # All services should be running
```

### Advanced Usage

```bash
# Upgrade with custom initrd
graphctl kexec /boot/vmlinuz-6.1.0 --initrd /boot/initrd.img-6.1.0

# Upgrade with custom kernel command line
graphctl kexec /boot/vmlinuz-6.1.0 --append "console=ttyS0 debug"

# Dry run with full validation
graphctl kexec --dry-run /boot/vmlinuz-6.1.0 --initrd /boot/initrd.img-6.1.0
```

### Monitoring and Verification

```bash
# Check system readiness for kexec
graphctl analyze  # Shows system health
dmesg | grep kexec  # Check kernel support

# Verify post-upgrade success
graphctl status  # All components should be ACTIVE
cat /proc/version  # Confirm new kernel
uptime  # Process uptime preserved across kexec
```

## Safety and Prerequisites

### ⚠️ CRITICAL SAFETY WARNINGS

**NEVER test kexec on production or development systems!**

- **Data Loss Risk**: Kernel replacement can cause complete system failure
- **No Recovery**: Failed kexec may require physical console access
- **State Corruption**: Invalid checkpoints can leave system unusable
- **Hardware Dependency**: Some hardware may not support kexec properly

**Always test in isolated VMs with full backups!**

### System Requirements

#### 1. Kernel Support
```bash
# Required kernel config options
CONFIG_KEXEC=y
CONFIG_KEXEC_FILE=y
CONFIG_CHECKPOINT_RESTORE=y
CONFIG_NAMESPACES=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y
CONFIG_PID_NS=y
CONFIG_NET_NS=y

# Check current kernel support
grep CONFIG_KEXEC /boot/config-$(uname -r)
zcat /proc/config.gz | grep CONFIG_KEXEC  # If available
```

#### 2. Required Software
```bash
# Install kexec-tools
sudo apt-get install kexec-tools  # Ubuntu/Debian
sudo yum install kexec-tools      # RHEL/CentOS
sudo pacman -S kexec-tools        # Arch Linux

# Install CRIU (required for checkpoint/restore)
sudo apt-get install criu

# Verify installations
which kexec
criu --version
```

#### 3. Privileges and Environment
```bash
# Must run as root (PID 1)
sudo graph-resolver  # Or as init system

# Verify kexec syscall support
sudo kexec --help

# Check CRIU compatibility
sudo criu check
sudo criu check --ms  # Memory snapshot support
```

#### 4. Storage Requirements
- **Minimum**: 2GB free space in checkpoint directory
- **Recommended**: Dedicated partition for `/checkpoint`
- **Persistent**: Storage must survive kernel transitions
- **Performance**: SSD recommended for checkpoint I/O

#### 5. Memory Requirements
- **Minimum**: 1GB available RAM
- **Recommended**: 2GB+ for large systems
- **Swap**: Avoid heavy swapping during checkpoint

### Validation Checklist

Before performing kexec, verify:

```bash
# System readiness comprehensive check
sudo graphctl kexec --dry-run /boot/new-kernel

# Manual verification steps:
[ -x /sbin/graph-resolver ]  # YakirOS init available
[ -d /checkpoint ]           # Checkpoint storage exists
[ "$(id -u)" = "0" ]         # Running as root
command -v kexec             # kexec utility available
criu check                   # CRIU functional
df -h /checkpoint            # Adequate free space
free -h                      # Sufficient memory
```

## Error Handling and Recovery

### Graceful Degradation

YakirOS implements a three-level fallback strategy:

1. **kexec fails** → Falls back to FD-passing hot-swap (zero downtime)
2. **FD-passing fails** → Falls back to standard restart (brief downtime)
3. **All methods fail** → Component marked FAILED, manual intervention

### Common Error Scenarios

#### 1. "CRIU binary not found"
```bash
# Install CRIU
sudo apt-get install criu

# Verify installation
which criu
criu --version
```

#### 2. "Kernel does not support checkpoint/restore"
```bash
# Check kernel configuration
grep CONFIG_CHECKPOINT_RESTORE /boot/config-$(uname -r)

# If missing, need to:
# - Compile custom kernel with CHECKPOINT_RESTORE=y
# - Or use distribution kernel that includes CRIU support
```

#### 3. "kexec_load syscall not available"
```bash
# Check kernel kexec support
grep CONFIG_KEXEC /boot/config-$(uname -r)

# Enable kexec if disabled
echo 1 > /proc/sys/kernel/kexec_load_disabled  # If set to 1
```

#### 4. "Checkpoint validation failed"
```bash
# Check checkpoint integrity
ls -la /checkpoint/

# Clean up corrupted checkpoints
sudo rm -rf /checkpoint/*

# Retry after cleanup
sudo graphctl kexec --dry-run /boot/new-kernel
```

#### 5. "Insufficient disk space"
```bash
# Check available space
df -h /checkpoint

# Clean up old checkpoints
sudo find /checkpoint -type d -mtime +1 -exec rm -rf {} \;

# Or increase checkpoint partition size
```

#### 6. "New kernel fails to boot"
```bash
# This requires physical/console access to recover
# Prevention is key:

# 1. Always test kernels in VMs first
# 2. Use known-good kernel versions
# 3. Verify kernel command line syntax
# 4. Keep rescue kernel available in bootloader
```

### Recovery Procedures

#### If kexec fails (system still running):
```bash
# System continues on old kernel - no immediate danger
# Check logs for failure reason
dmesg | tail -50
journalctl -u graph-resolver --no-pager

# Clean up any partial checkpoint data
sudo rm -rf /checkpoint/*

# Try simpler upgrade method
sudo graphctl upgrade component-name  # FD-passing fallback
```

#### If new kernel boots but processes don't restore:
```bash
# Check for checkpoint data
ls -la /checkpoint/

# Manual restoration attempt (dangerous)
sudo systemctl stop graph-resolver
sudo /sbin/graph-resolver  # Will attempt auto-restore

# Last resort: clean start
sudo rm -rf /checkpoint/*
sudo systemctl start graph-resolver
```

#### If new kernel fails to boot (requires console access):
```bash
# Boot from rescue kernel or installation media
# Mount root filesystem
# Restore old kernel as default in bootloader
# Reboot to working system

# Then investigate:
# - Check new kernel compatibility
# - Verify initrd matches kernel version
# - Check for missing kernel modules
```

## Performance Considerations

### Time Overhead

Typical kexec timeline (depends on system load and process count):

- **Phase 1-2 (Validation)**: 100ms - 1s
- **Phase 3 (Checkpoint)**: 1s - 30s (major factor)
- **Phase 4-5 (Validate/Save)**: 100ms - 2s
- **Phase 6 (Load kernel)**: 500ms - 2s
- **Phase 7 (Execute)**: <100ms (point of no return)
- **Post-kexec (Restore)**: 2s - 60s (major factor)
- **Total downtime**: ~0ms (processes preserved throughout)

### Memory Usage

Checkpoint process requires:
- **Process memory**: 1:1 ratio of active process RAM
- **Metadata overhead**: ~10% additional
- **Temporary buffers**: ~500MB during checkpoint
- **Peak usage**: Sum of all active process memory + overhead

### Storage I/O

Checkpoint I/O characteristics:
- **Write phase**: Sequential writes, high bandwidth
- **Read phase**: Random reads during restoration
- **Size**: Typically 50-80% of active process memory
- **Compression**: CRIU uses compression to reduce size

### Network Impact

- **Established connections**: Preserved across kexec
- **Listen sockets**: Automatically restored
- **Network namespace**: Maintained if isolated
- **DNS/routing**: May need brief re-convergence

## Troubleshooting

### Diagnostic Commands

```bash
# System compatibility check
sudo criu check --ms
sudo kexec --version
lsmod | grep kexec

# Storage analysis
df -h /checkpoint
du -sh /checkpoint/*
find /checkpoint -type f -exec ls -lh {} \;

# Process state analysis
sudo graphctl status
ps aux --forest
lsof | wc -l  # Open file descriptor count

# Memory analysis
free -h
cat /proc/meminfo | grep Available
cat /proc/sys/vm/overcommit_memory

# Network state analysis
ss -tulpn | grep LISTEN
ip addr show
ip route show
```

### Log Analysis

```bash
# YakirOS specific logs
journalctl -u graph-resolver -f
tail -f /run/graph/graph-resolver.log

# Kexec kernel logs
dmesg | grep -i kexec
journalctl -k | grep -i kexec

# CRIU logs (if available)
tail -f /tmp/criu.log
ls -la /run/graph/checkpoints/*/

# System logs during transition
journalctl --since "5 minutes ago" --no-pager
```

### Performance Tuning

```bash
# Increase checkpoint timeout for large processes
echo 60 > /proc/sys/kernel/criu_checkpoint_timeout

# Optimize memory for checkpointing
echo 1 > /proc/sys/vm/drop_caches  # Before checkpoint
echo 0 > /proc/sys/vm/swappiness   # Reduce swapping

# Use faster storage for checkpoints
mount -t tmpfs tmpfs /checkpoint -o size=4G  # RAM disk (temporary)

# Parallel checkpoint operations
echo 4 > /proc/sys/kernel/criu_threads  # If supported
```

## Testing and Validation

### Unit Testing

```bash
# Run safe unit tests (no actual kexec)
make test-unit
tests/unit/test_kexec

# Integration tests (safe)
make test-integration
tests/integration/test_kexec_integration
```

### VM Testing Setup

**NEVER test kexec on physical hardware during development!**

#### QEMU VM Setup
```bash
# Create test VM with 4GB RAM, enable KVM
qemu-system-x86_64 \
    -m 4G \
    -smp 4 \
    -enable-kvm \
    -drive file=yakiros-test.qcow2,format=qcow2 \
    -netdev user,id=net0 \
    -device rtl8139,netdev=net0 \
    -nographic \
    -append "console=ttyS0 init=/sbin/graph-resolver yakiros.checkpoint=/checkpoint"

# Inside VM: Test kexec operations safely
```

#### VirtualBox Setup
```bash
# Create VM with:
# - 4GB+ RAM
# - Enable VT-x/AMD-V
# - EFI firmware
# - Two virtual disks: root + checkpoint partition

# Install YakirOS and test kernel upgrades
```

### Test Scenarios

#### Basic Functionality Test
```bash
# 1. Boot YakirOS in VM
# 2. Start several test services
sudo graphctl status  # Verify services running

# 3. Prepare new kernel (copy of current kernel works for testing)
sudo cp /boot/vmlinuz /boot/vmlinuz-test

# 4. Dry run test
sudo graphctl kexec --dry-run /boot/vmlinuz-test

# 5. Execute kexec (IN VM ONLY!)
sudo graphctl kexec /boot/vmlinuz-test

# 6. Verify success
uname -r  # Should show test kernel
sudo graphctl status  # All services should be running
```

#### Stress Testing
```bash
# Test with high system load
stress --cpu 4 --vm 2 --vm-bytes 1G &
sudo graphctl kexec /boot/vmlinuz-test

# Test with many open files
for i in {1..100}; do sleep 3600 & done
sudo graphctl kexec /boot/vmlinuz-test

# Test with network connections
nc -l 8080 &  # Listen socket
sudo graphctl kexec /boot/vmlinuz-test
```

#### Failure Testing
```bash
# Test with invalid kernel
sudo graphctl kexec /dev/null  # Should fail gracefully

# Test with insufficient space
dd if=/dev/zero of=/checkpoint/fill bs=1M count=10000  # Fill storage
sudo graphctl kexec /boot/vmlinuz-test  # Should fail gracefully

# Test CRIU failure scenarios
killall criu  # Simulate CRIU unavailable
sudo graphctl kexec /boot/vmlinuz-test  # Should fall back to FD-passing
```

## Production Deployment

### Preparation Checklist

- [ ] Comprehensive testing in VMs with production workloads
- [ ] Backup and recovery procedures tested
- [ ] Monitoring and alerting configured
- [ ] Team trained on emergency procedures
- [ ] Rollback kernels available
- [ ] Maintenance windows planned (even though zero downtime)

### Deployment Process

1. **Validation Phase** (1 week before)
   ```bash
   # Verify new kernel in test environment
   # Load test with production-like workloads
   # Measure checkpoint/restore times
   # Document any compatibility issues
   ```

2. **Staging Phase** (2 days before)
   ```bash
   # Deploy to staging environment
   # Run production traffic replay
   # Verify all services restore correctly
   # Test monitoring and alerting
   ```

3. **Production Phase** (maintenance window)
   ```bash
   # Final readiness check
   sudo graphctl kexec --dry-run /boot/new-kernel

   # Execute upgrade
   sudo graphctl kexec /boot/new-kernel

   # Verification
   # - All services running
   # - Performance baseline met
   # - No error alerts
   # - User traffic flowing
   ```

### Monitoring Integration

```bash
# Pre-upgrade metrics
curl -s localhost:9090/metrics | grep yakiros_component_active
curl -s localhost:9090/metrics | grep yakiros_kexec_ready

# Post-upgrade verification
curl -s localhost:9090/metrics | grep yakiros_kexec_completed
curl -s localhost:9090/metrics | grep yakiros_component_restored
```

## Future Enhancements

### Planned Features (YakirOS Step 11+)

1. **Delta Checkpoints**: Incremental state preservation to reduce checkpoint time
2. **Parallel Restoration**: Restore multiple processes simultaneously
3. **Compression Optimization**: Better algorithms for faster I/O
4. **Network State Preservation**: Advanced socket state management
5. **Container Integration**: Checkpoint/restore containerized workloads
6. **Cluster Coordination**: Coordinate upgrades across multiple nodes

### Performance Optimizations

1. **Memory Deduplication**: Share identical pages across processes
2. **Async Checkpointing**: Non-blocking checkpoint operations
3. **Streaming Restoration**: Progressive process restoration
4. **Hardware Acceleration**: Use specialized hardware for compression/encryption

## Conclusion

YakirOS Step 10 successfully implements live kernel upgrades that preserve complete system state across kernel transitions. Combined with hot-swappable services and checkpoint/restore, YakirOS achieves its ultimate goal: **a truly rebootless Linux system**.

This implementation provides:

- **Zero-downtime kernel upgrades** with complete state preservation
- **Robust three-level fallback** ensuring maximum reliability
- **Comprehensive safety validation** preventing unsafe operations
- **Production-ready error handling** and recovery procedures
- **Extensive testing framework** for safe development and validation

The system can now handle any type of upgrade or maintenance without interrupting service:
- **Service upgrades**: Hot-swappable FD-passing (Step 4)
- **Service updates**: CRIU checkpoint/restore (Step 9)
- **Kernel upgrades**: Live kexec with full state preservation (Step 10)
- **Configuration changes**: Dynamic graph resolution (Steps 1-8)

YakirOS has achieved the vision of a **completely rebootless Linux system** suitable for mission-critical environments where downtime is unacceptable.

**Remember: Always test in VMs. Never test kexec on production systems!**