#!/usr/bin/env python3
"""Split a header-v0/v1 kernel (Image.gz-dtb) into the kernel and its appended DTBs.

  split-appended-dtb.py KERNEL OUTDIR

KERNEL is the 'kernel' file that unpack_bootv2.py extracts from a boot.img
whose header has no dtb section (grus: header v1, Image.gz-dtb). ABL picks
one of the appended DTBs by qcom,msm-id / qcom,board-id / qcom,pmic-id, so
the shim boot image must carry the same set: OUTDIR/dtb is all of them
concatenated, in the original order, ready for shim/build.sh (BOARD=grus).
Also writes OUTDIR/dtb.N for each tree and prints what identifies it.
"""
import os, struct, sys, zlib

FDT_MAGIC = 0xd00dfeed


def root_props(blob):
    """Root-node properties of one FDT, as {name: bytes}."""
    off_struct, off_strings = struct.unpack_from('>II', blob, 8)
    p, depth, props = off_struct, 0, {}
    while True:
        tok, = struct.unpack_from('>I', blob, p)
        p += 4
        if tok == 1:	# BEGIN_NODE
            end = blob.index(b'\0', p)
            p = (end + 4) & ~3
            depth += 1
        elif tok == 2:	# END_NODE
            depth -= 1
            if depth == 0:
                return props
        elif tok == 3:	# PROP
            ln, nameoff = struct.unpack_from('>II', blob, p)
            name = blob[off_strings + nameoff:blob.index(b'\0', off_strings + nameoff)].decode()
            if depth == 1:
                props[name] = blob[p + 8:p + 8 + ln]
            p = (p + 8 + ln + 3) & ~3
        elif tok == 4:	# NOP
            continue
        else:
            return props


def cells(v):
    return ' '.join('%#x' % c for c in struct.unpack('>%dI' % (len(v) // 4), v))


def main():
    src, out = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    os.makedirs(out, exist_ok=True)
    if data[:2] == b'\x1f\x8b':
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        image = d.decompress(data)
        rest = d.unused_data
        kernel = data[:len(data) - len(rest)]
        print(f'gzip kernel: {len(kernel)} bytes -> {len(image)} bytes')
    else:
        # Uncompressed Image: the DTBs start at the first FDT magic after the header.
        i = data.find(struct.pack('>I', FDT_MAGIC), 64)
        if i < 0:
            raise SystemExit('no appended DTB found')
        kernel, rest = data[:i], data[i:]
        print(f'raw kernel: {len(kernel)} bytes')
    open(os.path.join(out, 'kernel'), 'wb').write(kernel)
    dtbs, off = [], 0
    while off + 8 <= len(rest):
        magic, size = struct.unpack_from('>II', rest, off)
        if magic != FDT_MAGIC or size < 40 or off + size > len(rest):
            break
        dtbs.append(rest[off:off + size])
        off += size
    if not dtbs:
        raise SystemExit('no appended DTB found')
    if off != len(rest):
        print(f'warning: {len(rest) - off} trailing bytes after the last DTB')
    for n, blob in enumerate(dtbs):
        open(os.path.join(out, f'dtb.{n}'), 'wb').write(blob)
        pr = root_props(blob)
        model = pr.get('model', b'').rstrip(b'\0').decode(errors='replace')
        print(f'dtb.{n}: {len(blob)} bytes  model="{model}"')
        for k in ('qcom,msm-id', 'qcom,board-id', 'qcom,pmic-id'):
            if k in pr:
                print(f'    {k} = <{cells(pr[k])}>')
    open(os.path.join(out, 'dtb'), 'wb').write(b''.join(dtbs))
    print(f'{out}/dtb: {len(dtbs)} trees, {off} bytes')


if __name__ == '__main__':
    main()
