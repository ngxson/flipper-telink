#!/usr/bin/env python3
"""Remove Zigbee network secrets (and Tuya device credentials) from a Telink
Zigbee SDK flash dump.

Blanks (0xFF) the SDK's NV modules that hold network state, which is the same
as a factory-new device: firmware, MAC/calibration and app settings (module 6)
are kept. The device has to be paired again if this image is flashed.

  module 0 ZB_INFO   (NIB: PAN ID, ext PAN ID, addresses)
  module 1 ADDR      (address table)
  module 2 APS       (SSIB: network key, TC address)
  module 3 ZCL       (attributes / reporting)
  module 4 NWK_FC    (frame counters)
  module 7 KEYPAIR   (APS link keys with the trust center)
  module 0x0B        (Tuya: copy of PAN ID, channel, ext PAN ID)

Each module owns an 8 KB region whose sectors start with `5a 5a <module>`
(`7a 7a <module>` in the older SDK of the ZG-204ZL / ZG-226Z firmware).
The regions are found by scanning, so both layouts seen so far work:
  512 KB Telink SDK: modules 0-4 at 0x34000-0x3DFFF, module 7 at 0x7C000
  1 MB Tuya (ZT3L):  module n at 0xD8000 + n * 0x2000

Tuya firmware also keeps per-device cloud credentials in a sector of its own
(ZT3L: 0xFB000): product id at +0, a 64-hex-digit key at +0x14, the 32-char
auzKey at +0x68 and a MAC copy at +0xB0. That sector is found by its
auzKey-shaped string and blanked, except for the (public) product id.

Usage: sanitize_dump.py <in.bin> <out.bin>
"""
import re
import struct
import sys
import zlib

NV_MAGIC = (b'\x5a\x5a', b'\x7a\x7a')
WIPE_MODULES = {0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x0B}
SSIB_ITEM = 0x04  # module 2: network key at +0x10 of the item data
KEYPAIR_ITEM = 0x08  # module 7: TC IEEE (8) + link key (16)
NIB_ITEM = 0x01  # module 0

# Tuya auzKey: 32 alphanumerics, NUL terminated, mixed case + digits
AUZKEY_RE = re.compile(rb'(?<![0-9A-Za-z])([0-9A-Za-z]{32})\x00')
HEXKEY_RE = re.compile(rb'(?<![0-9a-fA-F])([0-9a-fA-F]{64})\x00')
PID_RE = re.compile(rb'[0-9a-z]{8}\x00')


def app_end(d):
    """End of the firmware image(s): KNLT header at 0 and, with a Tuya
    bootloader, at 0x8000. Size is the u32 at +0x18."""
    end = 0
    for base in (0x0, 0x8000):
        if d[base + 8:base + 12] == b'KNLT':
            end = max(end, base + struct.unpack_from('<I', d, base + 0x18)[0])
    return end


def nv_items(d, sec):
    """(addr, len, item_id) of the index entries of an NV sector."""
    out = []
    o = sec + 4
    while o + 8 <= sec + 0x1000 and d[o:o + 4] != b'\xff' * 4:
        a, ln, iid, _st = struct.unpack_from('<IHBB', d, o)
        out.append((a, ln, iid))
        o += 8
    return out


def find_modules(d):
    """{module id: 8 KB region start} for the modules to wipe."""
    found = {}
    for sec in range((app_end(d) + 0xfff) & ~0xfff, len(d), 0x1000):
        mod = d[sec + 2]
        if d[sec:sec + 2] not in NV_MAGIC or mod not in WIPE_MODULES:
            continue
        region = sec & ~0x1fff
        items = nv_items(d, sec)
        # module 4 has its own counter format; the others must index into their region
        if mod != 0x04 and not (items and region <= items[0][0] < region + 0x2000):
            continue
        found.setdefault(mod, region)
    return found


def is_auzkey(s):
    return (re.search(rb'[A-Z]', s) and re.search(rb'[a-z]', s)
            and re.search(rb'[0-9]', s)) is not None


def tuya_auth(d):
    """{sector: [secret strings]} of Tuya credential sectors after the app."""
    out = {}
    start = (app_end(d) + 0xfff) & ~0xfff
    for m in AUZKEY_RE.finditer(d, start):
        if not is_auzkey(m.group(1)):
            continue
        sec = m.start() & ~0xfff
        found = out.setdefault(sec, [])
        found.append(bytes(m.group(1)))
        for h in HEXKEY_RE.finditer(d, sec, sec + 0x1000):
            found.append(bytes(h.group(1)))
    return out


def secrets(d, regions):
    """Byte strings that must not survive anywhere in the output."""
    out = set()

    def latest(mod, iid):
        if mod not in regions:
            return None
        hit = None
        for sec in (regions[mod], regions[mod] + 0x1000):
            if d[sec:sec + 2] in NV_MAGIC:
                for a, ln, i in nv_items(d, sec):
                    if i == iid and a + ln <= len(d):
                        hit = d[a + 8:a + ln]  # skip the 8-byte item header
        return hit

    ssib = latest(0x02, SSIB_ITEM)
    if ssib:
        out.add(bytes(ssib[0x10:0x20]))  # network key
    kp = latest(0x07, KEYPAIR_ITEM)
    if kp:
        out.add(bytes(kp[0:8]))  # trust center IEEE
        out.add(bytes(kp[8:24]))  # APS link key
    nib = latest(0x00, NIB_ITEM)
    if nib:
        out.add(bytes(nib[0x04:0x0c]))  # ext PAN ID / TC address
        out.add(bytes(nib[0x50:0x58]))  # ext PAN ID
    return {s for s in out if len(set(s)) > 2}  # skip blank / zero fields


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = bytearray(open(src, 'rb').read())
    if d[8:12] != b'KNLT':
        sys.exit('%s: no Telink KNLT header, refusing' % src)

    regions = find_modules(d)
    if 0x02 not in regions:
        sys.exit('no NV module 2 (APS) found, refusing: unknown layout or already sanitized')
    found = secrets(d, regions)

    for mod, start in sorted(regions.items()):
        print('  wiping module %02X at 0x%05X-0x%05X' % (mod, start, start + 0x1fff))
        d[start:start + 0x2000] = b'\xff' * 0x2000

    for sec, keys in sorted(tuya_auth(d).items()):
        found.update(keys)
        pid = bytes(d[sec:sec + 9]) if PID_RE.fullmatch(d[sec:sec + 9]) else b''
        print('  wiping Tuya credentials at 0x%05X-0x%05X%s' %
              (sec, sec + 0xfff, ' (keeping product id %s)' % pid[:-1].decode() if pid else ''))
        d[sec:sec + 0x1000] = b'\xff' * 0x1000
        d[sec:sec + len(pid)] = pid

    for s in found:
        if d.find(s) >= 0:
            sys.exit('secret %s still present at 0x%X, refusing' % (s.hex(), d.find(s)))
    left = tuya_auth(d)
    if left:
        sys.exit('auzKey-shaped string still present at %s, refusing' %
                 ', '.join('0x%05X' % a for a in left))

    open(dst, 'wb').write(d)
    print('%s -> %s  crc32 %08X, secrets checked absent: %d' %
          (src, dst, zlib.crc32(d), len(found)))


if __name__ == '__main__':
    main()
