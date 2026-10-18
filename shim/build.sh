#!/bin/bash
# Build the shim and wrap it in a boot.img like the stock one:
#   BOARD=sweet (default): header v2 with the stock dtb section (PixelOS header)
#   BOARD=grus:            header v1, Image.gz with the stock DTBs appended
#                          (Mi 9 SE stock layout; ../dtb/grus/dtb, see
#                          tools/split-appended-dtb.py; DTB=... overrides)
set -euo pipefail
cd "$(dirname "$0")"
BOARD=${BOARD:-sweet}
PY=${PYTHON:-python3}
# -fno-jump-tables: no switch tables of absolute pointers (clang turns the
# panic-reason switch into one), the image is never relocated.
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -fno-jump-tables -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
# Toolchain: CROSS=aarch64-linux-gnu- (GNU, default on Linux) or LLVM
# (clang + ld.lld, e.g. Homebrew llvm + lld on macOS) when no GNU cross gcc.
CROSS=${CROSS:-aarch64-linux-gnu-}
if command -v "${CROSS}gcc" >/dev/null 2>&1; then
	CC="${CROSS}gcc"; LDFLAGS_X="-Wl,--no-warn-rwx-segments"
	OBJCOPY="${CROSS}objcopy"; READELF="${CROSS}readelf"; NM="${CROSS}nm"
elif command -v clang >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1; then
	CC="clang --target=aarch64-none-elf"; LDFLAGS_X="-fuse-ld=lld"
	tool() { command -v "llvm-$1" || command -v "$1"; }
	OBJCOPY=$(tool objcopy); READELF=$(tool readelf); NM=$(tool nm)
else
	echo "error: no ${CROSS}gcc and no clang + ld.lld (Linux: apt install gcc-aarch64-linux-gnu;" \
	     "macOS: brew install llvm lld, then PATH=\$(brew --prefix llvm)/bin:\$PATH)" >&2
	exit 1
fi
case "$BOARD" in
sweet) DTB=${DTB:-../dtb/boot_unpacked/dtb} ;;
grus) DTB=${DTB:-../dtb/grus/dtb}; CFLAGS="$CFLAGS -DBOARD_GRUS" ;;
*) echo "error: unknown BOARD=$BOARD (sweet, grus)" >&2; exit 1 ;;
esac
OBJS="shim console mmu fdt loader ufs panel"
rm -rf out/*.o
mkdir -p out
$CC $CFLAGS -c start.S -o out/start.o
for f in $OBJS; do
	$CC $CFLAGS -c $f.c -o out/$f.o
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$(cd out && $READELF -r $(printf '%s.o ' $OBJS) | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none $LDFLAGS_X -Wl,-N \
	out/start.o $(printf 'out/%s.o ' $OBJS) -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
# (No gawk strtonum or GNU truncate: works with mawk/BSD tools too.)
end=$($NM out/shim.elf | awk '$3=="_end"{print $1}')
[ -n "$end" ] || { echo "error: no _end symbol in shim.elf" >&2; exit 1; }
$PY - out/Image "$end" <<'PAD'
import sys
path, end = sys.argv[1], int(sys.argv[2], 16)
data = open(path, 'rb').read()
assert len(data) <= end, (len(data), end)
open(path, 'wb').write(data + bytes(end - len(data)))
PAD
# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio (ABL dislikes a zero-size ramdisk on some builds) -> diagnostics.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	# Empty newc archive (just TRAILER!!!), written without cpio.
	$PY - out/ramdisk.bin <<'CPIO'
import gzip, sys
name = b'TRAILER!!!\0'
hdr = b'070701' + b''.join(b'%08X' % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0))
rec = hdr + name
rec += b'\0' * (-len(rec) % 4)
rec += b'\0' * (-len(rec) % 512)
open(sys.argv[1], 'wb').write(gzip.compress(rec, 9, mtime=0))
CPIO
fi
[ -s "$DTB" ] || { echo "error: stock DTB $DTB missing (see README, porting to grus)" >&2; exit 1; }
if [ "$BOARD" = grus ]; then
	# ABL (header v1) finds the DTBs after the gzip stream, as in the stock
	# Image.gz-dtb; it decompresses the padded Image, so .bss/stacks exist.
	gzip -9 -n -c out/Image > out/Image.gz
	cat out/Image.gz "$DTB" > out/Image.gz-dtb
	$PY ../tools/mkbootimg.py --header_version 1 --kernel out/Image.gz-dtb --ramdisk out/ramdisk.bin \
		--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--tags_offset 0x100 --pagesize 4096 \
		--os_version 17.0.0 --os_patch_level 2035-06 \
		--cmdline "console=null" -o out/shim-boot.img
else
	$PY ../tools/mkbootimg.py --header_version 2 --kernel out/Image --ramdisk out/ramdisk.bin \
		--dtb "$DTB" --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
		--os_version 17.0.0 --os_patch_level 2035-06 \
		--cmdline "console=null" -o out/shim-boot.img
fi
# Host render of the console layout.
cc -O1 -Wall $([ "$BOARD" = grus ] && echo -DBOARD_GRUS) -o out/host_test console.c host_test.c && ./out/host_test
ls -l out/Image out/shim-boot.img
