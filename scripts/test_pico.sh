#!/usr/bin/env bash
#
# scripts/test_pico.sh — Automated Headless QEMU CI Gate for Raw Pico Series
#
# Verifies BOTH Cortex-M33 (RP2350 default, mps2-an521) AND
# Cortex-M0+ Thumb-1 (RP2040, mps2-an385):
#   - Clean zero-warning build (-Os -Wall -Wextra -Werror)
#   - Boot banner & zero-heap static SRAM accounting
#   - 1 kHz SysTick monotonic advance
#   - PendSV preemptive multi-tasking + Semaphore IPC
#   - EventFlag rendezvous
#   - Battery-saving WFI idle sleep
#   - Interactive `btron-pico#` shell commands (`ver`, `mem`, `ps`)
#
set -euo pipefail

QEMU_BIN="${QEMU_ARM:-qemu-system-arm}"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "[ERROR] $QEMU_BIN not found in PATH."
    exit 1
fi

run_profile() {
    local BOARD="$1"
    local MACH="$2"
    local ELF=".build/pico-qemu-${BOARD}/btron-pico-${BOARD}-qemu.elf"

    echo "=========================================================="
    echo " [TEST-PICO] Building PICO_BOARD=${BOARD} (${MACH})..."
    echo "=========================================================="
    make PICO_BOARD="$BOARD" pico

    local LOG_FILE
    LOG_FILE=$(mktemp /tmp/qemu_pico_${BOARD}_XXXXXX)
    trap 'rm -f "$LOG_FILE"' RETURN

    # Feed shell commands after boot selftest finishes, then exit QEMU cleanly
    (
        sleep 1
        printf "ver\nmem\nps\n"
        sleep 1
    ) | "$QEMU_BIN" -M "$MACH" -nographic -serial stdio -monitor none -kernel "$ELF" >"$LOG_FILE" 2>&1 &
    local QEMU_PID=$!

    sleep 3
    kill -TERM "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true

    cat "$LOG_FILE"

    local MARKERS=(
        "B-System/BTRON3 3.20 (pico-cleanroom) Cortex-M RTOS"
        "[MEM ] Static SRAM:"
        "[TEST] SysTick 1kHz monotonic : PASS"
        "[TEST] PendSV + Semaphore IPC : PASS"
        "[TEST] EventFlag rendezvous   : PASS"
        "[TEST] WFI battery idle sleep : PASS"
        "[BOOT] Stage 1: Pico RTOS Console Active"
        "btron-pico#"
        "SRAM static used :"
        "TID  PRIO  STATE  SWITCHES  NAME"
    )

    local PASS=0
    local FAIL=0
    for m in "${MARKERS[@]}"; do
        if grep -qF "$m" "$LOG_FILE"; then
            echo "  [PASS] ${BOARD}: '$m'"
            PASS=$((PASS + 1))
        else
            echo "  [FAIL] ${BOARD}: Missing '$m'"
            FAIL=$((FAIL + 1))
        fi
    done

    echo "----------------------------------------------------------"
    echo " [TEST-PICO] ${BOARD} (${MACH}): ${PASS} Passed, ${FAIL} Failed"
    echo "----------------------------------------------------------"
    if [ "$FAIL" -gt 0 ]; then
        exit 1
    fi
}

run_profile "rp2350" "mps2-an521"
run_profile "rp2040" "mps2-an385"
echo "[TEST-PICO] ALL PROFILES PASSED (RP2350 + RP2040)."
