#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.." || exit 1
LOG="logs/watchdog_test.log"
WD_PID=""

cleanup() {
    [ -n "$WD_PID" ] && kill -TERM "$WD_PID" 2>/dev/null && wait "$WD_PID" 2>/dev/null
}
trap cleanup EXIT

check() {
    if grep -q "$1" "$LOG"; then
        echo "  [PASS] $2"
    else
        echo "  [FAIL] $2"
    fi
}

start_wd() {
    WATCHDOG_TEST_MODE=1 WORKER_MODE="${1:-}" ./watchdog >/dev/null 2>&1 &
    WD_PID=$!
    sleep 1
    W1=$(grep "Child process created | PID:" "$LOG" | tail -n 2 | sed -n '1p' | awk '{print $NF}')
    W2=$(grep "Child process created | PID:" "$LOG" | tail -n 2 | sed -n '2p' | awk '{print $NF}')
}

stop_wd() {
    kill -TERM "$WD_PID" 2>/dev/null
    wait "$WD_PID" 2>/dev/null
    WD_PID=""
}

# 1) Normal çalışma ve SIGUSR1 durum raporu
test_1() {
    echo "[TEST 1] Normal çalışma ve SIGUSR1 durum raporu"
    start_wd
    kill -0 "$W1" 2>/dev/null && kill -0 "$W2" 2>/dev/null \
        && echo "  [PASS] 2 worker çalışıyor (W1=$W1, W2=$W2)" \
        || echo "  [FAIL] Workerlar başlatılamadı"

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
    kill -0 "$W1" 2>/dev/null && kill -USR2 "$W1"
    sleep 1
    check "Worker #1 exited normally with exit code 0 | PID: $W1" "Normal çıkış tespit edildi"
    grep -q "Restarting Worker #1" "$LOG" \
        && echo "  [FAIL] Normal çıkan worker yeniden başlatıldı" \
        || echo "  [PASS] Normal çıkan worker yeniden başlatılmadı"
    stop_wd
}

# 3) Beklenmedik sonlandırma (SIGKILL) ve 3 restart limiti
test_3() {
    echo "[TEST 3] Beklenmedik kapanış (SIGKILL) ve restart limiti (3)"
    start_wd
    TARGET="$W2"
    for i in 1 2 3; do
        kill -0 "$TARGET" 2>/dev/null && kill -KILL "$TARGET"
        sleep 1
        check "Restarting Worker #2 | Attempt: $i/3" "Restart $i/3 tetiklendi"
        TARGET=$(grep "Child process created | PID:" "$LOG" | tail -n 1 | awk '{print $NF}')
    done

    # 4. kez öldürünce limit aşılmalı
    kill -0 "$TARGET" 2>/dev/null && kill -KILL "$TARGET"
    sleep 1
    check "Worker #2 reached maximum restart limit (3)." "Maksimum restart limiti uygulandı"
    stop_wd
}

# 4) Graceful shutdown (SIGTERM)
test_4() {
    echo "[TEST 4] Graceful shutdown (SIGTERM)"
    start_wd
    stop_wd
    check "Watchdog shutdown started." "Kapanış başlatıldı"
    check "Worker #1 stopped gracefully | PID: $W1" "Worker #1 temiz kapandı"
    check "Worker #2 stopped gracefully | PID: $W2" "Worker #2 temiz kapandı"
    check "Watchdog shutdown completed." "Kapanış tamamlandı"
    ! kill -0 "$W1" 2>/dev/null && ! kill -0 "$W2" 2>/dev/null \
        && echo "  [PASS] Yetim worker kalmadı" \
        || echo "  [FAIL] Çalışmaya devam eden worker var"
}

# 5) Hata kodu (exit 1) ve SIGTERM timeout -> SIGKILL
test_5() {
    echo "[TEST 5] Hata kodu (exit 1) ve SIGKILL fallback"
    start_wd "exit_err"
    sleep 3
    check "exited with error | Exit code: 1" "Exit code 1 yakalandı"
    stop_wd

    start_wd "ignore_sigterm"
    stop_wd
    check "did not stop after SIGTERM. Sending SIGKILL" "Timeout sonrası SIGKILL gönderildi"
}

# 6) Log format kontrolü
test_6() {
    echo "[TEST 6] Log zaman damgası kontrolü"
    grep -qE "^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\]" "$LOG" \
        && echo "  [PASS] Log kayıtları zaman damgalı" \
        || echo "  [FAIL] Zaman damgası bulunamadı"
}

# Derle ve çalıştır
rm -f "$LOG"
make clean >/dev/null && make >/dev/null || { echo "[FAIL] Derleme hatası"; exit 1; }

case "${1:-all}" in
    1) test_1 ;;
    2) test_2 ;;
    3) test_3 ;;
    4) test_4 ;;
    5) test_5 ;;
    6) test_6 ;;
    all) test_1; test_2; test_3; test_4; test_5; test_6 ;;
    *) echo "Kullanım: $0 [1|2|3|4|5|6|all]"; exit 1 ;;
esac