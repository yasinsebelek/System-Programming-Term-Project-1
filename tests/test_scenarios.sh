#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.." || exit 1
LOG="logs/watchdog_test.log"
WD_PID=""
FAIL=0

cleanup() {
    if [ -n "$WD_PID" ]; then
        kill -TERM "$WD_PID" 2>/dev/null
        wait "$WD_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

check() {
    if grep -q -- "$1" "$LOG"; then
        echo "  [PASS] $2"
    else
        echo "  [FAIL] $2"
        FAIL=$((FAIL + 1))
    fi
}

verify_worker_pid() {
    local pid="$1"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || return 1
    grep -q "PID: $pid$" "$LOG" 2>/dev/null || return 1
    local comm
    if comm=$(ps -p "$pid" -o comm= 2>/dev/null) && [ -n "$comm" ]; then
        case "$comm" in
            *worker*) return 0 ;;
            *) return 1 ;;
        esac
    fi
    return 0
}

start_wd() {
    rm -f "$LOG"
    WATCHDOG_TEST_MODE=1 WORKER_MODE="${1:-}" ./watchdog >/dev/null 2>&1 &
    WD_PID=$!
    sleep 1
    W1=$(grep "Child process created | PID:" "$LOG" 2>/dev/null | sed -n '1p' | awk '{print $NF}')
    W2=$(grep "Child process created | PID:" "$LOG" 2>/dev/null | sed -n '2p' | awk '{print $NF}')
}

stop_wd() {
    if [ -n "$WD_PID" ]; then
        kill -"${1:-TERM}" "$WD_PID" 2>/dev/null
        wait "$WD_PID" 2>/dev/null
        WD_PID=""
    fi
}

# 1) Normal çalışma ve SIGUSR1 durum raporu
test_1() {
    echo "[TEST 1] Normal çalışma ve SIGUSR1 durum raporu"
    start_wd
    if verify_worker_pid "$W1" && verify_worker_pid "$W2"; then
        echo "  [PASS] 2 worker çalışıyor (W1=$W1, W2=$W2)"
    else
        echo "  [FAIL] Workerlar başlatılamadı"
        FAIL=$((FAIL + 1))
    fi

    kill -USR1 "$WD_PID"
    sleep 1
    check "Worker status requested." "SIGUSR1 alındı"
    check "Worker #1 | PID: $W1 | STATUS: RUNNING" "Worker #1 durumu loglandı"
    check "Worker #2 | PID: $W2 | STATUS: RUNNING" "Worker #2 durumu loglandı"
    stop_wd
}

# 2) Normal çıkış (exit 0 -> restart edilmemeli)
test_2() {
    echo "[TEST 2] Normal çıkış (exit 0)"
    start_wd
    if verify_worker_pid "$W1"; then
        kill -USR2 "$W1"
    fi
    sleep 1
    check "Worker #1 exited normally with exit code 0 | PID: $W1" "Normal çıkış (SIGUSR2) tespit edildi"
    grep -q "Restarting Worker #1" "$LOG" \
        && { echo "  [FAIL] Normal çıkan worker yeniden başlatıldı"; FAIL=$((FAIL + 1)); } \
        || echo "  [PASS] Normal çıkan worker yeniden başlatılmadı"
    ! kill -0 "$W1" 2>/dev/null \
        && echo "  [PASS] Zombie süreç kalmadı" \
        || { echo "  [FAIL] Worker reap edilmedi"; FAIL=$((FAIL + 1)); }

    kill -USR1 "$WD_PID"
    sleep 1
    check "Worker #1 | PID: N/A | STATUS: STOPPED | Restart Count: 0/3" "Worker #1 STOPPED durumu loglandı"
    stop_wd

    start_wd "exit_ok"
    sleep 3
    check "Worker #1 exited normally with exit code 0 | PID: $W1" "Normal çıkış (exit_ok modu) tespit edildi"
    grep -q "Restarting Worker" "$LOG" \
        && { echo "  [FAIL] exit_ok sonrası beklenmeyen restart yapıldı"; FAIL=$((FAIL + 1)); } \
        || echo "  [PASS] exit_ok sonrası restart yapılmadı"
    stop_wd
}

