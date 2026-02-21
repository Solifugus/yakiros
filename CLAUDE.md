# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

YakirOS is a **reactive, dependency-graph-driven init system** for Linux that replaces traditional init systems. Instead of sequential boot scripts, components declare what they provide and require, then activate automatically when their dependencies are satisfied. The ultimate goals are **hot-swappable services** and **live kernel upgrades** - a system that never needs to reboot.

### Key Philosophy
- Traditional init: "Here is the sequence. Follow it."
- YakirOS: "Here are the pieces. They know what they need."
- The system is not a machine that starts and stops - it's an organism that emerges and adapts.

## Architecture

```
                    ┌─────────────────────┐
                    │   Linux Kernel       │
                    │   (memory, sched,    │
                    │    syscalls, hw)     │
                    └─────────┬───────────┘
                              │
                              │ exec
                              ▼
                    ┌─────────────────────┐
                    │   graph-resolver    │
                    │   (PID 1)           │
                    │                     │
                    │  ┌───────────────┐  │
                    │  │  Capability   │  │
                    │  │  Registry     │  │
                    │  └───────┬───────┘  │
                    │          │          │
                    │  ┌───────────────┐  │
                    │  │  Graph        │  │
                    │  │  Resolver     │  │
                    │  └───────┬───────┘  │
                    │          │          │
                    │  ┌───────────────┐  │
                    │  │  Process      │  │
                    │  │  Supervisor   │  │
                    │  └───────────────┘  │
                    └─────────┬───────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
              ▼               ▼               ▼
        ┌───────────┐  ┌───────────┐  ┌───────────┐
        │  udevd    │  │  syslogd  │  │  sshd     │
        │           │  │           │  │           │
        │ provides: │  │ provides: │  │ provides: │
        │  hw.udev  │  │  logging  │  │  ssh      │
        │           │  │           │  │           │
        │ requires: │  │ requires: │  │ requires: │
        │  fs.dev   │  │  fs.var   │  │  net      │
        │  fs.sys   │  │  fs.run   │  │  ssh.keys │
        │  fs.run   │  └───────────┘  │  users    │
        └───────────┘                 └───────────┘
```

### Core Concepts

**Components**: Services or one-shot tasks declared in `/etc/graph.d/*.toml` files. Each component specifies:
- What capabilities it **provides** (e.g., "logging", "network")
- What capabilities it **requires** (e.g., "filesystem.var")
- Binary path and arguments to execute
- Type: "service" (long-running) or "oneshot" (run once)

**Capabilities**: Abstract system features that components provide/require. The graph resolver tracks which capabilities are active and activates components when their requirements are satisfied.

**Graph Resolution**: Continuous process that activates components as dependencies become available. Never considers the system "booted" - the graph is always live.

## Current Development Status

**Current Step**: 1 (Modularize and harden the graph resolver)
**Last Updated**: 2025-02-21

**Step 0 Completed**:
- ✓ Project renamed from SpliceOS to YakirOS
- ✓ Professional directory structure (src/, examples/, docs/, tests/, scripts/)
- ✓ Working monolithic prototype (~700 lines in src/graph-resolver.c)
- ✓ CLI tool (src/graphctl.c)
- ✓ Updated Makefile and build system for new structure
- ✓ Example component declarations (20+ components in examples/components.toml)
- ✓ Compiles clean with `-Wall -Wextra -Werror`
- ✓ Task tracking system established

**Step 1 In Progress**: Modularize the monolithic graph-resolver.c into 6 clean modules with PID 1 hardening.

## Common Development Commands

### Build Commands
```bash
# Build for testing on existing system (dynamic linking)
make

# Build static binaries for deployment as PID 1
make static

# Test build with helpful output
make test

# Clean build artifacts
make clean
```

### Testing Commands
```bash
# Create test directory and split components
sudo mkdir -p /etc/graph.d
make split-components
sudo cp examples/split/*.toml /etc/graph.d/

# Run in test mode (detects non-PID-1, won't mount filesystems)
sudo ./graph-resolver &

# Query the running resolver
./graphctl status     # Show all components and states
./graphctl pending    # Show components waiting on dependencies
./graphctl tree sshd  # Show dependency tree for a component
./graphctl resolve    # Trigger graph re-resolution
./graphctl reload     # Reload component declarations
```

