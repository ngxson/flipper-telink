#!/usr/bin/env python3
"""Raw CMSIS-DAP v2 client via pyusb - full visibility into target ACKs."""
import sys
import time
import usb.core
import usb.util

dev = usb.core.find(idVendor=0x0483, idProduct=0x5740)
if dev is None:
    print("no ST 0483:5740 device"); sys.exit(1)
try:
    serial = dev.serial_number
except Exception:
    serial = None
print("serial:", serial)
cfg = dev.get_active_configuration()
EP_OUT = EP_IN = None
interface_number = None
for intf in cfg:
    eps_out = [ep for ep in intf if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT]
    eps_in = [ep for ep in intf if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN]
    # DAP v2 bulk: interface class 0xff (vendor) with 2 bulk eps
    if intf.bInterfaceClass == 0xff and eps_out and eps_in:
        EP_OUT, EP_IN = eps_out[0], eps_in[0]
        interface_number = intf.bInterfaceNumber
        print("DAP v2 on iface", intf.bInterfaceNumber, "EP out/in:", hex(EP_OUT.bEndpointAddress), hex(EP_IN.bEndpointAddress))
if EP_OUT is None:
    print("no vendor bulk interface found; dumping config:")
    for intf in cfg:
        print("iface %d class %#x eps:" % (intf.bInterfaceNumber, intf.bInterfaceClass),
              [hex(e.bEndpointAddress) for e in intf])
    sys.exit(1)

usb.util.claim_interface(dev, interface_number)

def xfer(data, timeout=2000):
    EP_OUT.write(bytes(data), timeout)
    return EP_IN.read(512, timeout)

class Dap:
    """free-dap on Flipper speaks CMSIS-DAP v2 WITHOUT the sequence byte
    (same as OpenOCD's v2 backend), so send commands directly."""
    def cmd(self, data, timeout=2000):
        EP_OUT.write(bytes(data), timeout)
        r = EP_IN.read(512, timeout)
        return bytes(r)

d = Dap()
def info(i):
    r = d.cmd([0x00, i])
    print(f"  DAP_Info({i}):", r.hex() if r else None)
    if r and r[0] == 0 and len(r) >= 2:
        n = r[1]
        return r[2:2+n]
    return None

v = info(0xF0)  # capabilities (4 is the protocol version string)
print("DAP capabilities:", v.hex() if v else None)

print("DAP_Connect(SWD):", d.cmd([0x02, 0x01]).hex())
print("DAP_SWJ_Clock(100k):", d.cmd([0x11] + list((100000).to_bytes(4, "little"))).hex())
print("DAP_TransferConfigure:", d.cmd([0x04, 0, 100, 0, 0, 0]).hex())
print("DAP_SWD_Configure:", d.cmd([0x13, 0]).hex())

# swj sequences: >50 TMS high, JTAG-to-SWD 0xE79E (16 bits), >50 TMS high
hi = b"\xff" * 7  # 56 clocks of 1
r = d.cmd([0x12, 56] + list(hi)); print("swj 56x1:", r.hex())
r = d.cmd([0x12, 16, 0x9E, 0xE7]); print("swj jtag2swd:", r.hex())
r = d.cmd([0x12, 56] + list(hi)); print("swj 56x1:", r.hex())
r = d.cmd([0x12, 8, 0x00]); print("swj idle:", r.hex())

# DAP_Transfer uses request flags (DP read = 0x02), not the wire byte 0xA5.
found = False
for t in range(6):
    r = d.cmd([0x05, 0x00, 0x01, 0x02])
    print(f"DAP_Transfer(DPIDR) try {t}:", r.hex())
    if len(r) >= 3 and r[0] == 0x05:
        count, status = r[1], r[2]
        val = int.from_bytes(r[3:7], "little")
        print(f"  status {status:#x} count {count} value {val:#010x}")
        if status == 1 and count == 1 and len(r) >= 7 and val not in (0, 0xFFFFFFFF):
            print(f"*** TARGET ALIVE: DPIDR {val:#010x}, designer {(val>>1)&0x3ff:#05x}, partno {(val>>20)&0xff:#04x} ***")
            found = True
            break
        if status in (2,):  # WAIT
            time.sleep(0.1)
    time.sleep(0.2)
print("DAP_Disconnect:", d.cmd([0x03]).hex())
usb.util.release_interface(dev, interface_number)
usb.util.dispose_resources(dev)
sys.exit(0 if found else 1)
