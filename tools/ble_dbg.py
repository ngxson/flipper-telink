#!/usr/bin/env python3
"""Debug console for the SS6400zb firmware: BLE log + text commands.

    python3 tools/ble_dbg.py                     # stream the log (whole buffer first), Ctrl-C to stop
    python3 tools/ble_dbg.py info                # run commands, then keep streaming for 8 s
    python3 tools/ble_dbg.py "raw 0f" scan -t 15 # several commands, stream 15 s

Commands (see custom_fw/ss6400zb/src/fp.c): info, scan, on, off, raw <hex>,
dump (resend the buffered log), clear. Max 20 bytes per command.
`wait:<seconds>` pauses between commands (host side only).
Subscribing to the log streams everything still buffered, then live lines.
Needs `pip install bleak` (in a venv).
"""
import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

BASE = "0000{:04x}-0000-1000-8000-00805f9b34fb"
LOG_CHAR = BASE.format(0xFFF1)
CMD_CHAR = BASE.format(0xFFF2)
DEFAULT_PREFIX = "SS6400_"


async def run(args):
    print(f"scanning for '{args.name}*'...", file=sys.stderr)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or ad.local_name or "").startswith(args.name), timeout=15.0
    )
    if dev is None:
        sys.exit("device not found")
    async with BleakClient(dev) as c:
        def on_log(_, data):
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()

        await c.start_notify(LOG_CHAR, on_log)
        await asyncio.sleep(1.0)  # buffered log
        for cmd in args.commands:
            if cmd.startswith("wait:"):
                await asyncio.sleep(float(cmd[5:]))
                continue
            if len(cmd) > 20:
                sys.exit(f"command too long (max 20 bytes): {cmd}")
            await c.write_gatt_char(CMD_CHAR, cmd.encode(), response=True)
            await asyncio.sleep(0.3)
        if args.commands:
            await asyncio.sleep(args.time)
        else:
            while True:
                await asyncio.sleep(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*")
    ap.add_argument("-t", "--time", type=float, default=8.0, help="seconds to stream after the commands")
    ap.add_argument("-n", "--name", default=DEFAULT_PREFIX, help="advertised name prefix")
    try:
        asyncio.run(run(ap.parse_args()))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
