#!/usr/bin/env python3
"""Add /sweet-gpu with the Adreno GMU firmware to an existing ADT.

  adt-add-gpu-fw.py IN.adt OUT.adt a630_gmu.bin [--zap DIR/a615_zap]

The firmware is Qualcomm's (from the phone's /vendor/firmware); it goes only
into locally built boot images, never into git. bsd/dev/sweet_usb.c
(sweet_gpu_gmu_boot, boot-arg sweetgpu=2) copies property gmu-fw into the
GMU ITCM/DTCM (a630 "legacy" image: raw Cortex-M3 memory, 32 KiB).
--zap PREFIX adds PREFIX.mdt as zap-mdt and every PREFIX.bNN as zap-bNN
(sweet_gpu_zap, sweetgpu=5: PAS metadata + loadable segments).
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

src, dst, fw = sys.argv[1:4]
zap = sys.argv[sys.argv.index('--zap') + 1] if '--zap' in sys.argv else None
root, _ = adt.parse(open(src, 'rb').read())
data = open(fw, 'rb').read()
if len(data) != 0x8000:
    raise SystemExit(f'{fw}: {len(data)} bytes, expected the 32 KiB a630 legacy GMU image')
root.children = [c for c in root.children
                 if dict((k, v) for k, f, v in c.props).get('name') != b'sweet-gpu\0']
n = adt.Node()
n.props = [('name', 0, b'sweet-gpu\0'), ('AAPL,phandle', 0, struct.pack('<I', 900)),
           ('gmu-fw', 0, data)]
if zap:
    n.props.append(('zap-mdt', 0, open(zap + '.mdt', 'rb').read()))
    for i in range(100):
        seg = '%s.b%02d' % (zap, i)
        if not os.path.exists(seg):
            break
        n.props.append(('zap-b%02d' % i, 0, open(seg, 'rb').read()))
n.children = []
root.children.append(n)
blob = adt.serialize(root)
open(dst, 'wb').write(blob)
print(f'{dst}: {len(blob)} bytes (+gmu-fw {len(data)}' + (f', zap {len(n.props) - 4} segments' if zap else '') + ')',
      file=sys.stderr)
