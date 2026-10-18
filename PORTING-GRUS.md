# Port to the Xiaomi Mi 9 SE (`grus`)

Status (2026-10-18): **written, not yet run on a grus**. The shim, the ADT
generator and kernel patch `0079-grus-board.patch` build for both boards; sweet
output is unchanged (byte-identical ADT, same kernel code paths). Every grus
hardware value below comes from Xiaomi's `grus-p-oss` kernel tree
(`MiCode/Xiaomi_Kernel_OpenSource`), not from a live phone. Expect the first
boots to be bring-up boots.

## What differs from sweet

| | sweet (SM7150) | grus (SDM712, sdm670 family) | Where handled |
| --- | --- | --- | --- |
| Boot image | header v2, separate dtb | **header v1, Image.gz + appended DTBs** | `shim/build.sh` `BOARD=grus` |
| Panel | 1080x2400 video mode | **1080x2340 Samsung EA8074/EA8076, DSI command mode** | `shim/panel.c` (autorefresh), `SweetFramebuffer.cpp` |
| Splash | 0x9c000000, 0x1700000 | 0x9c000000, 0x2300000 | `shim/board.h`, `mkadt.py` |
| IMEM restart reason | 0x146aa65c | 0x146bf65c | `shim/board.h` |
| GIC, timer, PSHOLD, UFS host | | same (0x17a00000/0x17a60000, PPI 3, 0xc264000, 0x1d84000) | unchanged |
| CPUs | 6x A55 + 2x A76 | 6x A55 (Kryo 360 Silver, 1.7 GHz) + 2x A75 (Kryo 360 Gold, 2.3 GHz), MPIDR 0x000..0x700 like sweet | unchanged: upstream 0078 `cpus=8` covers both |
| RAM | upper bank 0xfcc00000 | 6 GiB, upper bank size **unverified** | `mkadt.py --mem`, shim check |
| Volume up | PM6150L GPIO2, SID 4 | **PM660L GPIO7, SID 2** (PPID 0x2c6) | `sweet_keys.c` |
| Power / vol down | PM6150 PON SID 0 | PM660 PON SID 0, same registers | unchanged |
| SPMI arbiter | 0xc440000 / 0xe600000 / 0xc40a000 | same | unchanged |
| RPMh apps RSC | 0x18220000 | **0x179e0000** (TCS +0xd00, DRV 2, same TCS layout) | `sweet_rpmh.c` |
| Touch | GT9896 Yellowstone @0x5d, SE 0xa84000 | **GT9886 normandy** @0x5d, same SE 0xa84000, same GCC clocks | `sweet_touch.c` |
| Touch pins | reset 8, irq 9, i2c 6/7, power gpio 90 | reset **5**, irq **125**, i2c 6/7 (qup9, mux 1), no power gpio | `sweet_touch.c` |
| Touch rails | L11C 3.2 V | **L8B 3.3 V** (vtouch) + **L13A 1.8 V** (iotouch) | `sweet_touch.c`, `sweet_rpmh.c` |
| Touch orientation | as is | x2x + y2y (rotated 180) | `sweet_touch.c` |
| USB GCC CBCRs | master 0xf010 ... | master 0xf00c, sleep 0xf010, utmi 0xf014, clkref 0x8c008 | `sweet_usb.c` |
| Battery | PM6150 FG readout (opt-in) | static only (PM660 FG gen3 + BQ2591x) | `SweetBattery.cpp` |

### Command-mode panel

On sweet the panel scans the splash buffer by itself. grus has a command-mode
AMOLED: the MDP sends a frame only on a kickoff, so after ABL hands over
nothing new would ever appear. `shim/panel.c` finds the command-mode CTL
(CTL_TOP bit 17), the ping-pong with tear check enabled, and turns on
ping-pong **autorefresh** (`PP_AUTOREFRESH_CONFIG = BIT(31) | 1`) followed by one
`CTL_START`, so the MDP resends the buffer on every TE, like video mode. The shim
screen shows `PANEL ctl/pp/autoref/sspp` lines; `sspp stride` must be `0x10e0`
(1080 x 4, 32 bpp). If autorefresh does not keep the screen updating, boot with
`sweetfb_kick=1`: the kernel turns autorefresh off and issues `CTL_START` after
every copy (console text is then only visible once the GUI presents).

