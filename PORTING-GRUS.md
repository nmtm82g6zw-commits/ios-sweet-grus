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
| Touch orientation | as is | boot-arg `sweettouchorient` (bit 0 mirrors x, bit 1 mirrors y; default none). The DT x2x/y2y (both mirrored) was inverted on a real grus | `sweet_touch.c` |
| USB GCC CBCRs | master 0xf010 ... | master 0xf00c, sleep 0xf010, utmi 0xf014, clkref 0x8c008 | `sweet_usb.c` |
| Battery | PM6150 FG readout (opt-in) | PM660 FG gen3 monotonic SOC, VBATT/IBATT, SMB2 charger state (opt-in `sweetbattery=1`, patch 0080), 3070 mAh | `SweetBattery.cpp` |

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

## Measured on the phone (2026-10-18)

First bring-up on a real Mi 9 SE (6 GB, ROM with an Android 11 boot.img):

- **Boot image:** the stock `boot.img` is **header v2** (Image.gz, empty
  ramdisk, 3 SoC DTBs in the dtb section, grus overlays in `dtbo`), not the
  v1 Image.gz-dtb of the original MIUI tree. `tools/grus-stock-dtb.py`
  handles both and records the version; `BOARD=grus shim/build.sh` follows it.
- **RAM:** live `/memory` = `<0x80000000 2G>, <0x1_00000000 0xfd6c0000>`, no
  static carve-out in the upper bank; `mkadt.py --board grus --highmem` now
  uses the whole upper bank by default.
- **Display:** ABL leaves SSPP0 (VIG0) scanning **ARGB8888 at 0x9c000000,
  stride 4320**, CTL0 in command mode on DSI0 (INTF1), PP0 tear check on and
  **autorefresh already enabled** (`0x80000001`), apps-SMMU context for the
  MDP with translation off (IOVA = PA). Writing into the buffer is enough;
  touching the running autorefresh (rewrite + `CTL_START`) corrupts the pipe
  (striped band, panel stops updating), so `panel_init()` only enables it
  when it is off. Verified with full-screen red/green/blue bands.
- **Hardware in the live tree:** EA8076 command-mode panel active
  (`dsi_ss_ea8076_fhd_cmd_display`, no DSC, 1 LM / 1 INTF), Goodix
  `normandy-i2c@5d` on `i2c@a84000` (reset 5, irq 125, L8B/L13A, x2x/y2y),
  volume up PM660L GPIO7: all as assumed by patch 0079.
- **Reset:** `reset()` lands in Android, not fastboot: grus ABL takes the
  reboot reason from the PMIC, not from IMEM (to do).

### Diagnostics without a screen

Booted without a payload, the grus shim writes its report to the first
64 KiB of the `logdump` partition (unused crash-dump area, LUN 0) after
every stage: MDP/CTL/PP/SSPP/DSI and SMMU registers, splash contents, RAM.
`run/04-readdiag.sh` (outside the repo) or simply
`adb exec-out dd if=/dev/block/bootdevice/by-name/logdump bs=4096 count=16`
from recovery reads it back (`SWEETDIAG1` header, then text).

### First kernel boots (2026-10-18, CI run 39153408958, read-only)

Kernel `board=grus` + `mkadt.py --board grus --highmem`, cmdline without
`ufsrw`/`rootrw` and with `sweetlog=0`, so nothing is written:

- k1: XNU runs through IOKit up to `AppleCLCD 1080x2340 stride 4320 at
  0x9c000000, presented as 1125x2436`, then stops.
- k2 (`io=0x6`): no `SweetCPU` ever probes: its personality is
  `IONameMatch 'qcom,kryo470'` and the grus ADT said `qcom,kryo360`, so BSD
  init waits forever in `ml_wait_max_cpus()`. Fixed in `mkadt.py` (grus CPUs
  carry both names).
- k3: `SWEET UFS: "userdata" on LUN 0 ... 53063 MiB, read-only`, keys and USB
  probes, `BSD root: ufsroot0`, `unknown Volume Signature` /
  `cannot mount root, errno = 19` with retries (expected: userdata still
  holds Android), skywalk self tests pass. **Touch:** RPMh votes for L13A/L8B
  complete, GT9886 answers at 0x5d, PID at 0x4535 reads `9886`, coordinate
  block at 0x4100 reads, `selected GT9886 normandy, 1080x2340`.
- Open: ~10 s after the touch driver starts polling, the console turns into
  smeared vertical columns (text still lands in some rows). Not yet known
  whether it is the console scroll on the 2340-row panel or a side effect of
  the touch bring-up; irrelevant once the GUI runs with `sweetfb_noconsole=1`,
  but to be checked (e.g. a boot with `sweettouch=0`).

## Still unverified

- GT9886 touch *reports* at `0x4100` (the block reads fine; real touches
  not yet seen through HID).
- PMIC GPIO7 of PM660L configured as an input by firmware (the driver checks
  and withholds volume up otherwise; it never reconfigures the PMIC).
