#!/usr/bin/env bash
#
# scripts/test_picocalc_build.sh — PicoCalc image gate (phases 3 and 5)
#
# Builds both images for both boards:
#   btron-picocalc-{rp2040,rp2350}.uf2 + -sdboot.bin
#   btron-picocalc-quake-{rp2040,rp2350}.uf2 + -sdboot.bin
#
# Checks:
#   - Zero compiler warnings (-Os -Wall -Wextra -Werror), via the recipes
#   - Valid UF2 magic, block counts, family IDs (0xE48BFF59 / 0xE48BFF56)
#   - RP2350 PICOBIN ARM-S IMAGE_DEF markers in the first 4 KiB
#   - RP2040 boot2 MPEG-2 CRC32 exact match
#   - SD-boot .bin initial MSP and Thumb Reset_Handler (>= 0x10032000)
#   - Linker SRAM assert: __sram_used__ <= 215040
#   - RP2040 payload fits in 2 MiB, SD-boot fits in the +200 KiB window
#   - Neither image contains ztriangle, the string e1m1, or progs.dat
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

find_tool() {
    local name="$1"
    local p
    for p in \
        "/opt/homebrew/opt/llvm/bin/$name" \
        "/usr/local/opt/llvm/bin/$name" \
        "$name"
    do
        if [ -x "$p" ] || command -v "$p" >/dev/null 2>&1; then
            command -v "$p"
            return 0
        fi
    done
    echo "[FAIL] $name not found" >&2
    exit 1
}

LLVM_NM="$(find_tool llvm-nm)"
LLVM_STRINGS="$(find_tool llvm-strings)"
SRAM_LIMIT=215040
# RP2040 flash is 2 MiB. SD-boot is linked at 0x10032000, so the window is 0x1CE000.
RP2040_BIN_LIMIT=2097152
RP2040_SD_LIMIT=1892352

echo "=========================================================="
echo " [TEST-PICOCALC] Building gears and quake images..."
echo "=========================================================="
make picocalc-all
make PICO_BOARD=rp2040 picocalc-quake
make PICO_BOARD=rp2350 picocalc-quake

echo "=========================================================="
echo " [TEST-PICOCALC] Verifying UF2 and boot headers..."
echo "=========================================================="
for kind in "" "-quake"; do
    python3 scripts/tool_mk_uf2.py --board rp2350 --verify "btron-picocalc${kind}-rp2350.uf2"
    python3 scripts/tool_mk_uf2.py --board rp2040 --verify "btron-picocalc${kind}-rp2040.uf2"
done

echo "=========================================================="
echo " [TEST-PICOCALC] Verifying SD-boot (+200 KiB) vectors..."
echo "=========================================================="
python3 - <<'PY'
import struct, sys

for kind in ("", "-quake"):
    for board, ram_top in (("rp2350", 0x20080000), ("rp2040", 0x20040000)):
        path = f"btron-picocalc{kind}-{board}-sdboot.bin"
        with open(path, "rb") as f:
            hdr = f.read(16)
        msp, reset = struct.unpack("<II", hdr[:8])
        if msp != ram_top:
            sys.exit(f"[FAIL] {path}: initial MSP 0x{msp:08X} != 0x{ram_top:08X}")
        if not (0x10032000 <= reset < 0x10400000) or (reset & 1) == 0:
            sys.exit(f"[FAIL] {path}: bad Thumb Reset_Handler 0x{reset:08X}")
        print(f"  [PASS] {path}: MSP=0x{msp:08X}, Reset_Handler=0x{reset:08X} (Thumb OK)")
PY

# The string gate must reject a file that contains the banned text.
# llvm-strings prints runs of printable bytes, so a text file is enough.
neg="$(mktemp)"
printf 'keep ztriangle e1m1 progs.dat out\n' > "$neg"
blob="$("$LLVM_STRINGS" "$neg")"
rm -f "$neg"
hit=0
for bad in ztriangle e1m1 progs.dat; do
    if printf '%s\n' "$blob" | grep -F -q -- "$bad"; then
        hit=1
    fi
