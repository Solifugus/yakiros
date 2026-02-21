# YakirOS Development Plan
# ══════════════════════════════════════════════════════════════
# 
# This file is the master plan for building YakirOS — a reactive,
# dependency-graph-driven init system that replaces traditional boot
# sequences. Components activate when prerequisites are satisfied.
# The system never needs to reboot.
#
# HOW TO USE THIS FILE:
# - Each STEP is self-contained. You can execute any step with a
#   fresh context window by reading ONLY that step.
# - Each step tells you: what files exist, what to build, how to
#   test, and how to mark progress.
# - After completing a step, mark it done in PROGRESS.md
# - The next developer (or Claude Code instance) reads PROGRESS.md
#   first to know where things stand.
#
# PROJECT LOCATION: ~/yakiros/
# PROGRESS TRACKER: ~/yakiros/PROGRESS.md
#
# ══════════════════════════════════════════════════════════════


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 0: PROJECT INITIALIZATION                            │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: You are starting the YakirOS project from scratch or
# verifying that the initial scaffolding exists.
#
# WHAT YAKIROS IS:
# A Linux init system (PID 1) that uses a dependency graph instead
# of a boot sequence. Components declare what they provide and
# require. The graph resolver activates them reactively as
# prerequisites become available. Components can be hot-swapped
# without rebooting. The system never considers itself "booted" —
# the graph is always live and resolving.
#
# WHAT TO DO:
#
# 1. Create the project directory structure:
#
#    ~/yakiros/
#    ├── src/                    # C source files
#    │   ├── graph-resolver.c   # PID 1 binary
#    │   ├── graphctl.c         # CLI control tool
#    │   ├── toml.c             # TOML parser (extracted module)
#    │   ├── toml.h
#    │   ├── capability.c       # Capability registry
#    │   ├── capability.h
#    │   ├── component.c        # Component lifecycle
#    │   ├── component.h
#    │   ├── graph.c            # Graph resolution engine
#    │   ├── graph.h
#    │   ├── control.c          # Unix socket control interface
#    │   ├── control.h
#    │   ├── log.c              # Logging
#    │   └── log.h
#    ├── examples/              # Example component declarations
#    │   └── *.toml
#    ├── tests/                 # Test harness
#    │   ├── run-tests.sh       # Test runner
#    │   └── test-*.sh          # Individual test scripts
#    ├── scripts/               # Helper scripts
#    │   └── split-components.py
#    ├── docs/                  # Design documents
#    │   └── architecture.md
#    ├── Makefile
#    ├── README.md
#    ├── PROGRESS.md            # Progress tracker (critical!)
#    └── PLAN.md                # This file
#
# 2. If source files already exist in ~/yakiros/ (from a prior
#    session), review them and verify they compile:
#      cd ~/yakiros && make 2>&1
#    If they exist as monolithic files (graph-resolver.c, graphctl.c),
#    that's fine — step 1 will refactor them into modules.
#
# 3. Create PROGRESS.md with this template:
#
#    ```markdown
#    # YakirOS Progress
#
#    ## Status
#    Current step: 0
#    Last updated: [date]
#    
#    ## Steps
#    - [ ] Step 0: Project initialization
#    - [ ] Step 1: Modularize and harden the graph resolver
#    - [ ] Step 2: Comprehensive test harness
#    - [ ] Step 3: Readiness protocol
#    - [ ] Step 4: File descriptor passing for hot-swap
#    - [ ] Step 5: graphctl enhancements
#    - [ ] Step 6: Health checks and degraded states
#    - [ ] Step 7: cgroup and namespace isolation
#    - [ ] Step 8: Dependency cycle detection and graph analysis
#    - [ ] Step 9: CRIU integration for process checkpoint/restore
#    - [ ] Step 10: kexec live kernel upgrade
#    - [ ] Step 11: VM integration testing with QEMU
#    - [ ] Step 12: Documentation and polish
#    
#    ## Notes
#    [any observations, blockers, or decisions]
#    ```
#
# 4. If files from a prior session exist, reconcile them into the
#    structure above. Don't delete working code — migrate it.
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 0: Project initialization"
#   - Update "Current step: 1"
#   - Update "Last updated:" with today's date
#   - Add any notes about what was found or decided
#
# DONE WHEN:
# - Directory structure exists
# - PROGRESS.md exists and is populated
# - Any existing code compiles (or is noted as needing fixes)
# - Step 0 is checked off in PROGRESS.md


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 1: MODULARIZE AND HARDEN THE GRAPH RESOLVER          │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ exists with either a monolithic
# graph-resolver.c or already-split source files. Check
# PROGRESS.md to see current state.
#
# GOAL: Split the resolver into clean modules with well-defined
# interfaces. Harden the code for production use as PID 1.
#
# WHAT TO DO:
#
# 1. Read the existing source. If graph-resolver.c is monolithic
#    (600+ lines, all in one file), split it into these modules:
#
#    src/log.h / src/log.c:
#    - void log_open(void);
#    - void graph_log(const char *level, const char *fmt, ...);
#    - Macros: LOG_INFO, LOG_WARN, LOG_ERR
#    - Writes to /dev/kmsg when available, stderr otherwise
#
#    src/toml.h / src/toml.c:
#    - Minimal TOML parser — only the subset we use:
#      [section], key = "value", key = ["a", "b"], key = number
#    - int parse_component(const char *path, component_t *comp);
#    - parse_array(), parse_signal(), parse_handoff() helpers
#    - Must handle: comments (#), blank lines, whitespace
#    - Must reject: malformed files (don't crash, log and skip)
#
#    src/capability.h / src/capability.c:
#    - capability_t struct: name, active flag, provider index
#    - int capability_index(const char *name);
#    - int capability_active(const char *name);
#    - void capability_register(const char *name, int provider);
#    - void capability_withdraw(const char *name);
#    - int capability_count(void);  /* total registered */
#    - const char *capability_name(int idx);  /* iterate */
#
#    src/component.h / src/component.c:
#    - component_t struct (see existing code for fields)
#    - int requirements_met(component_t *comp);
#    - int component_start(int idx);
#    - void component_exited(int idx, int status);
#    - void load_components(const char *dir);
#    - Restart rate limiting: max 5 restarts in 30 seconds,
#      then exponential backoff (30s, 60s, 120s, 300s)
#
#    src/graph.h / src/graph.c:
#    - int graph_resolve(void);  /* single pass, returns # changes */
#    - void graph_resolve_full(void);  /* iterate until stable */
#    - Cycle detection: if iterations > n_components, log error
#
#    src/control.h / src/control.c:
#    - int setup_control_socket(void);
#    - void handle_control_command(int client_fd);
#    - Commands: status, pending, resolve, tree <n>, reload,
#      rdeps <capability>, simulate remove <n>
#
#    src/graph-resolver.c (main):
#    - main() function only
#    - Early mounts (proc, sys, dev, run, devpts)
#    - Signal setup (SIGCHLD via self-pipe trick)
#    - epoll event loop
#    - Includes all the above headers
#
# 2. Update the Makefile:
#    ```makefile
#    SRCS = src/graph-resolver.c src/log.c src/toml.c \
#           src/capability.c src/component.c src/graph.c src/control.c
#    OBJS = $(SRCS:.c=.o)
#    CFLAGS = -Wall -Wextra -Werror -O2 -std=c11 -Isrc
#    
#    graph-resolver: $(OBJS)
#    	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
#    
#    graphctl: src/graphctl.c
#    	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
#    ```
#
# 3. Harden for PID 1:
#    - PID 1 must NEVER exit (kernel panic). Add a fallback:
#      if main event loop fails, drop to emergency shell
#      (exec /bin/sh) rather than returning.
#    - PID 1 must reap ALL children, not just managed ones.
#      (The existing SIGCHLD handler does this — verify it.)
#    - All malloc/strncpy/snprintf calls must be bounds-checked.
#    - No unbounded recursion in graph resolution.
#    - Handle SIGTERM (for shutdown), SIGINT (Ctrl+Alt+Del),
#      SIGUSR1 (reload config), SIGUSR2 (dump state to log).
#
# 4. Verify it compiles clean:
#    make clean && make 2>&1
#    All warnings must be resolved. -Werror is non-negotiable.
#
# 5. Verify graphctl still compiles:
#    make graphctl 2>&1
#
# HOW TO TEST:
#   - `make clean && make` must succeed with zero warnings
#   - Run `./graph-resolver` (not as PID 1) — it should print
#     the test mode warning and attempt to load from /etc/graph.d/
#   - If /etc/graph.d/ doesn't exist, it should log a warning
#     and enter the event loop anyway (no crash)
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 1"
#   - Update "Current step: 2"
#   - Note which modules were created and any design decisions
#
# DONE WHEN:
# - Code is split into ≥6 source files with clean headers
# - Compiles with -Wall -Wextra -Werror, zero warnings
# - Emergency shell fallback exists
# - Signal handlers for SIGTERM, SIGINT, SIGUSR1, SIGUSR2


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 2: COMPREHENSIVE TEST HARNESS                        │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has modularized source that compiles.
# Check PROGRESS.md for current state.
#
# GOAL: Build a test framework that exercises the resolver WITHOUT
# needing to be PID 1 or running in a VM. Test the graph logic,
# TOML parsing, and component lifecycle in userspace.
#
# WHAT TO DO:
#
# 1. Create tests/run-tests.sh:
#    - Sets up a temp directory with /etc/graph.d/ equivalent
#    - Creates mock component .toml files
#    - Uses simple binaries (like /bin/true, /bin/sleep, /bin/sh)
#      as component binaries
#    - Runs graph-resolver in test mode (not as PID 1)
#    - Uses graphctl to verify state
#    - Reports pass/fail for each test
#    - Cleans up temp dirs on exit (trap)
#
# 2. Create these specific test cases:
#
#    tests/test-toml-parsing.sh:
#    - Create valid .toml files, verify they're loaded
#    - Create malformed .toml files (missing name, missing binary,
#      bad syntax), verify they're skipped without crashing
#    - Create .toml with empty requires/provides, verify it loads
#    - Create non-.toml files in graph.d/, verify they're ignored
#
#    tests/test-graph-resolution.sh:
#    - Component A provides "cap-a", requires nothing → starts
#    - Component B requires "cap-a" → starts after A
#    - Component C requires "cap-a" AND "cap-b" → stays pending
#      until both are provided
#    - Use /bin/sleep as the component binary so processes stay alive
#    - Use graphctl status to verify states
#    - Use graphctl pending to verify C is waiting on cap-b
#
#    tests/test-restart.sh:
#    - Component that exits immediately (use /bin/false)
#    - Verify it enters FAILED state
#    - Verify it retries (check restart_count via graphctl)
#    - Verify rate limiting kicks in (doesn't restart instantly
#      after several failures)
#
#    tests/test-capability-withdrawal.sh:
#    - Start component A (provides cap-a)
#    - Start component B (requires cap-a)
#    - Kill A's process (kill -9)
#    - Verify cap-a goes DOWN in graphctl status
#    - Verify A is restarted
#    - Verify cap-a comes back UP
#
#    tests/test-inotify.sh:
#    - Start resolver with empty graph.d/
#    - Drop a new .toml file into graph.d/
#    - Verify the component is picked up and started
#    - Remove the .toml file
#    - (Component may keep running — that's OK for now)
#
#    tests/test-oneshot.sh:
#    - Oneshot component (type = "oneshot", binary = /bin/true)
#    - Verify it runs, exits, enters DONE state
#    - Verify its capabilities are registered after exit
#    - Downstream component that requires the oneshot's capability
#      → verify it starts after oneshot completes
#
# 3. For the test framework to work, graph-resolver needs to accept
#    a command-line argument for the config directory:
#      ./graph-resolver --config-dir /tmp/test-graph.d/
#    If this isn't already supported, add it. Default remains
#    /etc/graph.d/ when no argument is given.
#    Also add --control-socket <path> so tests don't collide with
#    a real install.
#
# 4. Update Makefile:
#    ```
#    test: all
#    	bash tests/run-tests.sh
#    ```
#
# HOW TO TEST:
#   - `make test` runs all tests
#   - Each test prints PASS or FAIL
#   - Zero failures required before marking done
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 2"
#   - Note how many test cases pass
#   - Note any bugs found and fixed during testing
#
# DONE WHEN:
# - ≥6 test scripts exist and pass
# - graph-resolver accepts --config-dir and --control-socket args
# - `make test` runs clean
# - All tests clean up after themselves


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 3: READINESS PROTOCOL                                │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has modularized, tested source.
# Check PROGRESS.md for current state.
#
# GOAL: Right now, service-type components are marked ACTIVE
# immediately after fork/exec. This is wrong — a service might
# need seconds to initialize before it can actually serve.
# Implement a readiness protocol so components signal when they
# are truly ready.
#
# THE PROBLEM:
# If sshd takes 2 seconds to start and a component that depends
# on the "ssh" capability starts immediately, it may fail because
# sshd isn't actually listening yet.
#
# THE SOLUTION:
# Three readiness modes, declared in the component's .toml:
#
#   [lifecycle]
#   readiness = "immediate"     # Current behavior (default)
#   readiness = "notify"        # Component writes to a notification fd
#   readiness = "file"          # Component creates a file when ready
#   readiness_file = "/run/sshd.ready"  # Path for "file" mode
#   readiness_timeout = 30      # Seconds to wait before marking FAILED
#
# WHAT TO DO:
#
# 1. Add readiness fields to component_t:
#    - readiness_mode: enum { READY_IMMEDIATE, READY_NOTIFY, READY_FILE }
#    - readiness_file: char[MAX_PATH]
#    - readiness_timeout: int (seconds, default 30)
#
# 2. Implement READY_NOTIFY:
#    - Before fork(), create a pipe.
#    - Pass the write end to the child as fd 3 (or set env var
#      NOTIFY_FD=<n>). The child writes "READY\n" to this fd
#      when it's ready.
#    - Parent watches the read end via epoll.
#    - When "READY\n" is received, transition to ACTIVE and
#      register capabilities.
#    - If readiness_timeout expires, mark FAILED.
#
# 3. Implement READY_FILE:
#    - After fork(), start watching readiness_file path with inotify
#      (IN_CREATE | IN_MODIFY).
#    - When the file appears (or already exists), transition to
#      ACTIVE and register capabilities.
#    - If readiness_timeout expires, mark FAILED.
#
# 4. Update TOML parser to read the new fields.
#
# 5. For READY_IMMEDIATE (default), keep current behavior:
#    mark ACTIVE immediately after exec succeeds.
#
# 6. Component state machine becomes:
#    INACTIVE → STARTING → (wait for readiness) → ACTIVE
#    INACTIVE → STARTING → (timeout) → FAILED
#    ACTIVE → (process exits) → FAILED → (retry) → STARTING
#
# 7. Update graphctl status output to show readiness mode and
#    time since start for STARTING components.
#
# HOW TO TEST:
#    Create a test component that sleeps 2 seconds then creates
#    a ready file:
#      binary = "/bin/sh"
#      args = ["-c", "sleep 2 && touch /tmp/test-ready && exec sleep 999"]
#      readiness = "file"
#      readiness_file = "/tmp/test-ready"
#
#    Create a downstream component that requires the first one's
#    capability. Verify:
#    - First component enters STARTING state
#    - After 2 seconds, transitions to ACTIVE
#    - THEN downstream component starts
#    - Not before.
#
#    Test timeout: set readiness_timeout = 2 with a component that
#    never signals ready (binary = /bin/sleep, readiness = "file").
#    Verify it enters FAILED after 2 seconds.
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 3"
#   - Note which readiness modes are implemented
#
# DONE WHEN:
# - All three readiness modes work
# - Timeout correctly marks FAILED
# - Downstream components correctly wait for readiness
# - Existing tests still pass
# - New readiness tests pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 4: FILE DESCRIPTOR PASSING FOR HOT-SWAP              │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has modularized, tested source with
# readiness protocol. Check PROGRESS.md.
#
# GOAL: Implement the core hot-swap mechanism. When upgrading a
# component, the new version inherits the old version's open file
# descriptors. From the perspective of connected clients, service
# is uninterrupted.
#
# THIS IS THE KEY FEATURE OF SPLICEOS. Without this, we're just
# another process supervisor. With this, we're a system that
# never needs to restart services.
#
# HOW FD PASSING WORKS ON LINUX:
# Unix domain sockets support SCM_RIGHTS — sending open file
# descriptors from one process to another via sendmsg/recvmsg.
# The receiving process gets new fd numbers pointing to the same
# underlying kernel objects (sockets, pipes, files).
#
# THE HOT-SWAP PROTOCOL:
#
#   1. graphctl upgrade <component> is issued
#   2. Resolver checks: component.handoff must be "fd-passing"
#   3. Resolver creates a unix socketpair (handoff_sock)
#   4. Resolver forks new component process, passing handoff_sock[1]
#      as fd 4 (or HANDOFF_FD env var)
#   5. New process starts initializing
#   6. Resolver sends SIGUSR1 to old process, signaling "prepare
#      to hand off"
#   7. Old process:
#      a. Stops accepting new connections
#      b. Sends all open fds over handoff_sock[0] via SCM_RIGHTS
#      c. Sends a "HANDOFF_COMPLETE\n" message
#      d. Exits cleanly
#   8. New process:
#      a. Receives fds from handoff_sock[1]
#      b. Adopts them (they now serve existing connections)
#      c. Signals readiness (via readiness protocol from step 3)
#   9. Resolver transitions capability from old to new without
#      any gap.
#
# WHAT TO DO:
#
# 1. Create src/handoff.h / src/handoff.c:
#
#    /* Send file descriptors over a unix socket */
#    int send_fds(int sock, int *fds, int n_fds);
#
#    /* Receive file descriptors from a unix socket */
#    int recv_fds(int sock, int *fds, int max_fds);
#
#    These use sendmsg/recvmsg with CMSG_FIRSTHDR and SCM_RIGHTS.
#    Reference: man 7 unix, man 3 cmsg
#
#    Key code pattern:
#    ```c
#    struct msghdr msg = {0};
#    struct cmsghdr *cmsg;
#    char buf[CMSG_SPACE(sizeof(int) * MAX_FDS)];
#    
#    msg.msg_control = buf;
#    msg.msg_controllen = sizeof(buf);
#    
#    cmsg = CMSG_FIRSTHDR(&msg);
#    cmsg->cmsg_level = SOL_SOCKET;
#    cmsg->cmsg_type = SCM_RIGHTS;
#    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * n_fds);
#    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * n_fds);
#    
#    sendmsg(sock, &msg, 0);
#    ```
#
# 2. Add "upgrade" command to graphctl and the control socket handler:
#    - graphctl upgrade <component_name>
#    - Validates the component exists and has handoff = "fd-passing"
#    - Triggers the hot-swap sequence in the resolver
#
# 3. Implement the hot-swap sequence in component.c:
#    - int component_upgrade(int idx);
#    - Creates socketpair
#    - Forks new process with handoff socket
#    - Signals old process with SIGUSR1
#    - Monitors both old and new via epoll
#    - On HANDOFF_COMPLETE from old, waits for readiness from new
#    - On readiness from new, transitions capabilities
#    - Kills old if it doesn't exit within 10 seconds
#
# 4. Create a test helper program: tests/echo-server.c
#    A simple TCP echo server that:
#    - Listens on a port
#    - Handles SIGUSR1 by sending its listen fd over HANDOFF_FD
#    - When started with HANDOFF_FD set, receives fd and uses it
#    This is the test subject for hot-swap.
#
# HOW TO TEST:
#    - Start echo-server as a component
#    - Connect a client (nc localhost <port>), send data, verify echo
#    - Run graphctl upgrade echo-server
#    - Verify: client connection survives
#    - Verify: new echo-server process is running (different PID)
#    - Verify: sending more data still echoes back
#    - Verify: capability never went DOWN during the swap
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 4"
#   - Note whether the echo server test passes
#   - Note any edge cases discovered
#
# DONE WHEN:
# - send_fds / recv_fds work correctly
# - graphctl upgrade triggers a hot-swap
# - Echo server survives upgrade with connected clients
# - No capability gap during swap
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 5: GRAPHCTL ENHANCEMENTS                             │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has hot-swap working. Check PROGRESS.md.
#
# GOAL: Make graphctl a comprehensive management tool.
#
# WHAT TO DO:
#
# 1. Add these commands to graphctl and the control socket handler:
#
#    graphctl status
#      Shows all components with state, PID, uptime, restart count.
#      Format:
#        COMPONENT            STATE      PID     UPTIME  RESTARTS
#        kernel               ACTIVE     0       -       0
#        mount-root-rw        DONE       -       -       0
#        udevd                ACTIVE     1234    5m32s   0
#        sshd                 ACTIVE     5678    5m30s   0
#        dhcpcd               STARTING   9012    2s      0
#        ntpd                 FAILED     -       -       3
#
#    graphctl caps (or graphctl capabilities)
#      Shows all capabilities and their status:
#        CAPABILITY                     STATUS  PROVIDER
#        kernel.syscalls                UP      kernel
#        filesystem.proc                UP      kernel
#        hardware.udev                  UP      udevd
#        network.configured             DOWN    -
#
#    graphctl tree <component>
#      Shows the full dependency tree (recursive):
#        sshd
#        ├── requires: network.configured (UP, from dhcpcd)
#        │   └── dhcpcd
#        │       ├── requires: hardware.devices.settled (UP, from udev-settle)
#        │       └── requires: network (UP, from loopback-addr)
#        ├── requires: ssh.hostkeys (UP, from ssh-keygen)
#        └── requires: users (UP, from users)
#
#    graphctl rdeps <capability>
#      Shows reverse dependencies — who depends on this:
#        filesystem.var:
#          → syslogd (ACTIVE)
#          → seed-entropy (DONE)
#
#    graphctl simulate remove <component>
#      Shows what would happen if a component were removed:
#        Removing syslogd would:
#          - Withdraw capability: logging
#          - Affect components: klogd (requires logging)
#          - Transitively affect: (none)
#
#    graphctl dot
#      Outputs the dependency graph in Graphviz DOT format:
#        digraph spliceos {
#          rankdir=LR;
#          udevd -> "filesystem.dev";
#          udevd -> "filesystem.sys";
#          sshd -> "network.configured";
#          ...
#        }
#      Usage: graphctl dot | dot -Tpng -o graph.png
#
#    graphctl log <component> [lines]
#      Shows recent log entries for a component (if logging is
#      captured — this may require piping stdout/stderr to a
#      per-component log file in /run/graph/<component>.log).
#
# 2. Add color output to graphctl (when stdout is a terminal):
#    - Green for ACTIVE/UP/DONE
#    - Red for FAILED/DOWN
#    - Yellow for STARTING/DEGRADED
#    - Use ANSI codes: \033[32m green, \033[31m red, \033[33m yellow,
#      \033[0m reset. Check isatty(1) before using colors.
#
# 3. Add per-component logging:
#    - When forking a component, redirect its stdout/stderr to
#      /run/graph/<component>.log
#    - Rotate: keep last 100KB per component (truncate on overflow)
#    - graphctl log reads these files
#
# HOW TO TEST:
#   - Start resolver with several test components
#   - Run each graphctl command and verify output is correct
#   - Pipe graphctl dot to a file, verify valid DOT syntax
#   - Test color output (run in terminal) and no-color (pipe to file)
#   - Test graphctl log with a component that produces output
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 5"
#   - List which commands are implemented
#
# DONE WHEN:
# - All listed commands work
# - Color output works (and degrades gracefully when piped)
# - Per-component logging works
# - DOT output produces valid graphviz input
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 6: HEALTH CHECKS AND DEGRADED STATES                 │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has enhanced graphctl. Check PROGRESS.md.
#
# GOAL: Components can define health checks. If a health check
# fails, the component enters DEGRADED state and its capabilities
# may be conditionally withdrawn.
#
# WHAT TO DO:
#
# 1. Add health check execution to the event loop:
#    - Each component with a health_check path gets checked
#      every health_interval seconds.
#    - Health check is a simple script/binary: exit 0 = healthy,
#      nonzero = unhealthy.
#    - Fork/exec the health check, set a timeout (10 seconds).
#    - Track consecutive failures.
#
# 2. New component states:
#    ACTIVE → (health check fails once) → ACTIVE (log warning)
#    ACTIVE → (health check fails 3x consecutive) → DEGRADED
#    DEGRADED → (health check passes) → ACTIVE
#    DEGRADED → (health check fails 5x consecutive) → FAILED
#      (process is killed and restarted)
#
# 3. DEGRADED state behavior:
#    - Capabilities remain registered but are marked "degraded"
#    - Add a "degraded" flag to capability_t
#    - Components that require a capability should still consider
#      it "available" when degraded (they're already running)
#    - But NEW activations should prefer non-degraded providers
#      (future: multiple providers for same capability)
#
# 4. Update graphctl status to show health:
#    COMPONENT     STATE      HEALTH    LAST CHECK
#    sshd          ACTIVE     OK        10s ago
#    dhcpcd        DEGRADED   FAIL(2)   5s ago
#    syslogd       ACTIVE     OK        20s ago
#
# 5. Create example health checks in /usr/libexec/graph/:
#    check-sshd: tries to connect to port 22, exit 0 if OK
#    check-network: pings gateway or checks route table
#    check-dns: tries to resolve a known hostname
#    check-syslogd: sends a test message to /dev/log
#
# HOW TO TEST:
#   - Component with health check that passes → stays ACTIVE
#   - Component with health check that fails → enters DEGRADED
#   - Component with health check that recovers → back to ACTIVE
#   - Component with persistent failure → restarted
#   - Health check timeout → counts as failure
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 6"
#   - Note health check behavior and any edge cases
#
# DONE WHEN:
# - Health checks run on schedule
# - DEGRADED state works correctly
# - Recovery from degraded works
# - Persistent failure triggers restart
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 7: CGROUP AND NAMESPACE ISOLATION                    │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has health checks. Check PROGRESS.md.
#
# GOAL: Each component runs in its own cgroup (for resource limits)
# and optionally its own namespaces (for isolation).
#
# WHAT TO DO:
#
# 1. cgroup v2 support:
#    - Mount cgroup2 at /sys/fs/cgroup if not already mounted
#    - Create a cgroup per component: /sys/fs/cgroup/graph/<name>/
#    - Before exec, move the child into its cgroup:
#      echo $PID > /sys/fs/cgroup/graph/<name>/cgroup.procs
#    - Apply resource limits from [resources] section:
#      memory_max → memory.max
#      cpu_weight → cpu.weight
#    - On component stop, clean up the cgroup (rmdir after empty)
#
# 2. Add to component .toml:
#    ```toml
#    [resources]
#    cgroup = "system/logging"   # cgroup path under /sys/fs/cgroup/graph/
#    memory_max = "64M"
#    memory_high = "48M"         # soft limit (throttle, don't kill)
#    cpu_weight = 50             # relative to other components
#    cpu_max = "50000 100000"    # 50% of one CPU
#    io_weight = 100
#    pids_max = 100              # fork bomb protection
#    ```
#
# 3. Namespace support (optional per-component):
#    ```toml
#    [isolation]
#    namespaces = ["mount", "pid", "net", "uts", "ipc"]
#    root = "/"                  # chroot/pivot_root target
#    hostname = "component-name" # for UTS namespace
#    ```
#    Use clone() or unshare() with appropriate CLONE_NEW* flags.
#    Mount namespace is most useful: component sees its own /tmp,
#    can't access other components' private dirs.
#
# 4. Implementation in component_start():
#    - Before fork: prepare cgroup directory
#    - After fork, in child (before exec):
#      * unshare() for requested namespaces
#      * Mount private /tmp if mount namespace
#      * Set hostname if UTS namespace
#    - After fork, in parent:
#      * Write child PID to cgroup.procs
#      * Write resource limits to cgroup files
#
# 5. Implement OOM handling:
#    - Monitor cgroup memory events (memory.events)
#    - If OOM killer hits a component, treat it like a crash
#    - Log specifically that it was OOM-killed
#
# HOW TO TEST:
#   - Component with memory_max = "16M" → verify the cgroup file
#     contains 16777216
#   - Component that tries to allocate more than memory_max →
#     gets killed, enters FAILED, is restarted
#   - Component with pids_max = 5 → can't fork more than 5 times
#   - Component with mount namespace → can't see other
#     components' /tmp files
#   - Verify cgroup dirs are cleaned up when component stops
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 7"
#   - Note which cgroup controllers are supported
#   - Note which namespaces are supported
#
# DONE WHEN:
# - cgroup creation and resource limits work
# - At least memory_max, cpu_weight, and pids_max are enforced
# - At least mount namespace isolation works
# - Cgroups are cleaned up on component exit
# - OOM events are detected and logged
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 8: DEPENDENCY CYCLE DETECTION AND GRAPH ANALYSIS      │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has cgroup isolation. Check PROGRESS.md.
#
# GOAL: Detect dependency cycles at load time (not at runtime when
# things are stuck). Provide graph analysis tools.
#
# WHAT TO DO:
#
# 1. Implement topological sort with cycle detection:
#    - When components are loaded, build the full dependency graph:
#      component → requires capability → provided by component
#    - Run Kahn's algorithm (BFS topological sort):
#      a. Find all nodes with no incoming edges (no requirements,
#         or all requirements provided by kernel)
#      b. Remove them from the graph, add to sorted list
#      c. Repeat until graph is empty or no more nodes can be removed
#      d. If nodes remain, they form one or more cycles
#    - Log detected cycles with the involved components
#    - Components in cycles should be marked with a new state:
#      COMP_CYCLE — permanently unable to activate
#
# 2. Add to graph.c:
#    - int graph_detect_cycles(void);
#      Returns number of components in cycles. Logs details.
#    - void graph_toposort(int *order, int *n_order);
#      Fills order[] with component indices in activation order.
#      This is informational (we don't USE the order for activation,
#      since we're reactive, but it's useful for analysis).
#
# 3. Add to graphctl:
#    - graphctl check
#      Runs cycle detection and reports results:
#        Graph analysis:
#          23 components, 15 capabilities
#          Activation layers: 7
#          Cycles: none
#      Or:
#        CYCLE DETECTED:
#          component-a requires cap-x (from component-b)
#          component-b requires cap-y (from component-a)
#
#    - graphctl order
#      Shows the topological ordering (what WOULD the activation
#      order be if we activated sequentially):
#        Layer 0: kernel
#        Layer 1: mount-root-rw, mount-proc (parallel)
#        Layer 2: mount-var, udevd (parallel)
#        Layer 3: syslogd, loopback
#        ...
#      This is useful for understanding the graph structure even
#      though we don't actually activate sequentially.
#
# 4. Run cycle detection at startup, after loading components.
#    If cycles are found, log them prominently but don't refuse
#    to start — the non-cycling components should still work.
#
# HOW TO TEST:
#   - Create components with no cycles → graphctl check reports clean
#   - Create A→B→A cycle → detected and reported
#   - Create A→B→C→A cycle → detected and reported
#   - Verify non-cycling components still activate normally
#   - Verify graphctl order output matches expected layers
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 8"
#   - Note cycle detection algorithm used
#
# DONE WHEN:
# - Cycle detection works at startup
# - graphctl check and graphctl order work
# - Cycles don't prevent non-cycling components from activating
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 9: CRIU INTEGRATION FOR CHECKPOINT/RESTORE            │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has graph analysis. Check PROGRESS.md.
#
# GOAL: Integrate CRIU (Checkpoint/Restore In Userspace) so we can
# freeze running processes, save their state, and restore them.
# This is a prerequisite for live kernel upgrades (Step 10).
#
# CRIU OVERVIEW:
# CRIU (https://criu.org/) can:
#   - Freeze a running process tree
#   - Dump its state to disk (memory, fds, registers, etc.)
#   - Restore the process tree from the dump
# The process doesn't know it was checkpointed. It just continues.
#
# WHAT TO DO:
#
# 1. Ensure CRIU is available:
#    - Check if `criu` binary exists
#    - If building for LFS: CRIU needs protobuf-c, libbsd, and
#      a few other libs. Document the build process in
#      docs/building-criu.md.
#    - CRIU requires kernel >= 3.11 with specific config options:
#      CONFIG_CHECKPOINT_RESTORE=y, CONFIG_NAMESPACES=y, etc.
#      Document required kernel config.
#
# 2. Create src/checkpoint.h / src/checkpoint.c:
#
#    /* Checkpoint a single component's process tree */
#    int component_checkpoint(int idx, const char *dump_dir);
#    
#    /* Restore a component from a checkpoint */
#    int component_restore(int idx, const char *dump_dir);
#    
#    /* Checkpoint ALL managed processes */
#    int checkpoint_all(const char *dump_dir);
#    
#    /* Restore ALL from checkpoint */
#    int restore_all(const char *dump_dir);
#
#    Implementation: fork/exec criu with appropriate arguments:
#      criu dump -t <PID> -D <dump_dir> --shell-job
#      criu restore -D <dump_dir> --shell-job
#
#    For checkpoint_all, we need to checkpoint the entire process
#    tree managed by the resolver. This is trickier because CRIU
#    normally checkpoints a process tree rooted at a specific PID.
#    Strategy: checkpoint each component separately into its own
#    subdirectory.
#
# 3. Add graphctl commands:
#    - graphctl checkpoint <component> [--dir /path]
#      Checkpoints a single component
#    - graphctl checkpoint-all [--dir /path]
#      Checkpoints all active components
#    - graphctl restore <component> [--dir /path]
#      Restores a single component
#
# 4. Checkpoint storage:
#    - Default location: /run/graph/checkpoints/<component>/
#    - Each checkpoint is a directory with CRIU dump files
#    - Keep only the latest checkpoint per component (disk space)
#
# 5. Handle checkpoint edge cases:
#    - Component with open network sockets: CRIU can handle TCP
#      with --tcp-established flag, but connections may break
#      if IP changes. Document limitations.
#    - Component with open files: works if files still exist at
#      same paths after restore.
#    - Component using /dev/random: needs --ext-unix-sk for
#      external unix sockets.
#
# HOW TO TEST:
#   - Start a simple component (e.g., a counter that writes to a
#     file every second)
#   - Checkpoint it
#   - Kill it
#   - Restore it
#   - Verify: counter continues from where it was checkpointed
#   - Checkpoint-all with multiple running components
#   - Kill all of them
#   - Restore all
#   - Verify: all continue from checkpoint
#
# NOTE: CRIU testing requires root and specific kernel support.
# Tests should detect missing prerequisites and skip gracefully.
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 9"
#   - Note CRIU version tested against
#   - Note any kernel config requirements
#
# DONE WHEN:
# - Single component checkpoint/restore works
# - Bulk checkpoint/restore works
# - graphctl commands work
# - Missing CRIU is handled gracefully (logged, not crash)
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 10: KEXEC LIVE KERNEL UPGRADE                         │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has CRIU integration. Check PROGRESS.md.
#
# GOAL: Upgrade the running kernel without rebooting. This is the
# crown jewel of YakirOS — combined with fd-passing hot-swap, the
# system never needs to reboot for any reason.
#
# HOW KEXEC WORKS:
# kexec is a Linux syscall that loads a new kernel into memory and
# jumps to it, bypassing BIOS/UEFI. It's like a fast reboot that
# skips the bootloader and hardware initialization.
#
#   kexec_load() — stage a new kernel in memory
#   kexec_exec() — jump to it (no return)
#
# The challenge: kexec destroys all userspace. We need to save
# and restore it using CRIU (Step 9).
#
# THE KEXEC SEQUENCE:
#
#   1. graphctl kexec /boot/vmlinuz-new [--initrd /boot/initrd-new]
#   2. Resolver validates new kernel exists
#   3. CRIU checkpoints ALL managed processes → /run/graph/checkpoints/
#   4. Copy checkpoints to a tmpfs or location that survives kexec
#      (this is tricky — we need a ram-based fs that the new kernel
#      can find. Options: initrd with checkpoints baked in, or a
#      reserved memory region, or a disk partition.)
#   5. Stage new kernel: kexec -l /boot/vmlinuz-new --initrd=...
#   6. Execute: kexec -e
#   7. --- NEW KERNEL BOOTS ---
#   8. New kernel hands off to graph-resolver (PID 1)
#   9. Resolver detects checkpoint data exists
#   10. Mounts virtual filesystems (Layer 1)
#   11. Restores processes from checkpoints via CRIU
#   12. Graph re-resolves — capabilities re-registered
#   13. System continues as if nothing happened
#
# WHAT TO DO:
#
# 1. Create src/kexec.h / src/kexec.c:
#
#    /* Validate a kernel image */
#    int kexec_validate(const char *kernel_path);
#    
#    /* Perform the full kexec sequence */
#    int kexec_perform(const char *kernel_path,
#                      const char *initrd_path,
#                      const char *cmdline);
#
# 2. Checkpoint persistence strategy:
#    SIMPLEST APPROACH for the prototype:
#    - Use a dedicated disk partition (e.g., /dev/sda2 mounted
#      at /checkpoint) that persists across kexec.
#    - Before kexec, write checkpoints there.
#    - After kexec, new resolver checks /checkpoint for data.
#    - If found, restore all, then clean up.
#
#    Add to graph-resolver main():
#    ```c
#    /* Check for post-kexec restoration */
#    if (access("/checkpoint/manifest.json", F_OK) == 0) {
#        LOG_INFO("post-kexec: restoring from checkpoint");
#        restore_all("/checkpoint");
#        unlink("/checkpoint/manifest.json");
#    }
#    ```
#
# 3. Add to graphctl:
#    - graphctl kexec <kernel> [--initrd <path>] [--append <cmdline>]
#      Triggers the full sequence.
#    - graphctl kexec --dry-run <kernel>
#      Validates the kernel and shows what would happen without
#      actually doing it.
#
# 4. Safety measures:
#    - Verify kernel image before loading (file exists, reasonable
#      size, starts with valid magic bytes)
#    - Verify CRIU checkpoints succeeded before kexec
#    - If any checkpoint fails, abort the kexec
#    - Log extensively — this is the most dangerous operation
#    - Keep the old kernel path in a file so we can report what
#      we came from after restore
#
# 5. Kernel command line:
#    The new kernel needs to know where graph-resolver is:
#      init=/sbin/graph-resolver
#    And where to find checkpoints:
#      spliceos.checkpoint=/checkpoint
#    Parse this from /proc/cmdline at startup.
#
# HOW TO TEST:
#   THIS MUST BE TESTED IN A VM. Never on bare metal during dev.
#   
#   1. Set up QEMU VM with YakirOS (see Step 11 for full VM setup)
#   2. Start several components, let them run
#   3. Build a new kernel (even the same version is fine for testing)
#   4. Run graphctl kexec /boot/vmlinuz-new
#   5. Verify: VM is running new kernel (uname -r)
#   6. Verify: all previously running components are still running
#   7. Verify: component state is preserved (counters continue, etc.)
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 10"
#   - Note the checkpoint persistence strategy chosen
#   - Note any limitations discovered
#
# DONE WHEN:
# - kexec sequence works in a VM
# - Processes survive the kernel upgrade
# - Safety checks prevent kexec with failed checkpoints
# - graphctl kexec --dry-run works
# - All prior tests still pass


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 11: VM INTEGRATION TESTING WITH QEMU                  │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has all major features. Check PROGRESS.md.
#
# GOAL: Create a complete VM test environment where YakirOS runs
# as the real init system. This validates everything end-to-end.
#
# WHAT TO DO:
#
# 1. Create scripts/build-vm.sh:
#    An automated script that builds a minimal bootable VM image
#    with YakirOS. NOT a full LFS build (that takes days).
#    Instead:
#
#    a. Create a raw disk image (1GB is enough)
#    b. Partition: 512MB root, 256MB checkpoint, 256MB swap
#    c. Format as ext4
#    d. Install a minimal root filesystem:
#       - busybox (provides sh, mount, ls, cat, ps, etc.)
#       - graph-resolver and graphctl (our binaries)
#       - Component declarations in /etc/graph.d/
#       - A prebuilt kernel (download a minimal config kernel or
#         use the host's kernel if compatible)
#    e. Install GRUB or use direct kernel boot
#    f. Output: spliceos-vm.qcow2
#
#    This should run in ~60 seconds, not hours. Use busybox
#    instead of full coreutils. The goal is testing, not production.
#
# 2. Create scripts/run-vm.sh:
#    ```bash
#    qemu-system-x86_64 \
#        -m 2G -smp 2 -enable-kvm \
#        -drive file=spliceos-vm.qcow2,format=qcow2 \
#        -nographic \
#        -append "console=ttyS0 init=/sbin/graph-resolver" \
#        -kernel /path/to/vmlinuz \
#        -initrd /path/to/initrd  # optional
#    ```
#
# 3. Create scripts/vm-test.sh:
#    Automated test that:
#    a. Boots the VM
#    b. Waits for the system to come up (monitor serial console
#       for "graph stable" message)
#    c. Connects via serial console or SSH (if networking works)
#    d. Runs graphctl status, verifies expected components
#    e. Kills a component, verifies restart
#    f. Drops a new .toml into graph.d, verifies activation
#    g. Tests hot-swap if echo-server is running
#    h. Shuts down cleanly
#    i. Reports results
#
# 4. Adapt component declarations for busybox:
#    busybox provides many tools but at different paths.
#    Create a graph.d/ set that works with busybox:
#    - mount commands use busybox mount
#    - /sbin/udevd becomes mdev (busybox device manager)
#    - Network uses busybox ifconfig/route or ip
#
# 5. Create a minimal kernel config doc (docs/kernel-config.md):
#    Required options:
#      CONFIG_CHECKPOINT_RESTORE=y  (for CRIU)
#      CONFIG_NAMESPACES=y
#      CONFIG_CGROUPS=y
#      CONFIG_TMPFS=y
#      CONFIG_DEVTMPFS=y
#      CONFIG_DEVTMPFS_MOUNT=y
#      CONFIG_INOTIFY_USER=y
#      CONFIG_EPOLL=y
#      CONFIG_UNIX=y
#      CONFIG_KEXEC=y
#      CONFIG_KEXEC_FILE=y
#      CONFIG_SERIAL_8250=y         (for QEMU console)
#      CONFIG_SERIAL_8250_CONSOLE=y
#
# HOW TO TEST:
#   - scripts/build-vm.sh completes without errors
#   - scripts/run-vm.sh boots to a graph-resolver prompt
#   - graphctl status shows expected components
#   - scripts/vm-test.sh passes all checks
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 11"
#   - Note VM boot time
#   - Note any components that needed adaptation
#
# DONE WHEN:
# - VM boots with YakirOS as PID 1
# - Components activate reactively (not sequentially)
# - graphctl works inside the VM
# - At least basic component lifecycle works (start, crash, restart)
# - Automated test script exists and passes


