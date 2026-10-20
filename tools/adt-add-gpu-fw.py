#!/usr/bin/env python3
"""Add /sweet-gpu with the Adreno GMU firmware to an existing ADT.

  adt-add-gpu-fw.py IN.adt OUT.adt a630_gmu.bin

The firmware is Qualcomm's (from the phone's /vendor/firmware); it goes only
into locally built boot images, never into git. bsd/dev/sweet_usb.c
(sweet_gpu_gmu_boot, boot-arg sweetgpu=2) copies property gmu-fw into the
GMU ITCM/DTCM (a630 "legacy" image: raw Cortex-M3 memory, 32 KiB).
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

src, dst, fw = sys.argv[1:4]
root, _ = adt.parse(open(src, 'rb').read())
data = open(fw, 'rb').read()
if len(data) != 0x8000:
    raise SystemExit(f'{fw}: {len(data)} bytes, expected the 32 KiB a630 legacy GMU image')
root.children = [c for c in root.children
                 if dict((k, v) for k, f, v in c.props).get('name') != b'sweet-gpu\0']
n = adt.Node()
n.props = [('name', 0, b'sweet-gpu\0'), ('AAPL,phandle', 0, struct.pack('<I', 900)),
           ('gmu-fw', 0, data)]
n.children = []
root.children.append(n)
blob = adt.serialize(root)
open(dst, 'wb').write(blob)
print(f'{dst}: {len(blob)} bytes (+gmu-fw {len(data)})', file=sys.stderr)
