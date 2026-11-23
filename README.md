# Watchdog – POSIX Signal-Based Process Monitoring and Management System

A modular C11 / POSIX process supervision system developed for the **Sakarya University Software Engineering Department — System Programming Term Project 1 (2026–2027)**.

The `watchdog` parent process creates, monitors, restarts, reports on, logs, and gracefully shuts down independent `worker` child processes using standard POSIX system calls and async-signal-safe signal handling.

---

## Table of Contents

- [Features](#features)
- [Project Structure](#project-structure)
- [Architecture Overview](#architecture-overview)
- [POSIX Mechanisms Used](#posix-mechanisms-used)
- [Requirements](#requirements)
- [Build & Run](#build--run)
- [Manual Testing & Signal Usage](#manual-testing--signal-usage)
- [Worker Test Modes & Environment Variables](#worker-test-modes--environment-variables)
- [Automated Test Suite](#automated-test-suite)
- [Logging Format & Sample Output](#logging-format--sample-output)
- [Team & Task Allocation](#team--task-allocation)

---

## Features

- **Child Process Creation:** Launches 2 independent worker processes (`WORKER_COUNT = 2`) using `fork()` and `execl()`, with an `FD_CLOEXEC` error pipe to reliably detect and report `execl()` failures in the parent.
- **Termination Monitoring:** Detects child exits asynchronously via `SIGCHLD` and reaps all terminated children in a non-blocking `waitpid(-1, &status, WNOHANG)` loop to prevent zombie processes.
- **Bounded Automatic Restart:** Distinguishes normal exits (`exit(0)` → `STOPPED`, no restart) from error exits (`exit != 0`) and signal terminations (`WIFSIGNALED`). Unexpectedly terminated workers are automatically restarted up to **3 times** (`MAX_RESTARTS = 3`), after which they transition to `FAILED`.
- **Periodic Health Checks:** Uses `setitimer(ITIMER_REAL, ...)` and `SIGALRM` every **5 seconds** to verify worker liveness via `kill(pid, 0)` and log active worker counts.
- **Status Reporting (`SIGUSR1`):** Prints and logs each worker's PID, current state (`RUNNING`, `STOPPED`, `RESTARTING`, `FAILED`), and restart count (`X/3`).
- **Graceful Shutdown (`SIGINT` / `SIGTERM`):** Stops the periodic timer, blocks new restarts, sends `SIGTERM` to all active workers, waits up to **5 seconds** (`SHUTDOWN_TIMEOUT = 5`) for clean exit, and escalates unresponsive workers to `SIGKILL` before reaping them and freeing resources.
- **Structured Timestamped Logging:** Automatically creates `logs/`, writes daily log files (`logs/watchdog_YYYY-MM-DD.log`) with 1 MB size-based rotation, sets `FD_CLOEXEC` on the log file descriptor, and supports an isolated test log via `WATCHDOG_TEST_MODE=1`.

---

## Project Structure

```text
watchdog-project/
├── Makefile                    # Build, test, and clean targets
├── README.md                   # Project documentation
├── include/
│   ├── logger.h                # Logging module interface
│   ├── process_manager.h       # Worker types, states, and lifecycle API
│   └── signal_manager.h        # Signal flags and handler setup API
├── src/
│   ├── main.c                  # Entry point, timer setup, main control loop, shutdown
│   ├── process_manager.c       # fork/execl, waitpid reaping, restarts, periodic checks, shutdown
│   ├── signal_manager.c        # Async-signal-safe sigaction() handlers
│   └── logger.c                # Timestamped daily/rotated file logger
├── workers/
│   └── worker.c                # Independent worker executable source
├── tests/
│   └── test_scenarios.sh       # Automated test suite (7 scenarios)
├── bin/                        # Generated on build (bin/worker)
├── logs/                       # Generated at runtime (watchdog_YYYY-MM-DD.log / watchdog_test.log)
└── docs/
    └── REPORT.pdf              # Project report and commit history
```

---

## Architecture Overview

1. **Signal Handling (`src/signal_manager.c`)**:
   - Installs handlers via `sigaction()` (`SA_RESTART`) for `SIGCHLD`, `SIGINT`, `SIGTERM`, `SIGUSR1`, and `SIGALRM`.
   - Handlers are strictly **async-signal-safe**: they only set `volatile sig_atomic_t` flags (`child_exited`, `shutdown_requested`, `status_requested`, `periodic_check_requested`). No I/O, memory allocation, or process creation is performed inside handlers.

2. **Main Control Loop (`src/main.c`)**:
   - Initializes the logger, installs signal handlers, starts worker processes, and configures a 5-second `ITIMER_REAL` periodic timer.
   - Processes pending signal flags in the main loop and pauses with `nanosleep()`.
   - On `SIGINT` or `SIGTERM`, stops the periodic timer, calls `stop_workers()`, logs shutdown completion, and closes the log file.

3. **Process Supervision (`src/process_manager.c`)**:
   - Maintains a dynamically allocated table of `Worker` structs (`pid`, `restart_count`, `status`).
   - Rolls back startup cleanly if any worker fails to spawn.
   - Handles reaping (`check_children`), bounded restarts (`restart_worker`), periodic liveness checks (`periodic_check_workers`), status reporting (`print_worker_status`), and two-phase graceful shutdown (`SIGTERM` → timeout → `SIGKILL` in `stop_workers`).

4. **Worker Process (`workers/worker.c` → `bin/worker`)**:
   - Runs as a standalone executable printing its `PID`, `PPID`, and a periodic `STATUS: RUNNING` heartbeat every 3 seconds.
   - Ignores `SIGINT` so `Ctrl+C` is coordinated exclusively by `watchdog` via `SIGTERM`.
   - Exits cleanly on `SIGTERM` (graceful shutdown) and exits normally with code `0` on `SIGUSR2`.

---

## POSIX Mechanisms Used

| Mechanism | Purpose | Source File(s) |
| :--- | :--- | :--- |
| `fork()` & `execl()` | Spawn independent `bin/worker` child processes | `src/process_manager.c` |
| `pipe()` & `fcntl(FD_CLOEXEC)` | Detect child `execl()` errors safely & prevent FD leaks | `src/process_manager.c`, `src/logger.c` |
| `sigaction()` | Register reliable, async-signal-safe signal handlers | `src/signal_manager.c`, `workers/worker.c` |
| `SIGCHLD` & `waitpid(..., WNOHANG)` | Detect and reap terminated children without zombies | `src/signal_manager.c`, `src/process_manager.c` |
| `SIGUSR1` | Trigger worker status report to stdout and log file | `src/signal_manager.c`, `src/process_manager.c` |
| `SIGINT` & `SIGTERM` | Initiate controlled graceful shutdown | `src/signal_manager.c`, `src/main.c`, `src/process_manager.c` |
| `kill()` & `SIGKILL` | Send `SIGTERM`, liveness check (`kill(pid, 0)`), and force-stop fallback | `src/process_manager.c` |
| `setitimer()` & `SIGALRM` | Periodic 5-second worker health check | `src/main.c`, `src/signal_manager.c`, `src/process_manager.c` |
| `nanosleep()` | Interruptible sleep in main loop, workers, and shutdown timeout | `src/main.c`, `src/process_manager.c`, `workers/worker.c` |

---

## Requirements

- Linux / POSIX-compliant OS
- `gcc` (C11 support)
- `make`
- `bash` (for running the automated test suite)

---

## Build & Run

### 1. Build the Project

```bash
make
```

This compiles:
- `./watchdog` (main supervisor executable)
- `./bin/worker` (worker child executable)

Compiler flags used: `-Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude`.

### 2. Run the Watchdog

```bash
./watchdog
```

> **Note:** Run `./watchdog` from the `watchdog-project/` root directory so it can locate `./bin/worker` and write to `./logs/`.

### 3. Clean Build Artifacts

```bash
make clean
```

---

## Manual Testing & Signal Usage

Start `./watchdog` in one terminal and note the printed Watchdog PID and Worker PIDs. From a second terminal, run the following commands:

### Request Status Report (`SIGUSR1`)

```bash
kill -SIGUSR1 <WATCHDOG_PID>
```

Writes the PID, status, and restart count of all workers to stdout and the current log file in `logs/`.

### Simulate Normal Worker Exit (No Restart)

```bash
kill -SIGUSR2 <WORKER_PID>
```

The worker exits with code `0`. Watchdog logs a normal exit, sets its state to `STOPPED`, and does **not** restart it.

### Simulate Unexpected Worker Crash (Triggers Restart)

```bash
kill -SIGKILL <WORKER_PID>
```

Watchdog detects the termination via `SIGCHLD` and restarts the worker automatically (up to 3 times). A 4th crash marks the worker as `FAILED` with no further restarts.

### Trigger Graceful Shutdown (`SIGTERM` or `Ctrl+C`)

```bash
kill -SIGTERM <WATCHDOG_PID>
# or press Ctrl+C in the watchdog terminal (SIGINT)
```

Watchdog stops the periodic timer, sends `SIGTERM` to active workers, waits up to 5 seconds (sending `SIGKILL` if any worker does not exit), reaps all children, and exits cleanly.

---

## Worker Test Modes & Environment Variables

| Variable | Value | Behavior |
| :--- | :--- | :--- |
| `WATCHDOG_TEST_MODE` | `1` | Directs log output to `logs/watchdog_test.log` instead of daily rotated logs |
| `WORKER_MODE` | *(unset)* | Default mode: runs continuously until `SIGTERM` or `SIGUSR2` |
| `WORKER_MODE` | `exit_ok` | Worker sleeps 2 seconds and exits normally (`exit 0`) |
| `WORKER_MODE` | `exit_err` | Worker sleeps 2 seconds and exits with error (`exit 1`) |
| `WORKER_MODE` | `ignore_sigterm` | Worker ignores `SIGTERM` to test the 5-second timeout → `SIGKILL` fallback |

Example:

```bash
WORKER_MODE=ignore_sigterm ./watchdog
```

---

## Automated Test Suite

Run all 7 test scenarios via `make` or directly through the test script:

```bash
make test
```

Or run a specific scenario (`1`–`7` or `all`):

```bash
./tests/test_scenarios.sh [1|2|3|4|5|6|7|all]
```

### Covered Scenarios

| # | Scenario | Verification |
| :-: | :--- | :--- |
| 1 | Normal Operation & `SIGUSR1` | 2 workers start and run; `SIGUSR1` logs PID and `RUNNING` state for both |
| 2 | Normal Termination | `SIGUSR2` causes exit code `0`; detected, not restarted, no zombies |
| 3 | Unexpected Termination & Restart Limit | Repeated `SIGKILL` triggers restarts `1/3`, `2/3`, `3/3`, then stops at limit |
| 4 | Graceful Shutdown (`SIGTERM`) | Watchdog stops and reaps all workers cleanly; no orphan processes |
| 5 | Graceful Shutdown (`SIGINT` / `Ctrl+C`) | Watchdog stops and reaps all workers cleanly on `SIGINT` |
| 6 | Error Exit & `SIGKILL` Fallback | `exit_err` triggers restart; `ignore_sigterm` escalates to `SIGKILL` after timeout |
| 7 | Periodic Check & Log Format | `SIGALRM` logs `Running workers: 2/2` after 5s; log entries include valid timestamps |

---

## Logging Format & Sample Output

Logs are written to `logs/watchdog_YYYY-MM-DD.log` (rotated automatically when exceeding 1 MB, or at midnight) using the format:

```text
[YYYY-MM-DD HH:MM:SS] [LEVEL] Message
```

### Sample Log Output

```text
[2026-11-23 15:00:21] [INFO] Watchdog started | PID: 8890
[2026-11-23 15:00:21] [INFO] Initializing workers...
[2026-11-23 15:00:21] [INFO] Child process created | PID: 8891
[2026-11-23 15:00:21] [INFO] Child process created | PID: 8892
[2026-11-23 15:00:21] [SUCCESS] All workers initialized successfully.
[2026-11-23 15:00:21] [INFO] Periodic child monitoring started.
[2026-11-23 15:00:26] [INFO] Periodic check completed | Running workers: 2/2
[2026-11-23 15:00:52] [INFO] Worker status requested.
[2026-11-23 15:00:52] [INFO] Worker #1 | PID: 8891 | STATUS: RUNNING | Restart Count: 0/3
[2026-11-23 15:00:52] [INFO] Worker #2 | PID: 8892 | STATUS: RUNNING | Restart Count: 0/3
[2026-11-23 15:01:26] [WARNING] Worker #1 terminated | PID: 8891
[2026-11-23 15:01:26] [INFO] Worker #1 exited normally with exit code 0 | PID: 8891
[2026-11-23 15:01:55] [WARNING] Worker #2 terminated | PID: 8892
[2026-11-23 15:01:55] [WARNING] Worker #2 terminated by signal: 9
[2026-11-23 15:01:55] [WARNING] Restarting Worker #2 | Attempt: 1/3
[2026-11-23 15:01:55] [INFO] Child process created | PID: 9179
[2026-11-23 15:01:55] [SUCCESS] Worker #2 restarted successfully | New PID: 9179
[2026-11-23 15:03:32] [ERROR] Worker #1 reached maximum restart limit (3).
[2026-11-23 15:03:51] [INFO] Periodic child monitoring stopped.
[2026-11-23 15:03:51] [INFO] Watchdog shutdown started.
[2026-11-23 15:03:51] [INFO] Stopping all workers.
[2026-11-23 15:03:51] [INFO] Sending SIGTERM to Worker #2 | PID: 9437
[2026-11-23 15:03:52] [SUCCESS] Worker #2 stopped gracefully | PID: 9437
[2026-11-23 15:03:52] [SUCCESS] All workers have been stopped.
[2026-11-23 15:03:52] [SUCCESS] Watchdog shutdown completed.
```

---

## Team & Task Allocation

| Student | GitHub | Assigned Module | Primary Files |
| :--- | :--- | :--- | :--- |
| Student 1 Name | `Yasin Şebelek` | Signal Management & Main Program (`sigaction`, `SIGINT`/`SIGTERM`/`SIGUSR1`, main loop, graceful shutdown) | `include/signal_manager.h`, `src/signal_manager.c`, `src/main.c` |
| Student 2 Name | `Yasin Şebelek` | Child Process Management (`fork`/`execl`, `SIGCHLD`/`waitpid`, bounded restarts, `SIGALRM`/`setitimer`) | `include/process_manager.h`, `src/process_manager.c`, `workers/worker.c` |
| Student 3 Name | `Yasin Şebelek` | Logging, Build & Test Infrastructure (logger, `SIGUSR1` reporting, test suite, `Makefile`, docs) | `include/logger.h`, `src/logger.c`, `tests/test_scenarios.sh`, `Makefile`, `README.md` |
