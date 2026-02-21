# Test Components for Readiness Protocol

This directory contains example components that demonstrate the YakirOS readiness protocol.

## Components

### File-based Readiness Components

**fast-web-server** (`fast-web-server.sh`)
- Signals readiness quickly by creating `/tmp/fast-web-server.ready`
- Provides `http-server` capability
- Timeout: 30 seconds

**slow-database** (`slow-database.sh`)
- Takes 10 seconds to become ready, creates `/tmp/slow-database.ready`
- Provides `database` capability
- Timeout: 15 seconds

**failing-service** (`failing-service.sh`)
- Never signals readiness (for testing timeout scenarios)
- Provides `broken-service` capability
- Timeout: 8 seconds (will timeout and fail)

### Command-based Readiness Components

**api-service** (`api-service.sh`)
- Uses health check command `./test-components/api-health-check.sh`
- Requires `network` capability, provides `api` capability
- Timeout: 30 seconds, check interval: 5 seconds

**flaky-service** (`flaky-service.sh`)
- Randomly succeeds or fails health checks
- Uses health check command `./test-components/flaky-health-check.sh`
- Requires `logging` capability, provides `flaky-capability`
- Timeout: 20 seconds, check interval: 3 seconds

### Signal-based Readiness Components

**log-daemon** (`log-daemon.sh`)
- Signals readiness by sending SIGUSR1 to parent process
- Provides `logging` capability
- Timeout: 10 seconds

## Usage

1. Copy component TOML files to `/tmp/yakiros/graph.d/`:
```bash
mkdir -p /tmp/yakiros/graph.d
cp test-components/*.toml /tmp/yakiros/graph.d/
```

2. Run the graph resolver:
```bash
sudo ./graph-resolver
```

3. Monitor component status:
```bash
./graphctl status
```

## Expected Behavior

- `fast-web-server`: Should become active within 2-3 seconds
- `slow-database`: Should become active after ~10 seconds
- `api-service`: Should become active after ~4 seconds
- `log-daemon`: Should become active within 3-4 seconds
- `flaky-service`: May become active or fail randomly
- `failing-service`: Should timeout after 8 seconds and fail

These components demonstrate different readiness patterns and help test the robustness of the readiness protocol implementation.