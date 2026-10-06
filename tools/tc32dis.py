#!/usr/bin/env python3
"""Minimal Telink TC32 disassembler for raw flash images.

Opcode table from trust1995/Ghidra_TELink_TC32 (extracted from tc32-elf-objdump).
Usage: tc32dis.py <image.bin> <start> <end>
"""
import struct
import sys

INSNS16 = [
    (0x46c0, 0xffff, 'tnop'),
    (0x0000, 0xffc0, 'tand %0-2r, %3-5r'),
    (0x0040, 0xffc0, 'txor %0-2r, %3-5r'),
    (0x0080, 0xffc0, 'tshftl %0-2r, %3-5r'),
    (0x00c0, 0xffc0, 'tshftr %0-2r, %3-5r'),
    (0x0100, 0xffc0, 'tasr %0-2r, %3-5r'),
    (0x0140, 0xffc0, 'taddc %0-2r, %3-5r'),
    (0x0180, 0xffc0, 'tsubc %0-2r, %3-5r'),
    (0x01c0, 0xffc0, 'trotr %0-2r, %3-5r'),
    (0x0200, 0xffc0, 'tnand %0-2r, %3-5r'),
    (0x0240, 0xffc0, 'tneg %0-2r, %3-5r'),
    (0x0280, 0xffc0, 'tcmp %0-2r, %3-5r'),
    (0x02c0, 0xffc0, 'tcmpn %0-2r, %3-5r'),
    (0x0300, 0xffc0, 'tor %0-2r, %3-5r'),
    (0x0340, 0xffc0, 'tmul %0-2r, %3-5r'),
    (0x0380, 0xffc0, 'tbclr %0-2r, %3-5r'),
    (0x03c0, 0xffc0, 'tmovn %0-2r, %3-5r'),
    (0x6bc0, 0xfff8, 'tmcsr %0-2r'),
    (0x6bc8, 0xfff8, 'tmrcs %0-2r'),
    (0x6bd0, 0xfff8, 'tmssr %0-2r'),
    (0x6bd8, 0xfff8, 'tmrss %0-2r'),
    (0x6800, 0xfe00, 'treti %O'),
    (0x6000, 0xff80, 'tadd sp, #%0-6W'),
    (0x6080, 0xff80, 'tsub sp, #%0-6W'),
    (0x0700, 0xff80, 'tjex %S'),
    (0x0400, 0xff00, 'tadd %D, %S'),
    (0x0500, 0xff00, 'tcmp %D, %S'),
    (0x0600, 0xff00, 'tmov %D, %S'),
    (0x6400, 0xfe00, 'tpush %N'),
    (0x6c00, 0xfe00, 'tpop %O'),
    (0xe800, 0xfe00, 'tadd %0-2r, %3-5r, %6-8r'),
    (0xea00, 0xfe00, 'tsub %0-2r, %3-5r, %6-8r'),
    (0xec00, 0xfe00, 'tadd %0-2r, %3-5r, #%6-8d'),
    (0xee00, 0xfe00, 'tsub %0-2r, %3-5r, #%6-8d'),
    (0x1200, 0xfe00, 'tstorerh %0-2r, [%3-5r, %6-8r]'),
    (0x1a00, 0xfe00, 'tloadrh %0-2r, [%3-5r, %6-8r]'),
    (0x1600, 0xf600, 'tloadrs%11?hb %0-2r, [%3-5r, %6-8r]'),
    (0x1000, 0xfa00, "tstorer%10'b %0-2r, [%3-5r, %6-8r]"),
    (0x1800, 0xfa00, "tloadr%10'b %0-2r, [%3-5r, %6-8r]"),
    (0xf000, 0xf800, 'tshftl %0-2r, %3-5r, #%6-10d'),
    (0xf800, 0xf800, 'tshftr %0-2r, %3-5r, %s'),
    (0xe000, 0xf800, 'tasr %0-2r, %3-5r, %s'),
    (0xa000, 0xf800, 'tmov %8-10r, #%0-7d'),
    (0xa800, 0xf800, 'tcmp %8-10r, #%0-7d'),
    (0xb000, 0xf800, 'tadd %8-10r, #%0-7d'),
    (0xb800, 0xf800, 'tsub %8-10r, #%0-7d'),
    (0x0800, 0xf800, 'tloadr %8-10r, [pc, #%0-7W] ; %0-7a'),
    (0x5000, 0xf800, 'tstorer %0-2r, [%3-5r, #%6-10W]'),
    (0x5800, 0xf800, 'tloadr %0-2r, [%3-5r, #%6-10W]'),
    (0x4000, 0xf800, 'tstorerb %0-2r, [%3-5r, #%6-10d]'),
    (0x4800, 0xf800, 'tloadrb %0-2r, [%3-5r, #%6-10d]'),
    (0x2000, 0xf800, 'tstorerh %0-2r, [%3-5r, #%6-10H]'),
    (0x2800, 0xf800, 'tloadrh %0-2r, [%3-5r, #%6-10H]'),
    (0x3000, 0xf800, 'tstorer %8-10r, [sp, #%0-7W]'),
    (0x3800, 0xf800, 'tloadr %8-10r, [sp, #%0-7W]'),
    (0x7000, 0xf800, 'tadd %8-10r, pc, #%0-7W'),
    (0x7800, 0xf800, 'tadd %8-10r, sp, #%0-7W'),
    (0xd000, 0xf800, 'tstorem %8-10r!, %M'),
    (0xd800, 0xf800, 'tloadm %8-10r!, %M'),
    (0xcf00, 0xff00, 'tserv %0-7d'),
    (0xc000, 0xf000, 'tj%8-11c %0-7B'),
    (0x8000, 0xf800, 'tj %0-10B'),
]
CONDS = ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', '', 'nv']
REGS = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 'r11', 'r12', 'sp', 'lr', 'pc']


