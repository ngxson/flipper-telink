#!/usr/bin/env python3
"""Close DAP Link over USB so the normal Flipper CLI becomes available."""
import argparse
import sys
import time

import serial.tools.list_ports
import usb.core
import usb.util


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("exit", "reboot"))
    parser.add_argument("--serial", default="DAP_Ngxson")
    args = parser.parse_args()
    devices = usb.core.find(find_all=True, idVendor=0x0483, idProduct=0x5740)
    dev = next((d for d in devices if d.serial_number == args.serial), None)
    if dev is None:
        parser.error(f"Flipper DAP {args.serial} is not connected")
    try:
        intf = next(i for i in dev.get_active_configuration() if i.bInterfaceClass == 0xFF)
        ep_out = next(e for e in intf if usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        ep_in = next(e for e in intf if usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
        usb.util.claim_interface(dev, intf.bInterfaceNumber)
        command = 0x82 if args.command == "exit" else 0x81
        ep_out.write(bytes([command]), 2000)
        try:
            reply = bytes(ep_in.read(64, 2000))
            if reply != bytes([command]):
                raise RuntimeError(f"Unexpected response: {reply.hex()}")
        except usb.core.USBError:
            # Reboot (or exit) can remove the interface before the reply arrives.
            # Only report success once normal Flipper USB actually reappears.
            pass
    finally:
        usb.util.dispose_resources(dev)
    cli_serial = args.serial.replace("DAP_", "flip_", 1)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        for port in serial.tools.list_ports.comports():
            if port.serial_number == cli_serial:
                print(f"Flipper CLI ready: {port.device}")
                return 0
        time.sleep(0.2)
    print("Flipper CLI did not reappear; the installed app may not support exit (0x82).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
