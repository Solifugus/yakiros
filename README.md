# YakirOS — The System That Never Reboots

A reactive, dependency-graph-driven init system for Linux.
Components activate when their prerequisites are satisfied.
Components are replaced in-place without stopping the system.
The graph is always live. There is no "booted" state.

## Quick Start (Testing on Existing System)

```bash
# Build
make

# Create test directory
sudo mkdir -p /etc/graph.d

# Split example components into individual files
make split-components
sudo cp examples/split/*.toml /etc/graph.d/

# Run resolver (detects non-PID-1, enters test mode)
# Note: in test mode it won't actually mount filesystems
# or start system services — it just resolves the graph
sudo ./graph-resolver &

# Query the graph
./graphctl status
./graphctl pending
./graphctl tree sshd
```

## Real Deployment (LFS VM)

### 1. Set up the VM

```bash
qemu-img create -f qcow2 yakiros.qcow2 20G

# Boot from an LFS live ISO or any Linux installer
qemu-system-x86_64 \
    -m 4G -smp 4 -enable-kvm \
    -drive file=yakiros.qcow2,format=qcow2 \
    -cdrom lfs-live-12.iso \
    -boot d
```

### 2. Build LFS normally

Follow the LFS book (https://www.linuxfromscratch.org/lfs/).
Build the complete toolchain and base system.

### 3. Replace init with graph-resolver

```bash
# On the LFS system, install musl for static builds
# (or cross-compile from host)

# Build static binaries
make static

# Install
make install DESTDIR=/mnt/lfs

# Install component declarations
make install-components DESTDIR=/mnt/lfs

# Set kernel command line in bootloader (GRUB example):
#   linux /boot/vmlinuz root=/dev/sda1 init=/sbin/graph-resolver
```

### 4. Boot and observe

The system will come up reactively:
- Kernel starts, hands off to graph-resolver (PID 1)
- Resolver mounts virtual filesystems (proc, sys, dev, run)
- Registers kernel capabilities
- Loads component declarations from /etc/graph.d/
- Resolves the graph — components activate as deps are met
- System is "up" when the graph is stable

Use `graphctl status` to watch the graph resolve in real-time.

## Milestones

### Milestone 1: Replace init ✓ (this code)
- [x] TOML parser (minimal subset)
- [x] Dependency graph resolution
- [x] Process supervision (fork/exec/reap)
- [x] Capability registry
- [x] inotify for live component addition
- [x] Control socket (graphctl)
- [x] Signal handling (SIGCHLD)
- [x] Early boot mounts
- [x] Restart rate limiting

### Milestone 2: Hot-swap services
- [ ] fd-passing between old and new process
- [ ] State serialization/deserialization
- [ ] Atomic capability handoff (no gap in service)
- [ ] `graphctl upgrade <component>`
- [ ] Health check integration

### Milestone 3: Live kernel upgrade
- [ ] CRIU integration for process checkpoint
- [ ] kexec wrapper in graphctl
- [ ] Restore from checkpoint after kexec
- [ ] Verify graph integrity post-restore

### Milestone 4: Advanced features
- [ ] cgroup setup per component
- [ ] Namespace isolation
- [ ] Readiness protocol (component signals when truly ready)
- [ ] Dependency cycle detection
- [ ] Graph visualization (DOT output)
- [ ] Overlay filesystem for atomic component updates
- [ ] Capability conditions (health-gated)

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
        │  fs.run   │  │           │  │  users    │
        └───────────┘  └───────────┘  └───────────┘
```

## File Layout

```
/sbin/graph-resolver          PID 1 binary (static)
/usr/bin/graphctl             CLI tool (static)
/etc/graph.d/*.toml           Component declarations
/run/graph-resolver.sock      Control socket
/usr/libexec/graph/check-*    Health check scripts
```

## Philosophy

Traditional init: "Here is the sequence. Follow it."
YakirOS: "Here are the pieces. They know what they need."

The system is not a machine that starts and stops.
It is an organism that emerges and adapts.

## License

Public domain. Do what you want with it.