- Everything the kernel does (no grus kernel boot yet).

## Restart and power off (patch 0080)

`SweetPlatformExpert::haltRestart` sends PSCI SYSTEM_RESET / SYSTEM_OFF, as
mainline Linux does on Qualcomm. Before, the platform had no
`PE_halt_restart`, so Restart / Power off from iOS could not complete. ABL
decides the boot mode from the PMIC PON reason, which only Android and
fastboot set, so a plain reset boots the `boot` partition. Panics are not
turned into resets: the panic text stays on the screen.

## Measured on a real grus (2026-10-19)

- iOS root on userdata (51 GiB HFS+, built on macOS with `run/09-rootimg.sh`,
  flashed as an Android sparse image), boots to SpringBoard; first boot after
  a new image is several minutes on the Apple logo.
- `cpus=6` and `cpus=8` (six A55 + two A75) both reach the home screen.
- Touch: `sweettouchorient=0` (no mirroring) is correct.
- Battery (0080 + 0081, `sweetbattery=1`): real percentage and charger
  plug/unplug shown by iOS.
- Untethered: the shim image flashed to `boot` starts iOS on power-on.
- USB Ethernet (0085 + `sweetusb=2`): grus ABL leaves the QUSB2 PHY powered
  down (PWR_CTRL1 0x23); after clearing POWR_DOWN (PLL_STATUS 0x17, core
  ready) the Mac enumerates `ios-sweet "Sweet iOS (USB Ethernet)"` as a
  CDC-ECM interface, ping 192.168.7.2 answers in ~2 ms with 0 % loss, and the
  live kernel log arrives on UDP 5140 (`run/18-usbnet-mac.sh` on macOS).
- CPU clocks (0089 probe, 0090 cpufreq-hw): ABL leaves the A55 cluster at
  1516 MHz and the two A75 at 299 MHz (the 300 MHz GPLL0 row). Selecting the
  top non-turbo LUT row at boot gives 1708 MHz / 2303 MHz, measured with the
  PMU cycle counter; the GUI is visibly smoother.
- Touch: polling GT9886 without waiting for its IRQ reads frames while the
  controller rewrites them (torn reads, checksum errors); `sweettouchirq=1`
  cuts the errors by ~18x. The framebuffer copy costs ~5 % of one CPU at
  ~10 frames/s, so it is not the bottleneck.

## Adreno 616 bring-up (patches 0091-0103, 2026-10-20)

Boot-arg `sweetgpu=N` (each level includes the previous ones); levels 2+
run from a thread 45 s after boot so the live USB log (`sweetusb=2`,
`run/18-usbnet-mac.sh`) catches a firmware reset. Firmware (Qualcomm,
from the phone's /vendor/firmware, never committed) goes into a locally
built ADT: `tools/adt-add-gpu-fw.py IN.adt OUT.adt a630_gmu.bin
--zap a615_zap --sqe a630_sqe.fw`.

| Level | What | Result on the phone |
|---|---|---|
| probe | GCC/GPU_CC state as ABL leaves it | GPU fully off (CX/GX GDSCs collapsed) |
| 1 | CX GDSC, AXI/memnoc, cx_gmu clock, GMU ITCM test | OK |
| 2 | GMU clock 200 MHz, RSCC/PDC init, legacy a630 GMU firmware | `0xbabeface` in 0.1 ms |
| 3 | GPU SMMU context bank 0 (iova 0x60000000), HFI queues, MSG_INIT, FW_VERSION | GMU fw 0x10070000 |
| 4 | cmd-db ARC votes, BOOT_SLUMBER + SPTPRAC, perf/bw/test HFI, OOB GPU_SET | GX GDSC on, RBBM/CP_SCRATCH OK |
| 5 | zap shader: SCM PAS 13 into gpu_region 0x96015000 | AUTH_AND_RESET ret 0 |
| 6 | hw_init subset, SQE, 4 KiB ring, CP_ME_INIT, SET_SECURE_MODE 0, CP_MEM_WRITE | **0x00c0ffee written by the GPU** |

Findings worth keeping:
- The GPU SMMU (0x05040000) global space is trapped and emulated by
  Qualcomm's hypervisor: accesses must be plain single-register ldr/str
  (a post-indexed load hangs the CPU, ISV=0), and an SMR with a mask was
  silently dropped; one SMR per stream ID works (GMU 4/5, GPU 0/1).
- An unmatched GMU stream makes the firmware reset the whole phone.
- cmd-db is at 0x85fe0000; BCM addresses SH0 0x5003c, MC0 0x50000,
  ACV 0x5000c (same as Linux's a618 table), CN0 0x500a8.
- The zap region on grus is 0x96015000 (stock DTB), not 0x95c15000.
- SCM standard calls come back with QCOM_SCM_INTERRUPTED (1) during
  PAS_INIT_IMAGE: resume with x0 = 1 and the returned x6 (A6 quirk).
