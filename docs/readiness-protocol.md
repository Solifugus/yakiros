# YakirOS Readiness Protocol Design

## Overview

The readiness protocol ensures that components are truly ready to provide their capabilities before the system considers their dependencies satisfied. This prevents premature dependency resolution and improves system reliability.

## Current Problem

Currently, YakirOS immediately marks components as `COMP_ACTIVE` after `fork()`/`exec()`, even though:
- The process may still be initializing
- Databases might be loading schemas
- Network services might be binding to ports
- Dependent services might fail because the capability isn't truly available

## Readiness Signaling Mechanisms

### 1. File-based Signaling (Recommended)

Components create a readiness file to signal they're ready:

```toml
[component]
name = "web-server"
binary = "/usr/bin/nginx"

[lifecycle]
readiness_file = "/run/nginx.ready"
readiness_timeout = 30
```

**Process:**
1. Component starts, YakirOS monitors `/run/nginx.ready`
2. Component creates the file when ready
3. YakirOS detects file creation via `inotify`, marks component `COMP_ACTIVE`
4. If timeout expires before file creation, component marked `COMP_FAILED`

### 2. Signal-based Signaling

Components send `SIGUSR1` to parent (graph-resolver) when ready:

```toml
[component]
name = "database"
binary = "/usr/bin/postgres"

[lifecycle]
readiness_signal = "SIGUSR1"
readiness_timeout = 60
```

### 3. Health Check Command

YakirOS periodically runs a command to verify readiness:

```toml
[component]
name = "database"
binary = "/usr/bin/postgres"

[lifecycle]
readiness_check = "/usr/bin/pg_isready"
readiness_timeout = 45
readiness_interval = 5
```

**Priority:** File-based is recommended as it's simple, reliable, and doesn't require signal handling coordination.

## Component State Machine Enhancement

### New States

Current: `INACTIVE` → `STARTING` → `ACTIVE`

Enhanced: `INACTIVE` → `STARTING` → `READY_WAIT` → `ACTIVE`

```c
typedef enum {
    COMP_INACTIVE,           /* requirements not met */
    COMP_STARTING,          /* process launching */
    COMP_READY_WAIT,        /* process launched, waiting for readiness signal */
    COMP_ACTIVE,            /* running and providing capabilities */
    COMP_FAILED,            /* crashed or readiness timeout */
    COMP_ONESHOT_DONE,      /* oneshot completed successfully */
} comp_state_t;
```

### Component Structure Extensions

```c
typedef struct {
    /* ... existing fields ... */

    /* Readiness protocol fields */
    char     readiness_file[MAX_PATH];     /* file to monitor for readiness */
    char     readiness_check[MAX_PATH];    /* command to check readiness */
    int      readiness_signal;             /* signal number for readiness */
    int      readiness_timeout;            /* timeout in seconds */
    int      readiness_interval;           /* check interval for health checks */
    time_t   ready_wait_start;             /* when READY_WAIT state started */
    int      readiness_method;             /* which method is configured */
} component_t;
```

### Readiness Methods

```c
typedef enum {
    READINESS_NONE,          /* no readiness check (immediate active) */
    READINESS_FILE,          /* monitor file creation */
    READINESS_SIGNAL,        /* wait for signal from component */
    READINESS_COMMAND,       /* run health check command */
} readiness_method_t;
```

## Implementation Plan

### Phase 1: Core Infrastructure
1. Extend `component_t` structure with readiness fields
2. Update TOML parser to handle `[lifecycle]` readiness configuration
3. Modify `component_start()` to transition to `COMP_READY_WAIT` instead of `COMP_ACTIVE`
4. Implement basic timeout handling

### Phase 2: Readiness Detection
1. Implement file-based readiness monitoring with `inotify`
2. Add signal handler for readiness signals from components
3. Implement health check command execution
4. Integrate readiness detection with main event loop

### Phase 3: Graph Integration
1. Update `graph_resolve()` to respect readiness states
2. Only register capabilities when components reach `COMP_ACTIVE`
3. Handle readiness timeouts in graph resolution
4. Update dependency checking logic

### Phase 4: Testing & Validation
1. Create test components that use different readiness methods
2. Update existing tests for new state machine
3. Add comprehensive readiness protocol tests
4. Validate timeout handling and failure scenarios

## Example Component Configurations

### Web Server (File-based)
```toml
[component]
name = "nginx"
type = "service"
binary = "/usr/bin/nginx"
args = ["-g", "daemon off;"]

[requires]
capabilities = ["network"]

[provides]
capabilities = ["http-server"]

[lifecycle]
readiness_file = "/run/nginx.ready"
readiness_timeout = 30
```

### Database (Health Check)
```toml
[component]
name = "postgres"
type = "service"
binary = "/usr/bin/postgres"

[requires]
capabilities = ["filesystem"]

[provides]
capabilities = ["database"]

[lifecycle]
readiness_check = "/usr/bin/pg_isready -h localhost"
readiness_timeout = 60
readiness_interval = 5
```

### Fast Service (Signal-based)
```toml
[component]
name = "log-daemon"
type = "service"
binary = "/usr/sbin/syslogd"

[provides]
capabilities = ["logging"]

[lifecycle]
readiness_signal = "SIGUSR1"
readiness_timeout = 10
```

## Timeout Handling

- **Default timeout:** 30 seconds
- **Timeout behavior:** Component marked `COMP_FAILED`, capabilities withdrawn
- **Retry logic:** Failed components can be restarted by graph resolution
- **Timeout configuration:** Per-component in TOML `readiness_timeout` field

## Backward Compatibility

Components without readiness configuration use `READINESS_NONE`:
- Immediate transition `STARTING` → `ACTIVE` (current behavior)
- Ensures existing components continue working
- Gradual migration path to readiness protocol

## Integration Points

### Main Event Loop
- Monitor `inotify` events for readiness files
- Handle readiness signals (`SIGUSR1`)
- Execute periodic health checks
- Process readiness timeouts

### Graph Resolution
```c
// Only components in ACTIVE state provide capabilities
int graph_resolve(void) {
    for (int i = 0; i < n_components; i++) {
        if (components[i].state == COMP_READY_WAIT) {
            check_readiness_timeout(i);
        }
        // ... existing logic
    }
}
```

### Process Supervision
- Readiness state tracking in `component_exited()`
- Proper cleanup of readiness files on component death
- Signal handler registration for readiness signals

## Benefits

1. **Reliability:** Services truly ready before dependents start
2. **Debugging:** Clear visibility into startup progress via `graphctl`
3. **Robustness:** Timeout handling prevents hung startup sequences
4. **Flexibility:** Multiple signaling mechanisms for different service types
5. **Compatibility:** Graceful fallback for components without readiness config

## Success Criteria

- [ ] Components stay in `COMP_READY_WAIT` until signaling readiness
- [ ] Capabilities only registered when components are `COMP_ACTIVE`
- [ ] Timeout handling prevents indefinite waits
- [ ] All three readiness mechanisms (file/signal/command) working
- [ ] Existing tests pass with readiness protocol enabled
- [ ] New readiness-specific tests validate protocol behavior