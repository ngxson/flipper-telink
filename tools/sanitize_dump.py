#!/usr/bin/env python3
"""Remove Zigbee network secrets from a Telink Zigbee SDK flash dump.

Blanks (0xFF) the SDK's NV modules that hold network state, which is the same
as a factory-new device: firmware, MAC/calibration and app settings (module 6)
are kept. The device has to be paired again if this image is flashed.

  module 0 ZB_INFO   (NIB: PAN ID, ext PAN ID, addresses)   0x34000
  module 1 ADDR      (address table)                         0x36000
  module 2 APS       (SSIB: network key, TC address)         0x38000
  module 3 ZCL       (attributes / reporting)                0x3A000
  module 4 NWK_FC    (frame counters)                        0x3C000
  module 7 KEYPAIR   (APS link keys with the trust center)   0x7C000

Usage: sanitize_dump.py <in.bin> <out.bin>
"""
import sys
import zlib

WIPE = [(0x34000, 0x3E000), (0x7C000, 0x7E000)]
SSIB_TAG = b'\x04\x7a'  # module 2 item 0x04 (SSIB), network key follows at +0x12
KEY_OFS = 0x12


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = bytearray(open(src, 'rb').read())
    if d[8:12] != b'KNLT':
        sys.exit('%s: no Telink KNLT header, refusing' % src)

    keys = set()
    o = d.find(SSIB_TAG, 0x38000, 0x3A000)
    while o >= 0:
        k = bytes(d[o + KEY_OFS:o + KEY_OFS + 16])
        if k != b'\xff' * 16:
            keys.add(k)
        o = d.find(SSIB_TAG, o + 1, 0x3A000)

    for start, end in WIPE:
        d[start:end] = b'\xff' * (end - start)

    for k in keys:
        if d.find(k) >= 0:
            sys.exit('key %s still present, refusing' % k.hex())

    open(dst, 'wb').write(d)
    print('%s -> %s  crc32 %08X, network key candidates removed: %d' %
          (src, dst, zlib.crc32(d), len(keys)))


if __name__ == '__main__':
    main()
