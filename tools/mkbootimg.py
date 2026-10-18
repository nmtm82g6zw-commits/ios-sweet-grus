#!/usr/bin/env python3
"""Self-contained Android boot image writer, header versions 0-2.

Same output as AOSP mkbootimg for the options below (byte-identical header,
id and layout), without its gki/avb imports: distribution packages of
mkbootimg (e.g. Ubuntu 24.04) fail with "No module named 'gki'", and macOS
has none. Only what shim/build.sh needs:

  mkbootimg.py --header_version N --kernel K [--ramdisk R] [--second S]
               [--recovery_dtbo D] [--dtb DTB] [--base B] [--kernel_offset O]
               [--ramdisk_offset O] [--second_offset O] [--tags_offset O]
               [--dtb_offset O] [--pagesize P] [--os_version A.B.C]
               [--os_patch_level YYYY-MM] [--cmdline S] [--board NAME] -o OUT
"""
import argparse, hashlib, re, struct, sys

BOOT_MAGIC = b'ANDROID!'
NAME_SIZE, ARGS_SIZE, EXTRA_ARGS_SIZE = 16, 512, 1024
HEADER_SIZE = {0: 1632, 1: 1648, 2: 1660}


def num(s):
    return int(s, 0)


def os_version_field(version, patch):
    a = b = c = 0
    if version:
        m = re.fullmatch(r'(\d{1,3})(?:\.(\d{1,3})(?:\.(\d{1,3}))?)?', version)
        if not m:
            raise SystemExit(f'bad --os_version {version}')
        a, b, c = (int(x or 0) for x in m.groups())
        if max(a, b, c) >= 128:
            raise SystemExit(f'bad --os_version {version}')
    y = mo = 0
    if patch:
        m = re.fullmatch(r'(\d{4})-(\d{2})(?:-\d{2})?', patch)
        if not m:
            raise SystemExit(f'bad --os_patch_level {patch}')
        y, mo = int(m.group(1)) - 2000, int(m.group(2))
        if not (0 <= y < 128 and 0 < mo <= 12):
            raise SystemExit(f'bad --os_patch_level {patch}')
    return ((a << 14) | (b << 7) | c) << 11 | (y << 4) | mo


def read(path):
    return open(path, 'rb').read() if path else b''


def pad(data, page):
    return data + b'\0' * (-len(data) % page)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--header_version', type=num, default=0)
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--ramdisk')
    ap.add_argument('--second')
    ap.add_argument('--recovery_dtbo')
    ap.add_argument('--dtb')
    ap.add_argument('--base', type=num, default=0x10000000)
    ap.add_argument('--kernel_offset', type=num, default=0x00008000)
    ap.add_argument('--ramdisk_offset', type=num, default=0x01000000)
    ap.add_argument('--second_offset', type=num, default=0x00f00000)
    ap.add_argument('--tags_offset', type=num, default=0x00000100)
    ap.add_argument('--dtb_offset', type=num, default=0x01f00000)
    ap.add_argument('--pagesize', type=num, default=2048)
    ap.add_argument('--os_version')
    ap.add_argument('--os_patch_level')
    ap.add_argument('--cmdline', default='')
    ap.add_argument('--board', default='')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args(argv)

    v = a.header_version
    if v not in HEADER_SIZE:
        raise SystemExit('only header versions 0, 1 and 2 are supported')
    if a.pagesize not in (2048, 4096, 8192, 16384):
        raise SystemExit(f'bad --pagesize {a.pagesize}')
    if v < 2 and a.dtb:
        raise SystemExit('--dtb needs --header_version 2')
    if v < 1 and a.recovery_dtbo:
        raise SystemExit('--recovery_dtbo needs --header_version 1 or 2')
    if v == 2 and not a.dtb:
        raise SystemExit('--header_version 2 needs --dtb')
    cmdline = a.cmdline.encode()
    if len(cmdline) > ARGS_SIZE + EXTRA_ARGS_SIZE - 2:
        raise SystemExit('--cmdline too long')
    board = a.board.encode()
    if len(board) >= NAME_SIZE:
        raise SystemExit('--board too long')

    kernel, ramdisk, second = read(a.kernel), read(a.ramdisk), read(a.second)
    dtbo, dtb = read(a.recovery_dtbo), read(a.dtb)

    # AOSP: sha1 over each image followed by its size (u32), size 0 if absent.
    sha = hashlib.sha1()
    images = [(kernel, a.kernel), (ramdisk, a.ramdisk), (second, a.second)]
    if v >= 1:
        images.append((dtbo, a.recovery_dtbo))
    if v >= 2:
        images.append((dtb, a.dtb))
    for data, path in images:
        if path:
            sha.update(data)
        sha.update(struct.pack('<I', len(data)))
    img_id = sha.digest()

    hdr = BOOT_MAGIC
    hdr += struct.pack('<10I', len(kernel), a.base + a.kernel_offset,
                       len(ramdisk), (a.base + a.ramdisk_offset) if ramdisk else 0,
                       len(second), (a.base + a.second_offset) if second else 0,
                       a.base + a.tags_offset, a.pagesize, v,
                       os_version_field(a.os_version, a.os_patch_level))
    hdr += board.ljust(NAME_SIZE, b'\0')
    hdr += cmdline[:ARGS_SIZE - 1].ljust(ARGS_SIZE, b'\0')
    hdr += img_id.ljust(32, b'\0')
    hdr += cmdline[ARGS_SIZE - 1:].ljust(EXTRA_ARGS_SIZE, b'\0')
    if v >= 1:
        dtbo_off = 0
        if dtbo:
            dtbo_off = a.pagesize * (1 + sum(-(-len(x) // a.pagesize) for x in (kernel, ramdisk, second)))
        hdr += struct.pack('<IQI', len(dtbo), dtbo_off, HEADER_SIZE[v])
    if v >= 2:
        hdr += struct.pack('<IQ', len(dtb), a.base + a.dtb_offset)
    assert len(hdr) == HEADER_SIZE[v], len(hdr)

    out = pad(hdr, a.pagesize)
    for data in (kernel, ramdisk, second) + ((dtbo,) if v >= 1 else ()) + ((dtb,) if v >= 2 else ()):
        out += pad(data, a.pagesize)
    open(a.output, 'wb').write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
