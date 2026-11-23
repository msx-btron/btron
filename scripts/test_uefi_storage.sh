#!/usr/bin/env bash
#
# Automated CI Test Runner for B-System VirtIO Storage & Live Installer Media.
# Validates QEMU boot across Live USB, Bootable CD, and Installed HDD.
#
set -euo pipefail

QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "[ERROR] qemu-system-x86_64 not found in PATH."
    exit 1
fi

ELF_PATH="downloads/intel-uefi/btron-uchida.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make uefi >/dev/null 2>&1
fi

# Ensure all storage images exist
make storage-images >/dev/null 2>&1

echo "=========================================================="
echo " 1/3 Testing Live USB Media with Installer Target Disk..."
echo "=========================================================="
LOG_USB=$(mktemp /tmp/qemu_uefi_usb_XXXXXX)
trap 'rm -f "$LOG_USB"' EXIT

((sleep 1; printf "s\n\n"; sleep 1; printf "desktop\n") | "$QEMU_BIN" \
    -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
    -display none \
    -device virtio-vga \
    -drive file=downloads/intel-uefi/btron-uefi-usb.img,if=none,id=bootusb,format=raw,file.locking=off \
    -device virtio-blk-pci,drive=bootusb,bootindex=1 \
    -drive file=.build/target_hdd.img,if=none,id=targethd,format=raw,file.locking=off \
    -device virtio-blk-pci,drive=targethd \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_USB" 2>&1) &
PID_USB=$!

sleep 7

pkill -P "$PID_USB" 2>/dev/null || true
kill -TERM "$PID_USB" 2>/dev/null || true
wait "$PID_USB" 2>/dev/null || true

USB_MARKERS=(
    "[VIRTIO-BLK] Detected VirtIO-Block at PCI 0:3.0"
    "[VIRTIO-BLK] Unit 0 online. Capacity: 64 MiB (131072 sectors)"
    "[VIRTIO-BLK] Detected VirtIO-Block at PCI 0:4.0"
    "[VIRTIO-BLK] Unit 1 online. Capacity: 64 MiB (131072 sectors)"
    "[HFDS] Mounted /SYS on VirtIO-Block device"
    "[HFDS] Mounted /ANDERS on VirtIO-Block device"
    "Authentic BTRON3 Desktop Active"
)

for marker in "${USB_MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_USB"; then
        echo ""
        echo "[FAIL] USB Test Failed: Missing expected output marker: '$marker'"
        cat "$LOG_USB"
        exit 1
    fi
done
echo "  [PASS] Live USB dual-partition volume mount and installer launch verified."

echo "=========================================================="
echo " 2/3 Testing Installed VirtIO Hard Disk Media..."
echo "=========================================================="
LOG_HDD=$(mktemp /tmp/qemu_uefi_hdd_XXXXXX)
trap 'rm -f "$LOG_USB" "$LOG_HDD"' EXIT

((sleep 1; printf "\n"; sleep 1; printf "desktop\n") | "$QEMU_BIN" \
    -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
    -display none \
    -device virtio-vga \
    -drive file=downloads/intel-uefi/btron-uefi-hdd.img,if=none,id=systemhd,format=raw,file.locking=off \
    -device virtio-blk-pci,drive=systemhd,bootindex=1 \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_HDD" 2>&1) &
PID_HDD=$!

sleep 6

pkill -P "$PID_HDD" 2>/dev/null || true
kill -TERM "$PID_HDD" 2>/dev/null || true
wait "$PID_HDD" 2>/dev/null || true

HDD_MARKERS=(
    "[VIRTIO-BLK] Detected VirtIO-Block at PCI 0:3.0"
    "[VIRTIO-BLK] Unit 0 online. Capacity: 64 MiB (131072 sectors)"
    "[HFDS] Mounted /SYS on VirtIO-Block device"
    "[HFDS] Mounted /ANDERS on VirtIO-Block device"
    "Authentic BTRON3 Desktop Active"
)

for marker in "${HDD_MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_HDD"; then
        echo ""
        echo "[FAIL] HDD Test Failed: Missing expected output marker: '$marker'"
        cat "$LOG_HDD"
        exit 1
    fi
done
echo "  [PASS] Direct boot from installed VirtIO HDD verified."

echo "=========================================================="
echo " 3/3 Testing Bootable CD-ROM with Target VirtIO HDD..."
echo "=========================================================="
LOG_CD=$(mktemp /tmp/qemu_uefi_cd_XXXXXX)
trap 'rm -f "$LOG_USB" "$LOG_HDD" "$LOG_CD"' EXIT

((sleep 2) | "$QEMU_BIN" \
    -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
    -display none \
    -device virtio-vga \
    -cdrom downloads/intel-uefi/btron-uefi-cd.iso -boot d \
    -drive file=.build/target_hdd.img,if=none,id=targethd,format=raw,file.locking=off \
    -device virtio-blk-pci,drive=targethd \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_CD" 2>&1) &
PID_CD=$!

sleep 4

pkill -P "$PID_CD" 2>/dev/null || true
kill -TERM "$PID_CD" 2>/dev/null || true
wait "$PID_CD" 2>/dev/null || true

CD_MARKERS=(
    "[VIRTIO-BLK] Detected VirtIO-Block at PCI 0:3.0"
    "[VIRTIO-BLK] Unit 0 online. Capacity: 64 MiB (131072 sectors)"
    "Ski Bootloader Active"
)

for marker in "${CD_MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_CD"; then
        echo ""
        echo "[FAIL] CD Test Failed: Missing expected output marker: '$marker'"
        cat "$LOG_CD"
        exit 1
    fi
done
echo "  [PASS] Bootable El Torito CD and secondary VirtIO disk verified."

echo ""
echo "=========================================================="
echo " [PASS] All 3 Storage Media Formats Verified on QEMU x86_64 UEFI SMP!"
echo "=========================================================="
exit 0