## Build

Kernel (CI, same workflow):

```sh
gh workflow run xnu.yml -R YOU/ios-sweet -f board=grus
gh run download RUN_ID -R YOU/ios-sweet -n xnu-kernel-grus -D xnu/out-grus
```

`board=grus` sets `SWEET_BOARD_FLAGS=-DSWEET_BOARD_GRUS=1`, which patch 0079 adds
to `MACHINE_FLAGS_ARM64_SWEET`. `out/BOARD` in the artifact records it.

Host tools: `aarch64-linux-gnu-gcc` or `clang` + `ld.lld` (macOS:
`brew install llvm lld` and put `$(brew --prefix llvm)/bin` first in `PATH`),
Python 3, gzip. `shim/build.sh` writes the boot image with `tools/mkbootimg.py`
(byte-identical to AOSP mkbootimg for header v1/v2, no `gki` module needed)
and needs neither gawk, cpio nor GNU truncate.

Stock DTBs (ABL picks one by msm-id/board-id/pmic-id, so carry all of them):

```sh
python3 tools/unpack_bootv2.py backup/boot.img dtb/grus_boot      # header v1
python3 tools/split-appended-dtb.py dtb/grus_boot/kernel dtb/grus  # -> dtb/grus/dtb
```

RAM: boot the shim once **without** a payload (`BOARD=grus shim/build.sh`, then
`fastboot boot shim/out/shim-boot.img`): it prints `MEMORY page= pages=` lines
(4 KiB pages) and the panel state, then probes UFS and resets after ~3 minutes.
Or read the live tree from recovery:

```sh
adb exec-out cat /sys/firmware/fdt > dtb/grus-live.dtb
python3 tools/fdt-ram.py dtb/grus-live.dtb     # prints a --mem suggestion
```

Then the phone image:

```sh
python3 tools/mkadt.py shim/out/grus.adt --board grus --highmem --mem 0x100000000:SIZE
python3 tools/mkpayload.py xnu/out-grus/kernel.development.sweet shim/out/grus.adt shim/out/payload-grus.bin \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=30'
BOARD=grus shim/build.sh ../shim/out/payload-grus.bin
```

Without `--mem`, `--board grus --highmem` uses a conservative 3 GiB from
0x100000000. The shim refuses to boot (`kernel RAM not in /memory` /
`overlaps reserved-memory`) rather than write to RAM the phone does not have.

The iOS root image and its flashing are the same as for sweet (README steps
2 and 4). The UFS driver looks for `userdata` in the GPT of LUN 0, as on sweet;
on grus that is expected but unverified (the shim's UFS probe and the kernel's
`SWEET UFS:` line show it). The log ring tools work unchanged.

## Suggested first boots

1. Shim without payload: red screen updates at all (autorefresh works), PANEL
   lines, MEMORY lines, UFS probe finds `userdata`.
2. `cpus=1`, no `sweetfb_noconsole`: kernel console to the Apple logo; pull the
   log ring from OrangeFox (`tools/sweet-log-pull.sh`).
3. Look for `SWEET FB: MDP ...`, `SWEET KEYS: board=grus ...`,
   `SWEET RPMH: rsc=0x179e0000 ...`, `SWEET TOUCH: probe 0x5d ...` in the log.
4. Then `cpus=6`, `sweetfb_noconsole=1`.

## Unverified assumptions (check these first)

- ABL's splash format on grus is 32 bpp, stride 4320 (shim `sspp stride`).
- The tear check is enabled by ABL, so autorefresh has a TE to run on.
- GT9886 coordinates are at 0x4100 (from the vendor tools and test code; the
  real address is in the vendor `goodix_gt9886_cfg_f2.bin`). The PID dump
  at 0x4535 is only logged.
- The 6 GiB part's upper bank size (use the shim or `fdt-ram.py`).
- PMIC GPIO7 of PM660L is configured as an input by firmware (the driver
  checks and withholds volume up otherwise; it never reconfigures the PMIC).
