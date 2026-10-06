#!/usr/bin/env python3
"""Push a firmware image over BLE with the Telink OTA protocol.

Works with the SS6400zb bring-up firmware (custom_fw/ss6400zb), and with any
Telink BLE firmware that exposes the standard OTA service.

    python3 tools/ble_ota.py scan                      # list Telink OTA devices
    python3 tools/ble_ota.py info [name|address]       # read the firmware revision
    python3 tools/ble_ota.py flash custom_fw/ss6400zb/bin/SS6400ZB.bin [name|address]

The device is picked by advertised name prefix (default "SS6400_") or by
address. Needs `pip install bleak` (in a venv, nothing is installed system wide).

Protocol (implemented on the device by the BLE library's otaWrite):
  - write [0x01, 0xff]                                  OTA start
  - write u16le(index) + 16 data bytes + u16le(crc16)   for every 16 bytes of
    the image (last block padded with 0xff); crc16 = CRC-16/MODBUS over the
    first 18 bytes
  - write [0x02, 0xff] + u16le(last) + u16le(~last)     OTA end
The image must carry the 0x5d 0x02 marker at offset 6 and its CRC32 at the end
(the build's tl_check_fw.py adds both); the device checks it, sets the boot
flag of the new slot, clears the old one and reboots.
"""
import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

OTA_SERVICE = "00010203-0405-0607-0809-0a0b0c0d1912"
OTA_CHAR = "00010203-0405-0607-0809-0a0b0c0d2b12"
FW_REV_CHAR = "00002a26-0000-1000-8000-00805f9b34fb"
DEFAULT_PREFIX = "SS6400_"

# read the characteristic back every N packets, so the writes without
# response can't run ahead of the link
SYNC_EVERY = 16


def crc16(data):
    crc = 0xFFFF
    for b in data:
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if (crc ^ b) & 1 else 0)
            b >>= 1
    return crc


def ota_packets(image):
    if image[6:8] != b"\x5d\x02":
        sys.exit("image has no 5d 02 marker at offset 6 (not built with tl_check_fw.py?)")
    size = struct.unpack_from("<I", image, 0x18)[0]
    if size != len(image):
        sys.exit(f"size field 0x{size:x} != file size 0x{len(image):x}")
    if len(image) % 16:
        image += b"\xff" * (16 - len(image) % 16)
    pkts = []
    for i in range(len(image) // 16):
        p = struct.pack("<H", i) + image[i * 16:(i + 1) * 16]
        pkts.append(p + struct.pack("<H", crc16(p)))
    return pkts


async def find_device(sel):
    if sel and ":" in sel or (sel and "-" in sel and len(sel) > 30):
        return sel  # address (macOS uses UUIDs)
    prefix = sel or DEFAULT_PREFIX
    print(f"scanning for '{prefix}*' (15 s)...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or ad.local_name or "").startswith(prefix), timeout=15.0
    )
    if dev is None:
        sys.exit("device not found")
    print(f"found {dev.address} ({dev.name})")
    return dev


async def scan():
    found = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for d, ad in found.values():
        if OTA_SERVICE in [u.lower() for u in ad.service_uuids] or (ad.local_name or "").startswith(DEFAULT_PREFIX):
            print(f"{d.address}  {ad.local_name}  rssi {ad.rssi}")


async def info(sel):
    dev = await find_device(sel)
    async with BleakClient(dev) as c:
        rev = await c.read_gatt_char(FW_REV_CHAR)
        print("firmware revision:", rev.decode(errors="replace"))


async def flash(path, sel):
    image = open(path, "rb").read()
    pkts = ota_packets(image)
    print(f"{path}: {len(image)} bytes, {len(pkts)} packets")
    dev = await find_device(sel)
    async with BleakClient(dev) as c:
        try:
            rev = await c.read_gatt_char(FW_REV_CHAR)
            print("running:", rev.decode(errors="replace"))
        except Exception:
            pass
        await c.write_gatt_char(OTA_CHAR, b"\x01\xff", response=False)
        t0 = time.time()
        for i, p in enumerate(pkts):
            await c.write_gatt_char(OTA_CHAR, p, response=False)
            if i % SYNC_EVERY == SYNC_EVERY - 1:
                await c.read_gatt_char(OTA_CHAR)
            if i % 256 == 255:
                rate = (i + 1) * 16 / (time.time() - t0) / 1024
                print(f"  {(i + 1) * 16:7d} / {len(image)}  {rate:.1f} KB/s", flush=True)
        await c.read_gatt_char(OTA_CHAR)
        last = len(pkts) - 1
        end = b"\x02\xff" + struct.pack("<HH", last, last ^ 0xFFFF)
        await c.write_gatt_char(OTA_CHAR, end, response=False)
        print(f"sent in {time.time() - t0:.1f} s, device verifies and reboots")
        await asyncio.sleep(2.0)
    print("reconnect with `info` to check the new revision / slot")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "scan":
        asyncio.run(scan())
    elif cmd == "info":
        asyncio.run(info(args[0] if args else None))
    elif cmd == "flash" and args:
        asyncio.run(flash(args[0], args[1] if len(args) > 1 else None))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
