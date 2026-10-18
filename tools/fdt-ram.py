#!/usr/bin/env python3
"""Show a live FDT's RAM and carve-outs and suggest mkadt.py --mem.

  adb exec-out cat /sys/firmware/fdt > dtb/live.dtb     # from recovery, adb root
  tools/fdt-ram.py dtb/live.dtb

The live tree is the one ABL handed to the running kernel: /memory holds the
real DDR ranges of this phone (6 GiB grus: low bank from 0x80000000 plus a
high bank from 0x100000000), /reserved-memory the static carve-outs. The
suggestion is the largest carve-out-free piece of the bank above 4 GiB,
32 MiB aligned at the start, as the shim and xnu expect.
"""
import struct, sys


def walk(blob):
    """Yield (path, props) for every node; props is {name: bytes}."""
    off_struct, off_strings = struct.unpack_from('>II', blob, 8)
    p, stack = off_struct, []
    nodes = []
    while True:
        tok, = struct.unpack_from('>I', blob, p)
        p += 4
        if tok == 1:
            end = blob.index(b'\0', p)
            name = blob[p:end].decode()
            p = (end + 4) & ~3
            stack.append((name, {}))
        elif tok == 2:
            name, props = stack.pop()
            nodes.append(('/'.join(n for n, _ in stack) + '/' + name, props))
            if not stack:
                return nodes
        elif tok == 3:
            ln, nameoff = struct.unpack_from('>II', blob, p)
            pname = blob[off_strings + nameoff:blob.index(b'\0', off_strings + nameoff)].decode()
            stack[-1][1][pname] = blob[p + 8:p + 8 + ln]
            p = (p + 8 + ln + 3) & ~3
        elif tok == 4:
            continue
        else:
            return nodes


def ranges(v):
    q = struct.unpack('>%dQ' % (len(v) // 8), v)
    return list(zip(q[0::2], q[1::2]))


def main():
    blob = open(sys.argv[1], 'rb').read()
    assert struct.unpack_from('>I', blob)[0] == 0xd00dfeed, 'not an FDT'
    mem, resv = [], []
    for path, props in walk(blob):
        parts = path.split('/')
        if len(parts) == 2 and parts[1].split('@')[0] == 'memory' and 'reg' in props:
            mem += ranges(props['reg'])
        if len(parts) == 3 and parts[1] == 'reserved-memory' and 'reg' in props:
            resv += [(b, s, parts[2]) for b, s in ranges(props['reg'])]
    print('/memory:')
    for b, s in mem:
        print(f'  {b:#012x} - {b + s:#012x}  ({s / 2**30:.2f} GiB)')
    print('/reserved-memory (static):')
    for b, s, n in sorted(resv):
        print(f'  {b:#012x} - {b + s:#012x}  {n}')
    best = None
    for b, s in mem:
        if b < 0x100000000:
            continue
        cuts = sorted((rb, rb + rs) for rb, rs, _ in resv if rb < b + s and rb + rs > b)
        lo = b
        for cb, ce in cuts + [(b + s, b + s)]:
            start = (lo + 0x1ffffff) & ~0x1ffffff
            end = cb & ~0x3fff
            if end > start and (best is None or end - start > best[1]):
                best = (start, end - start)
            lo = max(lo, ce)
    if best:
        print(f'\nsuggested: tools/mkadt.py OUT.adt --board grus --mem {best[0]:#x}:{best[1]:#x}')
    else:
        print('\nno usable range above 4 GiB')


if __name__ == '__main__':
    main()