# ┌─────────────────────────────────────────────────────────────┐
# │  STEP 12: DOCUMENTATION AND POLISH                          │
# └─────────────────────────────────────────────────────────────┘
#
# CONTEXT: ~/yakiros/ has all features and VM testing.
# Check PROGRESS.md.
#
# GOAL: Make the project ready for public consumption. Clean code,
# comprehensive docs, clear examples.
#
# WHAT TO DO:
#
# 1. README.md — complete rewrite:
#    - What YakirOS is (elevator pitch)
#    - Why it exists (the philosophy)
#    - Quick start (build and test in 5 minutes)
#    - Architecture overview with ASCII diagram
#    - Component declaration reference
#    - graphctl command reference
#    - FAQ: "How is this different from systemd?"
#      (systemd has socket activation but still thinks in terms
#       of boot targets and ordered sequences. YakirOS has no
#       concept of "booted" — the graph is always resolving.
#       Also: hot-swap and live kernel upgrade.)
#
# 2. docs/architecture.md:
#    - Detailed design of the graph resolver
#    - State machine diagram for components
#    - Capability model explanation
#    - Hot-swap protocol specification
#    - kexec sequence specification
#    - Security considerations
#
# 3. docs/component-reference.md:
#    - Complete TOML schema for component declarations
#    - Every field documented with type, default, and example
#    - Best practices for writing components
#
# 4. docs/building-for-lfs.md:
#    - Step-by-step guide for adding YakirOS to an LFS build
#    - Kernel config requirements
#    - How to migrate from sysvinit or systemd
#
# 5. Code cleanup:
#    - Consistent formatting (pick a style, apply it)
#    - Every function has a doc comment
#    - Every header has include guards
#    - No dead code
#    - No TODO comments without a plan
#    - Run with -fsanitize=address,undefined under tests
#
# 6. License: add LICENSE file (choose one — probably MIT or
#    public domain/0BSD given the "do what you want" intent)
#
# 7. Create a CHANGELOG.md summarizing what was built.
#
# 8. Final test pass:
#    - make clean && make (zero warnings)
#    - make test (all pass)
#    - VM test (all pass)
#    - Valgrind check (no memory leaks in resolver)
#
# HOW TO MARK PROGRESS:
# Edit ~/yakiros/PROGRESS.md:
#   - Check off "Step 12"
#   - Update status to "COMPLETE"
#   - Add final notes
#
# DONE WHEN:
# - All documentation is written and accurate
# - Code is clean and well-commented
# - All tests pass
# - A newcomer could read the README and understand the project
# - A developer could follow building-for-lfs.md and deploy it


# ══════════════════════════════════════════════════════════════
# END OF PLAN
#
# To start working: open ~/yakiros/PROGRESS.md, find the current
# step, then read ONLY that step above. Each step is self-contained.
# ══════════════════════════════════════════════════════════════