def bits(v, lo, hi):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def sext(v, nbits):
    return v - (1 << nbits) if v & (1 << (nbits - 1)) else v


def reglist(v, extra):
    regs = [REGS[i] for i in range(8) if v & (1 << i)]
    if v & 0x100:
        regs.append(extra)
    return '{' + ', '.join(regs) + '}'


class Img:
    def __init__(self, data):
        self.d = data

    def u16(self, a):
        return struct.unpack_from('<H', self.d, a)[0]

    def u32(self, a):
        return struct.unpack_from('<I', self.d, a)[0]


def fmt(img, addr, op, f, info):
    out = ''
    i = 0
    while i < len(f):
        ch = f[i]
        if ch != '%':
            out += ch
            i += 1
            continue
        i += 1
        if f[i] == 'N':
            out += reglist(op, 'lr'); i += 1; continue
        if f[i] == 'O':
            out += reglist(op, 'pc'); i += 1; continue
        if f[i] == 'M':
            out += reglist(op & 0xff, ''); i += 1; continue
        if f[i] == 'D':
            out += REGS[(op & 7) | ((op >> 4) & 8)]; i += 1; continue
        if f[i] == 'S':
            out += REGS[(op >> 3) & 15]; i += 1; continue
        if f[i] == 's':
            sh = bits(op, 6, 10)
            out += '#%d' % (sh or 32); i += 1; continue
        # bitfield %lo-hiX or %bitX
        j = i
        while f[j].isdigit():
            j += 1
        lo = int(f[i:j])
        if f[j] == '-':
            k = j + 1
            while f[k].isdigit():
                k += 1
            hi = int(f[j + 1:k])
            j = k
        else:
            hi = lo
        code = f[j]
        v = bits(op, lo, hi)
        if code == 'r':
            out += REGS[v]
        elif code == 'd':
            out += '%d' % v
        elif code == 'W':
            out += '%d' % (v * 4)
        elif code == 'H':
            out += '%d' % (v * 2)
        elif code == 'a':
            ea = ((addr + 4) & ~3) + v * 4
            val = img.u32(ea) if ea + 4 <= len(img.d) else None
            info['lit'] = val
            out += '[0x%x]=0x%08x' % (ea, val if val is not None else 0)
        elif code == 'B':
            nb = hi - lo + 1
            tgt = addr + 4 + sext(v, nb) * 2
            info['branch'] = tgt
            out += '0x%x' % tgt
        elif code == 'c':
            out += CONDS[v]
        elif code == "'":
            c2 = f[j + 1]
            out += c2 if v else ''
            j += 1
        elif code == '?':
            out += f[j + 1] if v else f[j + 2]
            j += 2
        i = j + 1
    return out


def decode(img, addr):
    op = img.u16(addr)
    info = {}
    # 32-bit tjl: 0x9000|hi11 then 0x9800|lo11
    if (op & 0xf800) == 0x9000 and addr + 4 <= len(img.d):
        op2 = img.u16(addr + 2)
        if (op2 & 0xf800) == 0x9800:
            off = sext(((op & 0x7ff) << 12) | ((op2 & 0x7ff) << 1), 23)
            tgt = addr + 4 + off
            info['call'] = tgt
            return 4, 'tjl 0x%x' % tgt, info
    for val, mask, f in INSNS16:
        if op & mask == val:
            return 2, fmt(img, addr, op, f, info), info
    return 2, '.short 0x%04x' % op, info


def main():
    img = Img(open(sys.argv[1], 'rb').read())
    a = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0)
    while a < end:
        n, s, _ = decode(img, a)
        raw = ' '.join('%04x' % img.u16(a + k) for k in range(0, n, 2))
        print('%06x: %-10s %s' % (a, raw, s))
        a += n


if __name__ == '__main__':
    main()
