#!/usr/bin/env python3
"""BLE validation for the custom ZG-228Z firmware (BLE mode).

Bridge the TX pad (PB1) to GND (or trigger BLE mode as described in AGENTS.md),
then run:

    python3 tools/ble_test.py                # scan for the device
    python3 tools/ble_test.py play            # play a test melody right away
    python3 tools/ble_test.py slot 1 "siren:d=4,o=6,b=200:c,g,c,g"
    python3 tools/ble_test.py read 1          # read back slot 1

Needs `pip install bleak` (in a venv - nothing is installed system wide).

Protocol (see custom_fw/src/app_ble.c):
  - service  UUID 128-bit base ...FFE0
  - char FFE1 "play":  write chunks of the RTTTL string; every write appends.
    A 0x00 byte terminates the string -> the device plays it.
  - char FFE2/FFE3/FFE4 "slot1..3": same chunked write, on the terminator the
    string is stored in NV slot 1..3 (confirmed by a short beep; two beeps =
    rejected). Reads return the stored melody (fixed 192-byte, zero padded).
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

BASE = "0000{:04x}-0000-1000-8000-00805f9b34fb"
SVC = BASE.format(0xFFE0)
CHAR_PLAY = BASE.format(0xFFE1)
CHAR_SLOTS = {1: BASE.format(0xFFE2), 2: BASE.format(0xFFE3), 3: BASE.format(0xFFE4)}


async def find_device():
    print("scanning for 'ZG228z' (10 s)...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or "").startswith("ZG228z"), timeout=10.0
    )
    if dev is None:
        sys.exit("device not found - is the TX pad bridged to GND?")
    print(f"found: {dev} ({dev.name})")
    return dev


async def send_rtttl(client, char, rtttl: str, terminator=True):
    data = rtttl.encode() + (b"\x00" if terminator else b"")
    # 20 bytes per write works with any MTU
    for i in range(0, len(data), 20):
        await client.write_gatt_char(char, data[i:i + 20], response=False)
        await asyncio.sleep(0.05)


async def main():
    if len(sys.argv) < 2:
        dev = await find_device()
        async with BleakClient(dev) as client:
            for uuid in [CHAR_PLAY] + list(CHAR_SLOTS.values()):
                print(uuid)
        return

    what = sys.argv[1]
    dev = await find_device()
    async with BleakClient(dev) as client:
        if what == "play":
            rtttl = sys.argv[2] if len(sys.argv) > 2 else "test:d=4,o=6,b=200:c,e,g,>c"
            print(f"play: {rtttl}")
            await send_rtttl(client, CHAR_PLAY, rtttl)
            print("sent (you should hear it now)")
        elif what == "slot":
            slot = int(sys.argv[2])
            rtttl = sys.argv[3]
            print(f"store slot {slot}: {rtttl}")
            await send_rtttl(client, CHAR_SLOTS[slot], rtttl)
            print("sent (one beep = stored, two beeps = rejected)")
        elif what == "read":
            slot = int(sys.argv[2])
            data = await client.read_gatt_char(CHAR_SLOTS[slot])
            rtttl = data.split(b"\x00")[0].decode(errors="replace")
            print(f"slot {slot}: {rttl!r}")
        else:
            sys.exit(__doc__)


if __name__ == "__main__":
    asyncio.run(main())