# 3) Beklenmedik sonlandırma (SIGKILL) ve 3 restart limiti
test_3() {
    echo "[TEST 3] Beklenmedik kapanış (SIGKILL) ve restart limiti (3)"
    start_wd
    TARGET="$W2"
    for i in 1 2 3; do
        if verify_worker_pid "$TARGET"; then
            kill -KILL "$TARGET"
        fi
        sleep 1
        check "Restarting Worker #2 | Attempt: $i/3" "Restart $i/3 tetiklendi"
        TARGET=$(grep "Worker #2 restarted successfully | New PID:" "$LOG" | tail -n 1 | awk '{print $NF}')
    done

    if verify_worker_pid "$TARGET"; then
        kill -KILL "$TARGET"
    fi
    sleep 1
    check "Worker #2 reached maximum restart limit (3)." "Maksimum restart limiti uygulandı"

    RESTART_COUNT=$(grep -c "Restarting Worker #2" "$LOG" || true)
    if [ "$RESTART_COUNT" -eq 3 ]; then
        echo "  [PASS] 4. restart yapılmadı (toplam restart: $RESTART_COUNT)"
    else
        echo "  [FAIL] Beklenen restart sayısı 3, bulunan: $RESTART_COUNT"
        FAIL=$((FAIL + 1))
    fi

    kill -USR1 "$WD_PID"
    sleep 1
    check "Worker #2 | PID: N/A | STATUS: FAILED | Restart Count: 3/3" "Worker #2 FAILED durumu loglandı"
    stop_wd
}

# 4) Graceful shutdown (SIGTERM)
test_4() {
    echo "[TEST 4] Graceful shutdown (SIGTERM)"
    start_wd
    stop_wd TERM
    check "Watchdog shutdown started." "Kapanış başlatıldı"
    check "Worker #1 stopped gracefully | PID: $W1" "Worker #1 temiz kapandı"
    check "Worker #2 stopped gracefully | PID: $W2" "Worker #2 temiz kapandı"
    check "Watchdog shutdown completed." "Kapanış tamamlandı"
    ! kill -0 "$W1" 2>/dev/null && ! kill -0 "$W2" 2>/dev/null \
        && echo "  [PASS] Yetim worker kalmadı" \
        || { echo "  [FAIL] Çalışmaya devam eden worker var"; FAIL=$((FAIL + 1)); }
}

# 5) Graceful shutdown (SIGINT / Ctrl+C)
test_5() {
    echo "[TEST 5] Graceful shutdown (SIGINT / Ctrl+C)"
    start_wd
    stop_wd INT
    check "Watchdog shutdown started." "SIGINT kapanışı başlatıldı"
    check "Worker #1 stopped gracefully | PID: $W1" "Worker #1 temiz kapandı"
    check "Worker #2 stopped gracefully | PID: $W2" "Worker #2 temiz kapandı"
    check "Watchdog shutdown completed." "SIGINT kapanışı tamamlandı"
    ! kill -0 "$W1" 2>/dev/null && ! kill -0 "$W2" 2>/dev/null \
        && echo "  [PASS] Yetim worker kalmadı" \
        || { echo "  [FAIL] Çalışmaya devam eden worker var"; FAIL=$((FAIL + 1)); }
}

# 6) Hata kodu (exit 1) ve SIGTERM timeout -> SIGKILL
test_6() {
    echo "[TEST 6] Hata kodu (exit 1) ve SIGKILL fallback"
    start_wd "exit_err"
    sleep 3
    check "exited with error | Exit code: 1" "Exit code 1 yakalandı"
    check "Restarting Worker #1 | Attempt: 1/3" "Hata sonrası restart tetiklendi"
    stop_wd

    start_wd "ignore_sigterm"
    stop_wd TERM
    check "did not stop after SIGTERM. Sending SIGKILL" "Timeout sonrası SIGKILL gönderildi"
    check "terminated and reaped" "SIGKILL sonrası worker reap edildi"
    ! kill -0 "$W1" 2>/dev/null && ! kill -0 "$W2" 2>/dev/null \
        && echo "  [PASS] SIGKILL sonrası yetim worker kalmadı" \
        || { echo "  [FAIL] SIGKILL sonrası çalışan worker kaldı"; FAIL=$((FAIL + 1)); }
}

# 7) Periyodik kontrol (SIGALRM / setitimer) ve log formatı
test_7() {
    echo "[TEST 7] Periyodik kontrol (SIGALRM) ve log zaman damgası"
    start_wd
    sleep 6
    check "Periodic check completed | Running workers: 2/2" "Periyodik kontrol çalıştı"
    grep -qE "^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\]" "$LOG" \
        && echo "  [PASS] Log kayıtları zaman damgalı" \
        || { echo "  [FAIL] Zaman damgası bulunamadı"; FAIL=$((FAIL + 1)); }
    stop_wd
}

make clean >/dev/null && make >/dev/null || { echo "[FAIL] Derleme hatası"; exit 1; }

case "${1:-all}" in
    1) test_1 ;;
    2) test_2 ;;
    3) test_3 ;;
    4) test_4 ;;
    5) test_5 ;;
    6) test_6 ;;
    7) test_7 ;;
    all) test_1; test_2; test_3; test_4; test_5; test_6; test_7 ;;
    *) echo "Kullanım: $0 [1|2|3|4|5|6|7|all]"; exit 1 ;;
esac

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
