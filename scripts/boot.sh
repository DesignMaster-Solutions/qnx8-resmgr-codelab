#!/usr/bin/env bash
# Step 6: boot the image with QEMU on the Mac (no network, console to vm/output/console.log) and check
# that /dev/hello answered as the codelab says. Exits 0 only if every expected line is there.
#   scripts/boot.sh [vm dir] [timeout seconds]
set -euo pipefail
VM=${1:-vm}; T=${2:-300}
cd "$VM"
LOG=output/console.log
rm -f "$LOG"
qemu-system-x86_64 -smp 2 -cpu max -m 1G -no-reboot -display none -monitor none \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 -kernel output/ifs.bin -serial file:"$LOG" \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 </dev/null &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
for ((i = 0; i < T; i++)); do
  grep -q "codelab: done" "$LOG" 2>/dev/null && break
  kill -0 "$pid" 2>/dev/null || break
  sleep 1
done
sleep 2 # let the last lines reach the log
tr -d '\r' < "$LOG" | grep -E '^(codelab:|c[-rwx]{9} | *[0-9]+ +1 system/bin/ticks)' || true
grep -q "codelab: read 1: Hello from a QNX 8.0 resource manager" "$LOG"
grep -q "codelab: read 2: Hi from the shell" "$LOG"
grep -q "codelab: long write refused" "$LOG"
grep -q "codelab: read 3: Hi from the shell" "$LOG"
grep -q "codelab: ticks grew" "$LOG"
grep -Eq "system/bin/ticks +20f " "$LOG"
echo "boot: all checks passed"
