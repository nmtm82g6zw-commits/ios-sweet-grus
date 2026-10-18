#!/usr/bin/env python3
"""Take the stock DTBs from a grus boot.img, whatever its header version.

  grus-stock-dtb.py backup/boot.img dtb/grus

Header v2 (dtb section, e.g. newer/custom ROMs): copies the section.
Header v0/v1 (Image.gz-dtb, original MIUI layout): splits the appended DTBs.
Writes OUT/dtb (all trees, in order), OUT/header_version (read by
shim/build.sh BOARD=grus so the shim image matches the stock layout) and
prints the model / msm-id / board-id of every tree.
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def trees(blob):
    out, off = [], 0
    while off + 8 <= len(blob) and struct.unpack_from('>I', blob, off)[0] == 0xd00dfeed:
        size = struct.unpack_from('>I', blob, off + 4)[0]
        out.append(blob[off:off + size])
        off += size
    return out, off


def main():
    img, out = sys.argv[1], sys.argv[2]
    b = open(img, 'rb').read()
    if b[:8] != b'ANDROID!':
        raise SystemExit(f'{img}: not an Android boot image')
    ks, _, rs, _, ss, _, _, ps, ver = struct.unpack_from('<9I', b, 8)
    os.makedirs(out, exist_ok=True)
    rnd = lambda n: (n + ps - 1) // ps * ps
    print(f'{img}: header v{ver}, page {ps}, kernel {ks}, ramdisk {rs}')
    if ver == 2:
        dtbo_size = struct.unpack_from('<I', b, 1632)[0]
        dtb_size = struct.unpack_from('<I', b, 1648)[0]
        off = ps + rnd(ks) + rnd(rs) + rnd(ss) + rnd(dtbo_size)
        blob = b[off:off + dtb_size]
        t, used = trees(blob)
        if not t or used != len(blob):
            raise SystemExit('dtb section is not a plain FDT list')
        data = blob
    elif ver in (0, 1):
        with tempfile.TemporaryDirectory() as tmp:
            k = os.path.join(tmp, 'kernel')
            open(k, 'wb').write(b[ps:ps + ks])
            subprocess.run([sys.executable, os.path.join(HERE, 'split-appended-dtb.py'), k, out], check=True)
            data = open(os.path.join(out, 'dtb'), 'rb').read()
        t, _ = trees(data)
    else:
        raise SystemExit(f'header v{ver} not supported (grus ships v1 or v2)')
    open(os.path.join(out, 'dtb'), 'wb').write(data)
    open(os.path.join(out, 'header_version'), 'w').write(f'{2 if ver == 2 else 1}\n')
    for n, blob in enumerate(t):
        sp = subprocess.run([sys.executable, '-c', '''
import sys, importlib.util
spec = importlib.util.spec_from_file_location("s", sys.argv[1]); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
p = m.root_props(open(sys.argv[2], "rb").read())
print("model=" + p.get("model", b"").rstrip(b"\\0").decode(errors="replace"))
for k in ("qcom,msm-id", "qcom,board-id"):
    if k in p: print("    %s = <%s>" % (k, m.cells(p[k])))
''', os.path.join(HERE, 'split-appended-dtb.py'), '/dev/stdin'], input=blob, capture_output=True)
        print(f'tree {n}: {len(blob)} bytes  ' + sp.stdout.decode().strip())
    print(f'{out}/dtb: {len(t)} trees; {out}/header_version: {2 if ver == 2 else 1}')


if __name__ == '__main__':
    main()
