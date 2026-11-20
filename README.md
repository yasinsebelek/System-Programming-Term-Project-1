# Watchdog – POSIX Signal-Based Process Monitoring and Management System

A modular C11 / POSIX supervisor application developed for a university System Programming term project. The `watchdog` parent process creates, monitors, tracks, restarts, logs, and gracefully shuts down independent `worker` child processes using standard POSIX system calls.

---

## Project Directory Structure

```text
watchdog-project/
├── Makefile                    # Builds bin/watchdog, bin/worker, ./watchdog symlink, and test target
├── README.md                   # Project documentation, build/test guide, and architecture overview
├── bin/
│   ├── watchdog                # Watchdog supervisor executable
│   └── worker                  # Independent worker executable
├── include/
│   ├── logger.h                # Timestamped POSIX logging API
│   ├── process_manager.h       # Worker process table, states, lifecycle & restart API
│   └── signal_manager.h        # Async-signal-safe sigaction() + setitimer() API
├── src/
│   ├── main.c                  # Entry point, CLI parsing, event loop, shutdown coordination
│   ├── process_manager.c       # fork(), execv(), waitpid(WNOHANG), restart limits, shutdown
│   ├── signal_manager.c        # sigaction() handlers, volatile sig_atomic_t flags, sigsuspend()
│   └── logger.c                # POSIX open()/write()/close() timestamped logging
├── workers/
│   └── worker.c                # Independent worker binary with PID/PPID output & test modes
├── logs/
│   └── watchdog.log            # Timestamped runtime event log
├── tests/
│   └── test_scenarios.sh       # Automated test suite covering all mandatory scenarios
└── docs/
    └── REPORT.pdf              # Final project report
```

---

## Architecture Overview

The system separates supervision from work across independent OS processes:

1. **Parent Process (`bin/watchdog`)**:
   - Initializes POSIX file logging (`logs/watchdog.log`) using `open(..., O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644)`.
   - Installs async-signal-safe handlers via `sigaction()` for `SIGCHLD`, `SIGUSR1`, `SIGINT`, `SIGTERM`, and `SIGALRM`, and configures a periodic `ITIMER_REAL` timer via `setitimer()`.
   - Spawns at least two independent worker processes using `fork()` + `execv()`, maintaining an internal `ProcessManager` table (`id`, `pid`, `state`, `restart_count`, `is_running`).
   - Runs a race-free event loop using `sigprocmask()` + `sigsuspend()` (0% idle CPU, zero missed wakeups).
   - Reaps all terminated children with `waitpid(-1, &status, WNOHANG)` in a loop to prevent zombie processes.
   - Applies the restart policy: normal exits (`WIFEXITED && WEXITSTATUS == 0`) transition to `STOPPED` with no restart; non-zero exits and signal terminations trigger up to **3 automatic restarts** per worker before marking the worker `FAILED`.
   - Performs controlled graceful shutdown on `SIGINT` / `SIGTERM`: stops timer, stops launching new workers, sends `SIGTERM` to running workers, waits up to the grace timeout (`5s` default), escalates unresponsive workers to `SIGKILL`, reaps all remaining children, closes logs, and exits cleanly.

2. **Child Processes (`bin/worker`)**:
   - Independent OS processes with distinct PIDs and `PPID == watchdog PID`.
   - Periodically output heartbeat messages showing PID and PPID.
   - Support test modes for deterministic verification:
     - Default: runs continuously until signaled.
     - `SIGUSR2` or `--exit-after=N`: exits normally with exit status `0`.
     - `--fail-after=N`: exits unexpectedly with exit status `1`.
     - `--ignore-term`: ignores `SIGTERM` to verify Watchdog's `SIGKILL` escalation fallback.

---

## Required POSIX Mechanisms Implemented

| Mechanism | Purpose in Watchdog | Source File |
| :--- | :--- | :--- |
| `fork()` | Creates independent child worker processes | `src/process_manager.c` |
| `execv()` | Replaces child process image with `bin/worker` | `src/process_manager.c` |
| `sigaction()` | Registers reliable POSIX signal handlers | `src/signal_manager.c`, `workers/worker.c` |
| `SIGCHLD` | Notifies parent of child process termination | `src/signal_manager.c`, `src/process_manager.c` |
| `waitpid(..., WNOHANG)` | Reaps terminated children non-blockingly (prevents zombies) | `src/process_manager.c` |
| `SIGUSR1` | Triggers worker status report written to `logs/watchdog.log` | `src/signal_manager.c`, `src/process_manager.c` |
| `SIGINT` | Initiates graceful shutdown (Ctrl+C) | `src/signal_manager.c`, `src/main.c` |
| `SIGTERM` | Initiates graceful shutdown of watchdog and workers | `src/signal_manager.c`, `src/process_manager.c` |
| `SIGKILL` | Forcefully terminates workers that ignore `SIGTERM` after timeout | `src/process_manager.c` |
| `kill()` | Delivers `SIGTERM`/`SIGKILL` and performs `kill(pid, 0)` liveness checks | `src/process_manager.c` |
| `setitimer()` + `SIGALRM` | Drives periodic worker health checks | `src/signal_manager.c`, `src/process_manager.c` |
| `open()` / `write()` / `close()` | Writes timestamped audit records to `logs/watchdog.log` | `src/logger.c` |

