#!/usr/bin/env python3
"""Raw image -> Android sparse image (like AOSP img2simg), no dependencies.

  img2simg.py RAW OUT.simg [--dont-care]

All-zero 4 KiB blocks become FILL chunks (value 0) so the bootloader still
writes zeros there; with --dont-care they are skipped and keep old data.
fastboot treats a sparse input as such: it is resparsed to the download size
and never read whole (a raw image larger than RAM fails in fastboot's AVB
footer copy with "Failed reading from <partition>").
"""
import struct, sys

BLK = 4096
MAX_RAW = 16384            # blocks per RAW chunk (64 MiB)
RAW, FILL, DONT_CARE = 0xCAC1, 0xCAC2, 0xCAC3
ZERO = bytes(BLK)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    dont_care = '--dont-care' in sys.argv[3:]
    chunks = 0
    with open(src, 'rb') as f, open(dst, 'wb') as o:
        o.write(bytes(28))                     # header, rewritten at the end
        total = 0
        run_kind, run_len, run_data = None, 0, []

        def flush():
            nonlocal chunks, run_kind, run_len, run_data
            if not run_len:
                return
            if run_kind == 'raw':
                data = b''.join(run_data)
                o.write(struct.pack('<HHII', RAW, 0, run_len, 12 + len(data)))
                o.write(data)
            elif dont_care:
                o.write(struct.pack('<HHII', DONT_CARE, 0, run_len, 12))
            else:
                o.write(struct.pack('<HHII', FILL, 0, run_len, 16) + bytes(4))
            chunks += 1
            run_kind, run_len, run_data = None, 0, []

        while True:
            buf = f.read(BLK * 8192)
            if not buf:
                break
            if len(buf) % BLK:
                buf += bytes(BLK - len(buf) % BLK)
            for i in range(0, len(buf), BLK):
                b = buf[i:i + BLK]
                kind = 'zero' if b == ZERO else 'raw'
                if kind != run_kind or (kind == 'raw' and run_len >= MAX_RAW):
                    flush()
                    run_kind = kind
                run_len += 1
                if kind == 'raw':
                    run_data.append(b)
                total += 1
        flush()
        o.seek(0)
        o.write(struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total, chunks, 0))
    print(f'{dst}: {total} blocks ({total * BLK} bytes), {chunks} chunks')


if __name__ == '__main__':
    main()
