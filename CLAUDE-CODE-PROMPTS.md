# YakirOS — Claude Code Prompt Template
#
# Copy and paste the appropriate prompt below into Claude Code
# to execute each step. Each prompt is self-contained.
#
# Before each step: make sure ~/yakiros/ contains the latest code.
# After each step: verify PROGRESS.md was updated.


# ═══════════════════════════════════════════════════════════════
# GENERIC PROMPT (works for any step)
# ═══════════════════════════════════════════════════════════════
# 
# Use this prompt to continue from wherever the project left off:

"""
You are working on YakirOS, a reactive dependency-graph-driven
Linux init system. The project is at ~/yakiros/.

First, read ~/yakiros/PROGRESS.md to find the current step.
Then read ~/yakiros/PLAN.md and find the section for that step.
Execute that step completely, following all instructions.

When done:
1. Verify everything compiles: make clean && make
2. Run any existing tests: make test (if tests exist)
3. Update PROGRESS.md: check off the completed step, bump
   "Current step" to the next number, update the date, and add
   any notes about what was done or decided.

If you hit a blocker, note it in PROGRESS.md under Notes and
move on to what you can complete.
"""


# ═══════════════════════════════════════════════════════════════
# STEP-SPECIFIC PROMPTS (if you want to target a specific step)
# ═══════════════════════════════════════════════════════════════

# --- Step 0 ---
"""
You are working on YakirOS at ~/yakiros/. Read PLAN.md, find
Step 0 (Project Initialization), and execute it. Create the
directory structure, verify any existing code compiles, and set
up PROGRESS.md. When done, mark Step 0 complete in PROGRESS.md.
"""

# --- Step 1 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 0 is done, then read PLAN.md Step 1 (Modularize and
Harden). Split the monolithic graph-resolver.c into modules with
clean headers. Harden for PID 1 use. Must compile with
-Wall -Wextra -Werror, zero warnings. Update PROGRESS.md when done.
"""

# --- Step 2 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 1 is done, then read PLAN.md Step 2 (Test Harness).
Build a comprehensive test framework. Add --config-dir and
--control-socket arguments to graph-resolver. Create at least 6
test scripts that exercise TOML parsing, graph resolution, restart
behavior, capability withdrawal, inotify, and oneshot components.
All tests must pass. Update PROGRESS.md when done.
"""

# --- Step 3 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 2 is done, then read PLAN.md Step 3 (Readiness
Protocol). Implement three readiness modes: immediate, notify
(via pipe/fd), and file (inotify on a path). Add readiness_timeout.
Downstream components must wait for readiness before activating.
Add tests. Update PROGRESS.md when done.
"""

# --- Step 4 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 3 is done, then read PLAN.md Step 4 (FD Passing
Hot-Swap). This is the core feature. Implement SCM_RIGHTS fd
passing, the hot-swap protocol, and graphctl upgrade. Build a
test echo server that survives hot-swap with connected clients.
This must work. Update PROGRESS.md when done.
"""

# --- Step 5 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 4 is done, then read PLAN.md Step 5 (graphctl
Enhancements). Add: caps, tree (recursive), rdeps, simulate
remove, dot (graphviz), log, color output, per-component logging.
Update PROGRESS.md when done.
"""

# --- Step 6 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 5 is done, then read PLAN.md Step 6 (Health Checks).
Implement periodic health check execution, DEGRADED state,
recovery, and auto-restart on persistent failure. Create example
health check scripts. Add tests. Update PROGRESS.md when done.
"""

# --- Step 7 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 6 is done, then read PLAN.md Step 7 (cgroup and
Namespace Isolation). Implement cgroup v2 creation, resource limits
(memory_max, cpu_weight, pids_max), mount namespace isolation, and
OOM detection. Add tests. Update PROGRESS.md when done.
"""

# --- Step 8 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 7 is done, then read PLAN.md Step 8 (Cycle Detection).
Implement topological sort via Kahn's algorithm, cycle detection at
load time, graphctl check and graphctl order commands. Cycles must
not prevent non-cycling components from activating. Add tests.
Update PROGRESS.md when done.
"""

# --- Step 9 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 8 is done, then read PLAN.md Step 9 (CRIU
Integration). Implement checkpoint/restore for individual
components and bulk operations. Handle missing CRIU gracefully.
Add graphctl checkpoint and restore commands. Add tests (skip
if CRIU unavailable). Update PROGRESS.md when done.
"""

# --- Step 10 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 9 is done, then read PLAN.md Step 10 (kexec Live
Kernel Upgrade). Implement the full kexec sequence: checkpoint all,
stage new kernel, kexec, restore all. Add safety checks and
dry-run mode. THIS MUST ONLY BE TESTED IN A VM. Update PROGRESS.md
when done.
"""

# --- Step 11 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 10 is done, then read PLAN.md Step 11 (VM Testing).
Create scripts to build a minimal bootable VM image using busybox,
run it in QEMU, and execute automated tests. Adapt component
declarations for busybox. Document kernel config requirements.
Update PROGRESS.md when done.
"""

# --- Step 12 ---
"""
You are working on YakirOS at ~/yakiros/. Read PROGRESS.md to
confirm Step 11 is done, then read PLAN.md Step 12 (Documentation
and Polish). Rewrite README, create architecture docs, component
reference, LFS migration guide. Clean up all code, add doc
comments, run sanitizers. Final test pass. Update PROGRESS.md to
COMPLETE when done.
"""