---

## Build & Run Instructions

### Compile

```bash
make
```

Generates:
- `bin/watchdog` (and convenience root symlink `./watchdog`)
- `bin/worker`

### Clean

```bash
make clean
```

### Run Watchdog

```bash
./watchdog
# or
./bin/watchdog
# or
make run
```

Optional CLI flags:

```text
--workers N            Number of worker processes (1-16, default: 2)
--check-interval N     Periodic SIGALRM health check interval in seconds (default: 5)
--shutdown-timeout N   Graceful shutdown timeout in seconds before SIGKILL (default: 5)
--log-file PATH        Log output file path (default: logs/watchdog.log)
--worker1-mode MODE    Optional worker 1 test mode (--exit-after=N, --fail-after=N, --ignore-term)
--worker2-mode MODE    Optional worker 2 test mode
--help                 Show usage information
```

---

## Interactive Manual Testing Commands

1. **Start Watchdog:** `./watchdog`
2. **Request Status Report (`SIGUSR1`):**
   ```bash
   kill -SIGUSR1 <WATCHDOG_PID>
   tail -n 10 logs/watchdog.log
   ```
3. **Simulate Unexpected Worker Crash (`SIGKILL`):**
   ```bash
   kill -SIGKILL <WORKER_PID>
   ```
4. **Simulate Normal Worker Exit (code 0, no restart):**
   ```bash
   kill -SIGUSR2 <WORKER_PID>
   ```
5. **Trigger Graceful Shutdown (`SIGTERM` or `Ctrl+C`):**
   ```bash
   kill -SIGTERM <WATCHDOG_PID>
   ```

---

## Automated Test Suite

Run all 8 mandatory test scenarios plus bonus verification tests (`SIGKILL` escalation and non-zero exit code recovery):

```bash
make test
# or
bash tests/test_scenarios.sh
```

---

## Sample Log Output (`logs/watchdog.log`)

```text
[2026-11-19 19:04:17] WATCHDOG STARTED PID=9647
[2026-11-19 19:04:17] WORKER 1 STARTED PID=9650
[2026-11-19 19:04:17] WORKER 2 STARTED PID=9651
[2026-11-19 19:04:18] PERIODIC HEALTH CHECK RUNNING=2/2
[2026-11-19 19:04:18] STATUS REPORT
[2026-11-19 19:04:18] WORKER 1 PID=9650 STATE=RUNNING RESTARTS=0
[2026-11-19 19:04:18] WORKER 2 PID=9651 STATE=RUNNING RESTARTS=0
[2026-11-19 19:04:19] WORKER 1 TERMINATED UNEXPECTEDLY PID=9650 SIGNAL=SIGKILL(9)
[2026-11-19 19:04:19] WORKER 1 RESTART ATTEMPT 1
[2026-11-19 19:04:19] WORKER 1 STARTED PID=9679
[2026-11-19 19:04:19] WORKER 1 RESTART LIMIT REACHED (MAX=3) STATE=FAILED
[2026-11-19 19:04:19] GRACEFUL SHUTDOWN INITIATED SIGNAL=SIGTERM(15)
[2026-11-19 19:04:19] SENDING SIGTERM TO WORKER 2 PID=9651
[2026-11-19 19:04:19] WORKER 2 TERMINATED DURING SHUTDOWN PID=9651
[2026-11-19 19:04:19] WATCHDOG SHUTDOWN COMPLETE
```

---

## Group Task Allocation & Git Workflow Guide

> **Note:** Per academic integrity rules, each group member should commit their assigned module using their own GitHub account rather than fabricating commit authorship.

| Module | Files | Primary Responsibility | Suggested Commit Message |
| :--- | :--- | :--- | :--- |
| Build & Worker | `Makefile`, `workers/worker.c` | Standalone worker process, heartbeat, signal/test modes, build rules | `feat(worker): implement standalone POSIX worker and Makefile` |
| Process Manager | `include/process_manager.h`, `src/process_manager.c` | `fork()`/`execv()`, state table, `waitpid()`, restart limit, graceful shutdown | `feat(process): implement worker lifecycle, reaping, restart limit, and shutdown` |
| Signal Manager | `include/signal_manager.h`, `src/signal_manager.c` | Async-signal-safe `sigaction()` handlers, `setitimer()`, `sigsuspend()` loop | `feat(signals): add async-signal-safe sigaction handlers and periodic timer` |
| Logging & Entry | `include/logger.h`, `src/logger.c`, `src/main.c` | POSIX `write()` timestamped logging, CLI parsing, main event loop | `feat(logger): add POSIX timestamped logger and main supervision loop` |
| Testing & Docs | `tests/test_scenarios.sh`, `README.md`, `docs/REPORT.pdf` | End-to-end verification scenarios, documentation, final PDF report | `test(docs): add automated test suite, README, and final project report` |
