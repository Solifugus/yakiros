# YakirOS Checkpoint System (Step 9)

## Overview

YakirOS Step 9 implements CRIU (Checkpoint/Restore In Userspace) integration for complete process state preservation during hot-swap operations. This extends the existing file descriptor passing mechanism to provide true zero-downtime upgrades with full memory state continuity.

## Architecture

### Three-Level Fallback Strategy

YakirOS implements a robust three-level fallback strategy for component upgrades:

1. **Level 1: CRIU Checkpoint/Restore** - Full state preservation
2. **Level 2: FD-Passing Hot-swap** - Zero-downtime, state loss
3. **Level 3: Standard Restart** - Brief downtime, full state loss

This ensures maximum reliability while providing the best possible user experience when CRIU is available.

```
┌─────────────────┐
│ Component       │
│ Upgrade Request │
└─────────┬───────┘
          │
          ▼
┌─────────────────┐     ┌──────────────┐     ┌─────────────┐
│ CRIU Checkpoint │────▶│ FD-Passing   │────▶│ Restart     │
│ (Full State)    │     │ (Zero Down)  │     │ (Brief Down)│
│                 │     │              │     │             │
│ • Memory state  │     │ • File desc. │     │ • Clean     │
│ • Open files    │     │ • Network    │     │   start     │
│ • Network conn. │     │ • Sockets    │     │ • Full      │
│ • Process tree  │     │              │     │   downtime  │
└─────────────────┘     └──────────────┘     └─────────────┘
```

### Component Configuration

Components can be configured for checkpoint support via TOML:

```toml
[component]
handoff = "checkpoint"

[checkpoint]
enabled = true
preserve_fds = "network,filesystem"
leave_running = true
memory_estimate = 256
max_age = 24
```

## Storage Architecture

### Directory Structure

```
/run/graph/checkpoints/           # Temporary checkpoint storage
├── component-name/
│   ├── timestamp-1/
│   │   ├── metadata.json        # Checkpoint metadata
│   │   ├── core-*.img           # Process core images
│   │   ├── mm-*.img             # Memory mappings
│   │   ├── pagemap-*.img        # Memory pages
│   │   └── fs-*.img             # File system state
│   └── timestamp-2/
└── ...

/var/lib/graph/checkpoints/       # Persistent checkpoint storage
└── (same structure as /run)
```

### Metadata Format

Each checkpoint includes JSON metadata for validation and management:

```json
{
  "component_name": "webserver",
  "original_pid": 12345,
  "timestamp": 1640995200,
  "image_size": 1048576,
  "capabilities": "web.http,web.https",
  "criu_version": {
    "major": 3,
    "minor": 16,
    "patch": 1
  },
  "leave_running": true,
  "preserve_fds": "network,filesystem"
}
```

## graphctl Commands

### Basic Operations

```bash
# Create checkpoint of running component
graphctl checkpoint webserver

# Restore from latest checkpoint
graphctl restore webserver

# Restore from specific checkpoint
graphctl restore webserver 1640995200

# List available checkpoints
graphctl checkpoint-list webserver
graphctl checkpoint-list              # All components

# Remove specific checkpoint
graphctl checkpoint-rm webserver 1640995200

# Prepare component for migration
graphctl migrate webserver
```

### Advanced Management

```bash
# Upgrade with automatic fallback
graphctl upgrade webserver           # Uses three-level strategy

# Manual checkpoint cleanup
graphctl checkpoint-cleanup webserver

# Storage usage analysis
graphctl checkpoint-usage webserver
```

## CRIU Requirements

### System Dependencies