done
if [ "$hit" -ne 1 ]; then
    echo "[FAIL] banned-string gate did not see ztriangle / e1m1 / progs.dat"
    exit 1
fi
echo "  [PASS] banned-string gate rejects ztriangle, e1m1, and progs.dat"

echo "=========================================================="
echo " [TEST-PICOCALC] SRAM, flash window, and banned contents..."
echo "=========================================================="

need_sym() {
    local elf="$1" sym="$2"
    if ! "$LLVM_NM" "$elf" | grep -E -q "(^| )${sym}\$"; then
        echo "[FAIL] $elf missing $sym"
        exit 1
    fi
}

forbid_sym() {
    local elf="$1" sym="$2"
    if "$LLVM_NM" "$elf" | grep -E -q "(^| )${sym}\$"; then
        echo "[FAIL] $elf defines $sym"
        exit 1
    fi
}

check_elf() {
    local elf="$1" kind="$2" board="$3"
    local used dec bin sd
    used="$("$LLVM_NM" -g "$elf" | awk '$NF=="__sram_used__"{print $1; exit}')"
    if [ -z "$used" ]; then
        echo "[FAIL] $elf has no __sram_used__"
        exit 1
    fi
    dec=$((16#$used))
    if [ "$dec" -gt "$SRAM_LIMIT" ]; then
        echo "[FAIL] $elf __sram_used__ $dec > $SRAM_LIMIT"
        exit 1
    fi
    echo "  [PASS] $elf __sram_used__ $dec <= $SRAM_LIMIT"

    blob="$("$LLVM_STRINGS" "$elf")"
    for bad in ztriangle e1m1 progs.dat; do
        if printf '%s\n' "$blob" | grep -F -q -- "$bad"; then
            echo "[FAIL] $elf contains '$bad'"
            exit 1
        fi
    done
    forbid_sym "$elf" ztriangle

    if [ "$kind" = "quake" ]; then
        need_sym "$elf" build_quake
        need_sym "$elf" start_sim_render
        need_sym "$elf" build_terminal
        need_sym "$elf" build_cabinet
        forbid_sym "$elf" build_glgears
        forbid_sym "$elf" build_xmb
        bin=".build/picocalc-quake-${board}/btron-picocalc-quake-${board}.bin"
        sd="btron-picocalc-quake-${board}-sdboot.bin"
    else
        need_sym "$elf" build_glgears
        need_sym "$elf" build_xmb
        need_sym "$elf" build_terminal
        need_sym "$elf" build_cabinet
        forbid_sym "$elf" build_quake
        forbid_sym "$elf" start_sim_render
        bin=".build/picocalc-${board}/btron-picocalc-${board}.bin"
        sd="btron-picocalc-${board}-sdboot.bin"
    fi

    if [ "$board" = "rp2040" ]; then
        local n
        n="$(wc -c < "$bin" | tr -d ' ')"
        if [ "$n" -gt "$RP2040_BIN_LIMIT" ]; then
            echo "[FAIL] $bin is $n bytes, limit $RP2040_BIN_LIMIT"
            exit 1
        fi
        n="$(wc -c < "$sd" | tr -d ' ')"
        if [ "$n" -gt "$RP2040_SD_LIMIT" ]; then
            echo "[FAIL] $sd is $n bytes, limit $RP2040_SD_LIMIT"
            exit 1
        fi
        echo "  [PASS] $board $kind flash window"
    fi
}

check_elf ".build/picocalc-rp2040/btron-picocalc-rp2040.elf" gears rp2040
check_elf ".build/picocalc-rp2350/btron-picocalc-rp2350.elf" gears rp2350
check_elf ".build/picocalc-quake-rp2040/btron-picocalc-quake-rp2040.elf" quake rp2040
check_elf ".build/picocalc-quake-rp2350/btron-picocalc-quake-rp2350.elf" quake rp2350

echo "=========================================================="
echo " [TEST-PICOCALC] Gears and quake images verified on both boards."
echo "=========================================================="
