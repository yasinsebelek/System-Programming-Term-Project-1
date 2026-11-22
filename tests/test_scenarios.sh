#!/usr/bin/env bash
# Automated Test Suite for POSIX Watchdog Process Manager
# Verifies all test scenarios defined in Section 7 of the project specification.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

LOG_FILE="logs/watchdog_test.log"
OUT_FILE="/tmp/watchdog_test_stdout.log"
PASS_COUNT=0
FAIL_COUNT=0
CURRENT_WD_PID=""
LOG_START_LINE=1

rm -f "$LOG_FILE"

cleanup() {
    if [ -n "$CURRENT_WD_PID" ] && kill -0 "$CURRENT_WD_PID" 2>/dev/null; then
        kill -TERM "$CURRENT_WD_PID" 2>/dev/null || true
        wait "$CURRENT_WD_PID" 2>/dev/null || true
    fi
    rm -f "$OUT_FILE"
}
trap cleanup EXIT

current_log() {
    if [ -f "$LOG_FILE" ]; then
        tail -n +"$LOG_START_LINE" "$LOG_FILE"
    fi
}

pass() {
    echo "  [PASS] $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "  [FAIL] $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

assert_log_contains() {
    local pattern="$1"
    local description="$2"
    if current_log | grep -qE "$pattern"; then
        pass "$description"
    else
        fail "$description (pattern not found: $pattern)"
    fi
}

assert_log_not_contains() {
    local pattern="$1"
    local description="$2"
    if current_log | grep -qE "$pattern"; then
        fail "$description (unexpected pattern found: $pattern)"
    else
        pass "$description"
    fi
}

wait_for_log() {
    local pattern="$1"
    local timeout="${2:-10}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout" ]; do
        if current_log | grep -qE "$pattern"; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

verify_worker_pid() {
    local pid="$1"
    if [ -z "$pid" ] || ! [[ "$pid" =~ ^[0-9]+$ ]]; then
        return 1
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi
    return 0
}

start_watchdog() {
    local mode="${1:-}"
    if [ -f "$LOG_FILE" ]; then
        LOG_START_LINE=$(( $(wc -l < "$LOG_FILE") + 1 ))
    else
        LOG_START_LINE=1
    fi
    rm -f "$OUT_FILE"
    if [ -n "$mode" ]; then
        WATCHDOG_TEST_MODE=1 WORKER_MODE="$mode" ./watchdog > "$OUT_FILE" 2>&1 &
    else
        WATCHDOG_TEST_MODE=1 ./watchdog > "$OUT_FILE" 2>&1 &
    fi
    CURRENT_WD_PID=$!
    wait_for_log "Periodic child monitoring started\." 10
}

stop_watchdog_with_signal() {
    local sig="$1"
    local wd_pid="$CURRENT_WD_PID"
    kill -"$sig" "$wd_pid"
    wait "$wd_pid"
    local rc=$?
    CURRENT_WD_PID=""
    return "$rc"
}

echo "=================================================="
echo "     WATCHDOG AUTOMATED TEST SCENARIOS SUITE      "
echo "=================================================="
echo ""

# [BUILD]
echo "[BUILD] Compiling project with make..."
if make clean >/dev/null && make >/dev/null && [ -x ./watchdog ] && [ -x ./bin/worker ]; then
    pass "Project compiled cleanly with make and produced ./watchdog and ./bin/worker"
else
    fail "Project compilation failed"
    exit 1
fi
echo ""

# [SCENARIO 1] Normal Operation
echo "[SCENARIO 1] Normal operation"
if start_watchdog; then
    pass "Watchdog started and initialized workers"
else
    fail "Watchdog failed to start within timeout"
fi

W1_PID=$(current_log | grep "Child process created | PID:" | sed -n '1p' | awk '{print $NF}')
W2_PID=$(current_log | grep "Child process created | PID:" | sed -n '2p' | awk '{print $NF}')

if verify_worker_pid "$W1_PID" && verify_worker_pid "$W2_PID"; then
    pass "At least two worker processes are running (W1=$W1_PID, W2=$W2_PID)"
else
    fail "Expected two running worker processes (W1=$W1_PID, W2=$W2_PID)"
fi
echo ""

# [SCENARIO 2] Status Reporting (SIGUSR1)
echo "[SCENARIO 2] Status reporting via SIGUSR1"
kill -USR1 "$CURRENT_WD_PID"
wait_for_log "Worker #2 \| PID: $W2_PID \| STATUS: RUNNING" 5
assert_log_contains "Worker status requested\." "SIGUSR1 triggered status reporting"
assert_log_contains "Worker #1 \| PID: $W1_PID \| STATUS: RUNNING \| Restart Count: 0/3" "Worker #1 running status logged"
assert_log_contains "Worker #2 \| PID: $W2_PID \| STATUS: RUNNING \| Restart Count: 0/3" "Worker #2 running status logged"
echo ""

# [SCENARIO 3] Normal Termination (exit code 0 -> no restart)
echo "[SCENARIO 3] Normal termination of Worker #1 (exit code 0)"
if verify_worker_pid "$W1_PID"; then
    kill -USR2 "$W1_PID"
    wait_for_log "Worker #1 exited normally with exit code 0 \| PID: $W1_PID" 6
    assert_log_contains "Worker #1 exited normally with exit code 0 \| PID: $W1_PID" "Normal termination detected and logged"
    assert_log_not_contains "Restarting Worker #1" "Normally exited Worker #1 was NOT restarted"

    kill -USR1 "$CURRENT_WD_PID"
    wait_for_log "Worker #1 \| PID: -1 \| STATUS: STOPPED \| Restart Count: 0/3" 5
    assert_log_contains "Worker #1 \| PID: -1 \| STATUS: STOPPED \| Restart Count: 0/3" "SIGUSR1 reports Worker #1 as STOPPED"
else
    fail "Worker #1 PID ($W1_PID) could not be verified before test"
fi
echo ""

# [SCENARIO 4 & 5] Unexpected Termination (SIGKILL) & Restart Limit (Max 3)
echo "[SCENARIO 4 & 5] Unexpected termination (SIGKILL) and restart limit (3)"
CURRENT_W2_PID="$W2_PID"

for attempt in 1 2 3; do
    if verify_worker_pid "$CURRENT_W2_PID"; then
        kill -KILL "$CURRENT_W2_PID"
        wait_for_log "Restarting Worker #2 \| Attempt: ${attempt}/3" 6
        assert_log_contains "Restarting Worker #2 \| Attempt: ${attempt}/3" "Unexpected termination triggered restart ${attempt}/3"

        elapsed=0
        while [ "$elapsed" -lt 6 ]; do
            count=$(current_log | grep -c "Worker #2 restarted successfully | New PID:" 2>/dev/null || echo 0)
            if [ "$count" -ge "$attempt" ]; then
                break
            fi
            sleep 1
            elapsed=$((elapsed + 1))
        done

        NEW_W2_PID=$(current_log | grep "Worker #2 restarted successfully | New PID:" | tail -n 1 | awk '{print $NF}')
        if [ "$NEW_W2_PID" != "$CURRENT_W2_PID" ] && verify_worker_pid "$NEW_W2_PID"; then
            pass "Worker #2 restarted with verified new PID ($NEW_W2_PID)"
            CURRENT_W2_PID="$NEW_W2_PID"
        else
            fail "Worker #2 new PID ($NEW_W2_PID) is not valid/running after attempt $attempt"
        fi
    else
        fail "Worker #2 PID ($CURRENT_W2_PID) not running before attempt $attempt"
    fi
done

# 4th termination should exceed MAX_RESTARTS (3)
if verify_worker_pid "$CURRENT_W2_PID"; then
    kill -KILL "$CURRENT_W2_PID"
    wait_for_log "Worker #2 reached maximum restart limit \(3\)\." 6
    assert_log_contains "Worker #2 reached maximum restart limit \(3\)\." "Restart limit enforced after 3 restarts"
    assert_log_not_contains "Restarting Worker #2 \| Attempt: 4/3" "No 4th restart attempted"

    kill -USR1 "$CURRENT_WD_PID"
    wait_for_log "Worker #2 \| PID: -1 \| STATUS: FAILED \| Restart Count: 3/3" 5
    assert_log_contains "Worker #2 \| PID: -1 \| STATUS: FAILED \| Restart Count: 3/3" "SIGUSR1 reports Worker #2 as FAILED (3/3)"
else
    fail "Worker #2 PID ($CURRENT_W2_PID) not running before limit test"
fi

stop_watchdog_with_signal TERM
echo ""

# [SCENARIO 6] Graceful Shutdown via SIGTERM
echo "[SCENARIO 6] Graceful shutdown via SIGTERM"
start_watchdog
W1_PID=$(current_log | grep "Child process created | PID:" | sed -n '1p' | awk '{print $NF}')
W2_PID=$(current_log | grep "Child process created | PID:" | sed -n '2p' | awk '{print $NF}')

if stop_watchdog_with_signal TERM; then
    pass "Watchdog exited cleanly with code 0 on SIGTERM"
else
    fail "Watchdog returned non-zero exit code on SIGTERM"
fi

assert_log_contains "Watchdog shutdown started\." "Shutdown start logged"
assert_log_contains "Sending SIGTERM to Worker #1 \| PID: $W1_PID" "SIGTERM sent to Worker #1"
assert_log_contains "Sending SIGTERM to Worker #2 \| PID: $W2_PID" "SIGTERM sent to Worker #2"
assert_log_contains "Worker #1 stopped gracefully \| PID: $W1_PID" "Worker #1 stopped gracefully"
assert_log_contains "Worker #2 stopped gracefully \| PID: $W2_PID" "Worker #2 stopped gracefully"
assert_log_contains "Watchdog shutdown completed\." "Shutdown completion logged"

if ! kill -0 "$W1_PID" 2>/dev/null && ! kill -0 "$W2_PID" 2>/dev/null; then
    pass "No worker processes remain running after SIGTERM"
else
    fail "Orphan worker process still running after SIGTERM"
fi
echo ""

# [SCENARIO 7] Graceful Shutdown via SIGINT (Ctrl+C)
echo "[SCENARIO 7] Graceful shutdown via SIGINT (Ctrl+C)"
start_watchdog
W1_PID=$(current_log | grep "Child process created | PID:" | sed -n '1p' | awk '{print $NF}')
W2_PID=$(current_log | grep "Child process created | PID:" | sed -n '2p' | awk '{print $NF}')

if stop_watchdog_with_signal INT; then
    pass "Watchdog exited cleanly with code 0 on SIGINT"
else
    fail "Watchdog returned non-zero exit code on SIGINT"
fi

assert_log_contains "Worker #1 stopped gracefully \| PID: $W1_PID" "Worker #1 stopped gracefully on SIGINT"
assert_log_contains "Worker #2 stopped gracefully \| PID: $W2_PID" "Worker #2 stopped gracefully on SIGINT"
assert_log_contains "Watchdog shutdown completed\." "SIGINT shutdown completion logged"

if ! kill -0 "$W1_PID" 2>/dev/null && ! kill -0 "$W2_PID" 2>/dev/null; then
    pass "No worker processes remain running after SIGINT"
else
    fail "Orphan worker process still running after SIGINT"
fi
echo ""

# [SCENARIO 8a] Unexpected Non-Zero Exit Code (exit 1)
echo "[SCENARIO 8a] Unexpected non-zero exit code detection (exit 1)"
start_watchdog "exit_err"
wait_for_log "exited with error \| Exit code: 1" 8
assert_log_contains "exited with error \| Exit code: 1" "Non-zero exit code (1) detected and logged"
assert_log_contains "Restarting Worker #[12] \| Attempt: 1/3" "Restart triggered for non-zero exit code"
stop_watchdog_with_signal TERM
echo ""

# [SCENARIO 8b] SIGTERM Timeout Fallback to SIGKILL
echo "[SCENARIO 8b] SIGTERM timeout fallback to SIGKILL"
start_watchdog "ignore_sigterm"
W1_PID=$(current_log | grep "Child process created | PID:" | sed -n '1p' | awk '{print $NF}')
W2_PID=$(current_log | grep "Child process created | PID:" | sed -n '2p' | awk '{print $NF}')

if stop_watchdog_with_signal TERM; then
    pass "Watchdog exited cleanly after SIGKILL timeout escalation"
else
    fail "Watchdog returned non-zero exit code during SIGKILL escalation"
fi

assert_log_contains "did not stop after SIGTERM\. Sending SIGKILL" "SIGKILL escalation triggered after timeout"
assert_log_contains "Worker #1 terminated and reaped \| PID: $W1_PID" "Unresponsive Worker #1 terminated and reaped"
assert_log_contains "Worker #2 terminated and reaped \| PID: $W2_PID" "Unresponsive Worker #2 terminated and reaped"
assert_log_contains "All workers have been stopped\." "All unresponsive workers confirmed stopped"
echo ""

# [SCENARIO 9] Log Timestamp & Format Verification
echo "[SCENARIO 9] Log timestamp and format verification"
assert_log_contains "^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] \[(INFO|WARNING|ERROR|SUCCESS)\]" "Log entries include ISO timestamps and severity levels"
echo ""

echo "=================================================="
echo "  TEST SUMMARY: PASSED = $PASS_COUNT | FAILED = $FAIL_COUNT"
echo "  Test log saved to: $LOG_FILE"
echo "=================================================="

if [ "$FAIL_COUNT" -ne 0 ]; then
    exit 1
fi
exit 0