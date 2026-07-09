#!/usr/bin/env bash

# test-dualbank.sh
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

# STM32U585 DUALBANK_SWAP fallback test, on the m33mu emulator.
#
# Exercises the hardware-assisted bank swap and the fallback-after-
# verification-failure path in a single emulator session (three boots):
#
#   boot 1: BOOT empty, valid v2 in UPDATE. wolfBoot verifies the update,
#           activates SWAP_BANK and reboots.
#   boot 2: the v2 app (see stm32u585-dualbank/staging_app.c) runs from
#           the logical BOOT partition (physical bank 2), stages a fake
#           v3 image with a broken integrity record into the logical
#           UPDATE partition (physical bank 1), and reboots.
#   boot 3: wolfBoot picks the fake v3 update, verification fails, and it
#           must erase the corrupt UPDATE image and fall back to v2. The
#           v2 app then reports success with bkpt 0x7f.
#
# This is a regression test for the erase bank selection under
# SWAP_BANK (BKER refers to the physical bank on STM32U5, RM0456
# 7.5.8): with a logical-address-derived BKER, boot 3 erases the
# healthy v2 image instead of the corrupt update and the device bricks.
# The flash trace is also checked so that a regression cannot hide
# behind an accidentally-successful boot.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="${WOLFBOOT_ROOT:-$(cd "$script_dir/../.." && pwd)}"
# The top-level Makefile defaults WOLFBOOT_ROOT to $(PWD), which is the
# caller's directory when invoked via make -C: export the real root so
# tool paths (keygen/sign) resolve correctly wherever this script runs.
export WOLFBOOT_ROOT
EMU_APPS="$WOLFBOOT_ROOT/test-app/emu-test-apps"
EMU_DIR=stm32u585-dualbank
EMU_PATH="$EMU_APPS/$EMU_DIR"
M33MU="${M33MU:-$(command -v m33mu || true)}"

log() {
  echo "==> $*"
}

die() {
  echo "error: $*" >&2
  exit 1
}

cfg_get() {
  local key="$1"
  local val
  val="$(grep -m1 -E "^${key}[?]*[:]*=" "$WOLFBOOT_ROOT/.config" 2>/dev/null | sed -E "s/^${key}[?]*[:]*=//" || true)"
  echo "${val}"
}

[[ -n "$M33MU" ]] || die "m33mu not found (set M33MU=/path/to/m33mu)"
[[ -f "$WOLFBOOT_ROOT/.config" ]] || die "missing .config (use config/examples/stm32u5-nonsecure-dualbank.config)"

TARGET="$(cfg_get TARGET)"
DUALBANK_SWAP="$(cfg_get DUALBANK_SWAP)"
[[ "$TARGET" == "stm32u5" ]] || die "TARGET=$TARGET, this test needs TARGET=stm32u5"
[[ "$DUALBANK_SWAP" == "1" ]] || die "DUALBANK_SWAP=$DUALBANK_SWAP, this test needs DUALBANK_SWAP=1"

IMAGE_HEADER_SIZE="$(cfg_get IMAGE_HEADER_SIZE)"
IMAGE_HEADER_SIZE="${IMAGE_HEADER_SIZE:-1024}"
BOOT_ADDR="$(cfg_get WOLFBOOT_PARTITION_BOOT_ADDRESS)"
UPDATE_ADDR="$(cfg_get WOLFBOOT_PARTITION_UPDATE_ADDRESS)"
PART_SIZE="$(cfg_get WOLFBOOT_PARTITION_SIZE)"
[[ -n "$BOOT_ADDR" && -n "$UPDATE_ADDR" && -n "$PART_SIZE" ]] || \
  die "missing partition addresses/size in .config"

ARCH_OFFSET=0x08000000
BOOT_OFFSET=$((BOOT_ADDR - ARCH_OFFSET))
UPDATE_OFFSET=$((UPDATE_ADDR - ARCH_OFFSET))
UPDATE_OFFSET_HEX=$(printf "0x%x" "$UPDATE_OFFSET")

STDBUF=""
if command -v stdbuf >/dev/null 2>&1; then
  STDBUF="stdbuf -oL -eL"
fi

RUN_LOG="$EMU_PATH/dualbank_fallback.log"

log "Rebuilding wolfboot.bin"
make -C "$WOLFBOOT_ROOT" clean wolfboot.bin

log "Building and signing staging app (v2)"
make -C "$EMU_APPS" TARGET=stm32u5 EMU_DIR="$EMU_DIR" EMU_VERSION=2 \
  IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu

[[ -f "$EMU_PATH/image_v2_signed.bin" ]] || die "missing signed staging app"

log "Running swap + staged-corrupt-update + fallback cycle (3 boots)"
: >"$RUN_LOG"
set +e
M33MU_FLASH_TRACE=1 $STDBUF "$M33MU" --cpu stm32u585 --dualbank \
  "$WOLFBOOT_ROOT/wolfboot.bin" \
  "$EMU_PATH/image_v2_signed.bin:$UPDATE_OFFSET_HEX" \
  --timeout 120 --expect-bkpt 0x7f >"$RUN_LOG" 2>&1
emu_rc=$?
set -e

if ! grep -q "\[EXPECT BKPT\] Success" "$RUN_LOG"; then
  tail -n 40 "$RUN_LOG" | sed 's/^/  | /'
  die "fallback failed: bkpt 0x7f not reached (m33mu rc=$emu_rc)"
fi
log "bkpt 0x7f hit: wolfBoot fell back to the healthy image"

resets="$(grep -c "System reset requested" "$RUN_LOG" || true)"
[[ "$resets" == "2" ]] || die "expected 2 system resets (swap + staging), got $resets"
log "2 system resets observed (swap activation + staging reboot)"

log "Checking erase targets in the flash trace"
python3 - "$RUN_LOG" "$BOOT_OFFSET" "$UPDATE_OFFSET" "$PART_SIZE" <<'PY'
import re, sys

path, boot_s, update_s, size_s = sys.argv[1:5]
boot = int(boot_s, 0)
update = int(update_s, 0)
size = int(size_s, 0)

erases = []
for line in open(path, errors="replace"):
    m = re.search(r"\[FLASH_ERASE\].*start=0x([0-9a-fA-F]+) len=0x([0-9a-fA-F]+)", line)
    if m:
        erases.append((int(m.group(1), 16), int(m.group(2), 16)))

if not erases:
    sys.exit("no [FLASH_ERASE] lines found: flash trace missing?")

# The bug signature: any erase landing inside the BOOT partition, which
# holds the healthy fallback image for the whole session.
hit_boot = [e for e in erases if boot <= e[0] < boot + size]
if hit_boot:
    sys.exit("BUG: erase hit the BOOT partition (healthy image): "
             + ", ".join(f"0x{s:x}+0x{l:x}" for s, l in hit_boot))

# The corrupt update must have been erased (boot 3 fallback path).
hit_update = [e for e in erases if update <= e[0] < update + size]
if not hit_update:
    sys.exit("missing erase of the corrupt UPDATE image")

print(f"==> erase check ok: {len(hit_update)} update-partition erases, "
      f"BOOT partition untouched")
PY

log "ok: stm32u5 dual-bank swap fallback test passed"