1. **CRIU Installation**:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install criu

   # RHEL/CentOS
   sudo yum install criu

   # From source
   git clone https://github.com/checkpoint-restore/criu
   cd criu && make && sudo make install
   ```

2. **Kernel Support**:
   - `CONFIG_CHECKPOINT_RESTORE=y`
   - `CONFIG_NAMESPACES=y`
   - `CONFIG_UTS_NS=y`
   - `CONFIG_IPC_NS=y`
   - `CONFIG_PID_NS=y`
   - `CONFIG_NET_NS=y`
   - `CONFIG_EXPERT=y`
   - `CONFIG_IA32_EMULATION=y` (x86_64)

3. **Permissions**:
   - CAP_SYS_ADMIN capability
   - Access to /proc/sys/kernel/ns_last_pid
   - Access to /proc/sys/kernel/pid_max

### Compatibility Check

YakirOS automatically detects CRIU availability:

```bash
# Check CRIU support
criu check
criu check --ms           # Memory snapshot support
criu check --restore-detached
```

## Implementation Details

### Core Modules

#### 1. `src/checkpoint.h/c`
Low-level CRIU wrapper functions:
- `criu_is_supported()` - Detect CRIU availability
- `criu_checkpoint_process()` - Checkpoint a process
- `criu_restore_process()` - Restore from checkpoint
- `checkpoint_validate_image()` - Validate checkpoint integrity

#### 2. `src/checkpoint-mgmt.h/c`
Storage lifecycle and metadata management:
- `checkpoint_save_metadata()` - Persist checkpoint info
- `checkpoint_list_checkpoints()` - Enumerate available checkpoints
- `checkpoint_cleanup()` - Garbage collection
- `checkpoint_storage_usage()` - Quota management

#### 3. Modified `src/component.c`
Extended component lifecycle with checkpoint support:
- `component_upgrade()` - Three-level fallback strategy
- `component_checkpoint()` - Manual checkpoint creation
- `component_restore()` - Restoration from checkpoint

### Upgrade Process Flow

1. **Checkpoint Phase**:
   ```c
   // Create checkpoint directory
   checkpoint_create_directory(component_name, &checkpoint_id, &path);

   // Attempt CRIU checkpoint (leave running)
   result = criu_checkpoint_process(comp->pid, path, 1);

   // Save metadata
   checkpoint_save_metadata(path, &metadata);
   ```

2. **Restore Phase**:
   ```c
   // Validate checkpoint images
   checkpoint_validate_image(path);

   // Restore process
   new_pid = criu_restore_process(path);

   // Update component record
   comp->pid = new_pid;
   comp->state = COMP_READY_WAIT;
   ```

3. **Cleanup Phase**:
   ```c
   // Terminate old process
   kill(old_pid, SIGTERM);

   // Clean up temporary checkpoint
   remove_directory_recursive(checkpoint_path);
   ```

## Error Handling

### Graceful Degradation

When CRIU is unavailable or checkpoint fails:

```c
switch (comp->handoff) {
    case HANDOFF_CHECKPOINT:
        if (criu_is_supported() == CHECKPOINT_SUCCESS) {
            result = upgrade_with_checkpoint(comp);
            if (result == 0) break;
            LOG_WARN("Checkpoint failed, falling back to FD-passing");
        }
        __attribute__((fallthrough));

    case HANDOFF_FD_PASSING:
        result = upgrade_with_fd_passing(comp);
        if (result == 0) break;
        LOG_WARN("FD-passing failed, falling back to restart");
        __attribute__((fallthrough));

    case HANDOFF_NONE:
    default:
        result = upgrade_with_restart(comp);
        break;
}
```

### Common Error Scenarios

1. **CRIU Not Available** (`CHECKPOINT_ERROR_CRIU_NOT_FOUND`)
   - Automatic fallback to FD-passing
   - User notification of degraded functionality

2. **Kernel Unsupported** (`CHECKPOINT_ERROR_KERNEL_UNSUPPORTED`)
   - One-time warning on system startup
   - Feature disabled for session

3. **Checkpoint Timeout** (`CHECKPOINT_ERROR_TIMEOUT`)
   - Process killed after 30s default timeout
   - Immediate fallback to next level

4. **Restore Failure** (`CHECKPOINT_ERROR_RESTORE_FAILED`)
   - Fallback to FD-passing or restart
   - Checkpoint marked as corrupted

## Performance Considerations

### Memory Usage

- Checkpoint images can be large (process memory + metadata)
- Default quota: 100MB per component
- Automatic cleanup of old checkpoints

### Time Overhead

- Checkpoint creation: 100ms - 2s depending on process size
- Restore time: Similar to checkpoint creation
- Fallback adds minimal latency (< 50ms)

### Storage Management

- Temporary checkpoints in `/run` (tmpfs recommended)
- Persistent checkpoints in `/var/lib` for migration
- Configurable retention policies

## Troubleshooting

### Common Issues

1. **"CRIU binary not found"**
   ```bash
   # Install CRIU
   sudo apt-get install criu

   # Verify installation
   which criu
   criu --version
   ```

2. **"Kernel does not support checkpoint/restore"**
   ```bash
   # Check kernel config
   grep CONFIG_CHECKPOINT_RESTORE /boot/config-$(uname -r)

   # Run CRIU compatibility check
   sudo criu check --ms
   ```

3. **"Permission denied"**
   ```bash
   # Ensure graph-resolver runs as root (PID 1)
   # Check capabilities
   sudo criu check
   ```

4. **"Checkpoint image corrupt"**
   ```bash
   # Clean up corrupted checkpoint
   graphctl checkpoint-rm component_name checkpoint_id

   # Try creating new checkpoint
   graphctl checkpoint component_name
   ```

### Debugging

Enable verbose logging:

```bash
# Set log level for checkpoint operations
export YAKIROS_LOG_LEVEL=DEBUG

# Check CRIU logs in /tmp/criu.log
tail -f /tmp/criu.log
```

## Migration and Backup

### Component Migration

1. **Prepare for migration**:
   ```bash
   graphctl migrate webserver
   ```

2. **Create portable archive**:
   ```bash
   graphctl checkpoint-archive webserver 1640995200 /backup/webserver.tar.gz
   ```

3. **Extract on target system**:
   ```bash
   graphctl checkpoint-extract /backup/webserver.tar.gz webserver
   graphctl restore webserver
   ```

### Backup Strategy

- Regular checkpoint creation via cron
- Long-term storage in `/var/lib/graph/checkpoints/`
- Compression and archival for off-site backup

## Testing

### Unit Tests
```bash
make build-tests
tests/unit/test_checkpoint
```

### Integration Tests
```bash
tests/integration/test_checkpoint_integration
```

### Manual Testing
```bash
# Test basic functionality
sudo ./graph-resolver &
./graphctl checkpoint test-component
./graphctl restore test-component

# Test fallback behavior
# (Requires CRIU to be temporarily unavailable)
```

## Future Enhancements

### Planned Features

1. **Live Migration** - Move components between systems
2. **Delta Checkpoints** - Incremental state preservation
3. **Parallel Restore** - Multi-process restoration
4. **Compression** - Reduce checkpoint storage size
5. **Encryption** - Secure checkpoint storage

### Performance Optimizations

1. **Memory Deduplication** - Share common pages
2. **Async Checkpoints** - Non-blocking operations
3. **Streaming Restore** - Progressive process restoration
4. **Smart Scheduling** - Optimize checkpoint timing

## Conclusion

YakirOS Step 9 successfully implements enterprise-grade checkpoint/restore functionality that extends the existing hot-swap infrastructure. The three-level fallback strategy ensures maximum reliability while providing complete process state preservation when CRIU is available.

This implementation brings YakirOS closer to its ultimate goal of a truly rebootless Linux system, providing the foundation for live kernel upgrades (Step 10) and advanced system maintenance operations.