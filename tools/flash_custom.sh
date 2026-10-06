#!/bin/sh
# Flash the custom ZG-228Z firmware to the ZG-226Z via the Flipper SWS app.
# See AGENTS.md ("Flash" under the custom firmware task) for the full procedure.
#
# Prerequisites:
#   - custom firmware built:  custom_fw/bin/ZG228Z.bin   (docker build, see AGENTS.md)
#   - ZG-226Z wired to the Flipper: SWS -> C0, GND -> GND, 3V3 rail ON
#   - stock dump backed up already (dumps/private/zg226z_full.bin) for rollback
#
# Usage: tools/flash_custom.sh [path-to-bin]
set -e

PORT=/dev/cu.usbmodemflip_Ngxson1
PY=".ufbt/toolchain/current/bin/python3"
STORAGE=".ufbt/current/scripts/storage.py"
FCLI="python3 tools/fcli.py"
BIN="${1:-custom_fw/bin/ZG228Z.bin}"

[ -f "$BIN" ] || { echo "missing $BIN (build it first)"; exit 1; }

echo "== 1. copy image to the Flipper SD card"
"$PY" "$STORAGE" -p "$PORT" send "$BIN" /ext/ZG228Z.bin

echo "== 2. make sure the app is running"
$FCLI "input send back press" >/dev/null 2>&1 || true
$FCLI "input send back short" >/dev/null 2>&1 || true
$FCLI "input send back release" >/dev/null 2>&1 || true
sleep 1

echo "== 3. halt the target (press the device button during the spam window!)"
echo "     run this manually if it fails:  sws act c0 10000"
$FCLI "sws act c0 10000" || { echo "activation failed - press the button and retry"; exit 1; }

echo "== 4. check the flash (expect JEDEC C8 60 13, a first garbage read is normal)"
$FCLI "sws jedec"

echo "== 5. flash the image at 0 (erase + write + verify)"
$FCLI "sws flash /ext/ZG228Z.bin 0 1"

echo "== 6. erase the Zigbee/BLE NV areas so the device starts factory-new"
$FCLI "sws erase 0x34000 0xA000"   # NV modules 0-4 (ZB_INFO .. NWK frame counters)
$FCLI "sws erase 0x74000 0x2000"   # NV_BLE
$FCLI "sws erase 0x7A000 0x4000"   # NV module 6 (app) + keypairs

echo "== 7. resume the target - it should boot the custom firmware"
$FCLI "sws run"

echo "done. Remove the SWS wire, then pair it with Zigbee2MQTT (permit join on)."
echo "For BLE mode instead: bridge the TX pad (PB1) to GND and (re)boot."