### VM Testing
```bash
# Create and run test VM (when implemented)
make vm-test

# Manual QEMU run
qemu-system-x86_64 \
    -m 4G -smp 4 -enable-kvm \
    -drive file=rebootless.qcow2,format=qcow2 \
    -nographic \
    -append "console=ttyS0 init=/sbin/graph-resolver"
```

## File Layout

```
/sbin/graph-resolver          # PID 1 binary (static build)
/usr/bin/graphctl             # CLI management tool (static build)
/etc/graph.d/*.toml           # Component declarations
/run/graph-resolver.sock      # Control socket for graphctl
/usr/libexec/graph/check-*    # Health check scripts (future)
```

## Component Declaration Format

Components are declared in TOML files in `/etc/graph.d/`:

```toml
[component]
name = "sshd"
binary = "/usr/sbin/sshd"
args = ["-D", "-f", "/etc/ssh/sshd_config"]
type = "service"

[provides]
capabilities = ["ssh"]

[requires]
capabilities = ["network.configured", "ssh.keys", "users"]

[lifecycle]
handoff = "none"              # Future: "fd-passing" for hot-swap
restart_max = 5               # Max restarts in restart_window
restart_window = 30           # Time window in seconds
```

## Development Phases (PLAN.md)

The project follows a 12-step development plan:
1. **Step 0**: Project initialization (current)
2. **Step 1**: Modularize graph resolver into clean modules
3. **Step 2**: Comprehensive test harness
4. **Step 3**: Readiness protocol (components signal when ready)
5. **Step 4**: File descriptor passing for hot-swap
6. **Step 5**: Enhanced graphctl commands
7. **Step 6**: Health checks and degraded states
8. **Step 7**: cgroup and namespace isolation
9. **Step 8**: Dependency cycle detection
10. **Step 9**: CRIU integration for checkpoint/restore
11. **Step 10**: kexec live kernel upgrades
12. **Step 11**: VM integration testing
13. **Step 12**: Documentation and polish

**Always check PROGRESS.md for current status before starting work.**

## Important Implementation Notes

### PID 1 Requirements
- **NEVER exit** - kernel panic will result. Use emergency shell fallback.
- **Must reap all children** - not just managed processes
- **Signal handling**: SIGCHLD (child reaping), SIGTERM (shutdown), SIGINT (Ctrl+Alt+Del), SIGUSR1 (reload), SIGUSR2 (dump state)
- **Early boot mounts**: Must mount proc, sys, dev, run, devpts before loading components

### Code Quality Standards
- Compile with `-Wall -Wextra -Werror` (zero warnings required)
- All malloc/strncpy/snprintf calls must be bounds-checked
- No unbounded recursion in graph resolution
- Extensive logging for debugging (writes to `/dev/kmsg` before syslog available)

### Testing Strategy
- Test mode: When not running as PID 1, skip dangerous operations (mounting, etc.) but still test graph logic
- Use `--config-dir` and `--control-socket` arguments for test isolation
- Component tests use simple binaries like `/bin/sleep`, `/bin/true`, `/bin/false`

## Milestones and Vision

### Milestone 1: Replace init ✓ (current code)
Basic graph resolution, process supervision, inotify component loading

### Milestone 2: Hot-swap services (Steps 3-4)
fd-passing between old and new processes, atomic capability handoff, zero downtime upgrades

### Milestone 3: Live kernel upgrade (Steps 9-10)
CRIU checkpoint/restore + kexec = upgrade kernel without rebooting

### Milestone 4: Advanced features (Steps 6-8)
Health checks, cgroups, namespaces, cycle detection, graph visualization

The ultimate vision: A Linux system that **never needs to reboot** for any reason - service upgrades, kernel upgrades, configuration changes all happen transparently while the system continues running.