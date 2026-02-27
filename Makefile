# YakirOS Build System
#
# Prerequisites:
#   - musl-gcc (for static builds) OR gcc
#   - A working LFS toolchain
#
# Targets:
#   make              - build everything
#   make static       - build with musl (for PID 1 use)
#   make test         - build for testing on existing system
#   make install      - install to DESTDIR (default /)
#   make vm-test      - run in QEMU (requires yakiros.qcow2)
#   make build-tests  - build all test executables
#   make test-unit    - run unit tests
#   make test-integration - run integration tests
#   make test-all     - run all tests

CC ?= gcc
MUSL_CC ?= musl-gcc
CFLAGS = -Wall -Wextra -Werror -O2 -std=c11 -Isrc
LDFLAGS =
DESTDIR ?= /

# Source files for modular build
RESOLVER_SRCS = src/graph-resolver.c src/log.c src/toml.c src/capability.c \
                src/component.c src/graph.c src/control.c src/handoff.c src/cgroup.c
RESOLVER_OBJS = $(RESOLVER_SRCS:.c=.o)

BINS = graph-resolver graphctl

# Default: dynamic build for testing
all: $(BINS)

graph-resolver: $(RESOLVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(RESOLVER_OBJS) $(LDFLAGS)

graphctl: src/graphctl.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Test executables
UNIT_TESTS = tests/unit/test_toml tests/unit/test_toml_readiness tests/unit/test_capability tests/unit/test_component \
             tests/unit/test_graph tests/unit/test_log tests/unit/test_control tests/unit/test_handoff tests/unit/test_isolation \
             tests/unit/test_cycle_detection
INTEGRATION_TESTS = tests/integration/test_full_system tests/integration/test_hotswap tests/integration/test_isolation_integration \
                    tests/integration/test_cycle_detection_integration
ALL_TESTS = $(UNIT_TESTS) $(INTEGRATION_TESTS) tests/test_framework_test

# Build all tests
build-tests: $(ALL_TESTS)

# Unit tests
tests/unit/test_toml: tests/unit/test_toml.c src/toml.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_toml_readiness: tests/unit/test_toml_readiness.c src/toml.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_capability: tests/unit/test_capability.c src/capability.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_component: tests/unit/test_component.c src/component.c src/capability.c src/toml.c src/handoff.c src/cgroup.c src/graph.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_graph: tests/unit/test_graph.c src/graph.c src/component.c src/capability.c src/toml.c src/handoff.c src/cgroup.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_log: tests/unit/test_log.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_control: tests/unit/test_control.c src/control.c src/component.c src/capability.c src/toml.c src/handoff.c src/cgroup.c src/graph.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_handoff: tests/unit/test_handoff.c src/handoff.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_isolation: tests/unit/test_isolation.c src/cgroup.c src/component.c src/capability.c src/toml.c src/handoff.c src/graph.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

# Integration tests
tests/integration/test_full_system: tests/integration/test_full_system.c src/component.c src/capability.c src/graph.c src/toml.c src/handoff.c src/cgroup.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/integration/test_hotswap: tests/integration/test_hotswap.c src/component.c src/capability.c src/handoff.c src/toml.c src/cgroup.c src/graph.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/integration/test_isolation_integration: tests/integration/test_isolation_integration.c src/toml.c src/cgroup.c src/component.c src/capability.c src/handoff.c src/graph.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/unit/test_cycle_detection: tests/unit/test_cycle_detection.c src/graph.c src/component.c src/capability.c src/toml.c src/handoff.c src/cgroup.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

tests/integration/test_cycle_detection_integration: tests/integration/test_cycle_detection_integration.c src/component.c src/capability.c src/graph.c src/toml.c src/handoff.c src/cgroup.c src/log.c
	$(CC) $(CFLAGS) -Itests -o $@ $^

# Test framework test
tests/test_framework_test: tests/test_framework_test.c
	$(CC) $(CFLAGS) -Itests -o $@ $<

# Echo server for hot-swap testing
tests/echo-server: tests/echo-server.c src/handoff.c src/log.c
	$(CC) $(CFLAGS) -Isrc -o $@ $^

# Run unit tests
test-unit: $(UNIT_TESTS)
	@echo "Running unit tests..."
	@for test in $(UNIT_TESTS); do \
		echo "=== $$test ==="; \
		if $$test; then \
			echo "PASS: $$test"; \
		else \
			echo "FAIL: $$test"; \
			exit 1; \
		fi; \
	done
	@echo "All unit tests passed!"

# Run integration tests
test-integration: $(INTEGRATION_TESTS)
	@echo "Running integration tests..."
	@for test in $(INTEGRATION_TESTS); do \
		echo "=== $$test ==="; \
		if $$test; then \
			echo "PASS: $$test"; \
		else \
			echo "FAIL: $$test"; \
			exit 1; \
		fi; \
	done
	@echo "All integration tests passed!"

# Run all tests
test-all: test-unit test-integration
	@echo "All tests passed successfully!"

# Clean object files and test binaries
clean:
	rm -f $(BINS) $(RESOLVER_OBJS) $(ALL_TESTS)

# Static build with musl — this is what goes on the real system
static: CC=$(MUSL_CC)
static: LDFLAGS=-static
static: $(BINS)
	@echo "Built static binaries:"
	@file graph-resolver graphctl
	@ls -la graph-resolver graphctl

# Test build — run on existing system (not as PID 1)
test: all
	@echo ""
	@echo "=== Test Mode ==="
	@echo "1. Create test graph dir:  mkdir -p /tmp/yakiros/graph.d"
	@echo "2. Copy example components: cp examples/*.toml /tmp/yakiros/graph.d/"
	@echo "3. Run resolver:           sudo ./graph-resolver"
	@echo "   (will detect non-PID-1 and run in test mode)"
	@echo "4. In another terminal:    ./graphctl status"
	@echo ""

install: static
	install -d $(DESTDIR)/sbin
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/etc/graph.d
	install -d $(DESTDIR)/usr/libexec/graph
	install -m 755 graph-resolver $(DESTDIR)/sbin/graph-resolver
	install -m 755 graphctl $(DESTDIR)/usr/bin/graphctl
	@echo ""
	@echo "Installed. Set kernel command line: init=/sbin/graph-resolver"
	@echo "Place component declarations in /etc/graph.d/*.toml"

# Split the combined component file into individual .toml files
split-components:
	@mkdir -p examples/split
	@echo "Splitting components into individual files..."
	@python3 scripts/split-components.py || echo "split-components.py not found in scripts/"

# Install example component declarations
install-components:
	install -d $(DESTDIR)/etc/graph.d
	@if [ -d examples/split ]; then \
		for f in examples/split/*.toml; do \
			install -m 644 "$$f" $(DESTDIR)/etc/graph.d/; \
			echo "  installed $$(basename $$f)"; \
		done; \
	else \
		echo "Run 'make split-components' first"; \
	fi

# Run in QEMU for testing
vm-test:
	@if [ ! -f yakiros.qcow2 ]; then \
		echo "Creating VM disk..."; \
		qemu-img create -f qcow2 yakiros.qcow2 20G; \
	fi
	qemu-system-x86_64 \
		-m 4G \
		-smp 4 \
		-enable-kvm \
		-drive file=yakiros.qcow2,format=qcow2 \
		-nographic \
		-append "console=ttyS0 init=/sbin/graph-resolver"

.PHONY: all static test install install-components clean vm-test split-components \
        build-tests test-unit test-integration test-all
