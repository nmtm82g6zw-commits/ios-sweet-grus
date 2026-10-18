/*
 * Keep a DSI command-mode panel scanning the splash framebuffer (grus).
 *
 * Register layout: SDE v4 (sdm670-sde.dtsi offsets from mdss_mdp@ae00000,
 * Linux sde_hw_ctl.c / sde_hw_pingpong.c / sde_hw_sspp.c):
 *   CTL n   0x2000 + 0x200 n   TOP 0x14 (bit 17 command mode, 7:4 intf), START 0x1c
 *   PP n    0x71000 + 0x800 n  TEAR_CHECK_EN 0x00, AUTOREFRESH_CONFIG 0x30
 *                              (bit 31 enable, low bits: TE frames per refresh)
 *   SSPP    SRC_SIZE 0x00, SRC0_ADDR 0x14, SRC_YSTRIDE0 0x24, SRC_FORMAT 0x30,
 *           SRC_UNPACK_PATTERN 0x34
 *   CTL     FLUSH 0x18: VIG0 bit 0, VIG1 1, DMA0 11, DMA1 12, DMA2 24
 * The display is left running by ABL, so the MDP is powered and clocked.
 *
 * Splash format (measured on grus, 2026-10-18): ABL scans the splash as
 * RGB888, 3 bytes/pixel, stride 3240; the shim, xnu's console and
 * IOMobileFramebuffer all draw 32-bit BGRA, which shows up as fine vertical
 * stripes. panel_init() switches the pipe to XRGB8888 (stride 4320), as in
 * Linux dpu/sde formats: same B,G,R element order, plus the 4th (X)
 * element, bpp-1 = 3, unpack_count-1 = 3, C3 bits = 8. Fits the
 * 0x2300000 splash region (1080 x 2340 x 4 = 0x9a4100).
 * MMU off: every access is a Device access, as for the other shim drivers.
 */
#include <stdint.h>
#include "board.h"
#include "console.h"
#include "panel.h"

#if BOARD_CMD_PANEL
#define CTL(n)			(MDP_BASE + 0x2000UL + 0x200UL * (n))
#define CTL_TOP			0x14
#define CTL_START		0x1c
#define PP(n)			(MDP_BASE + 0x71000UL + 0x800UL * (n))
#define PP_TEAR_CHECK_EN	0x00
#define PP_AUTOREFRESH		0x30
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK		0x34
#define CTL_FLUSH		0x18
/* SRC_FORMAT fields (sde_hw_sspp.c _sspp_setup_format) */
#define FMT_C3_BITS(x)		((x) << 6)
#define FMT_BPP_MASK		(3U << 9)		/* bpp - 1 */
#define FMT_UNPACK_CNT_MASK	(3U << 12)		/* unpack_count - 1 */
#define FMT_ALPHA_EN		(1U << 8)
#define N_CTL			5
#define N_PP			4
#define N_SSPP			5

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v)
{
	__asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint32_t *)a = v;
	__asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t sspp_flush_bit(int i)
{
	switch (i) {
	case 0: return 1U << 0;		/* VIG0 */
	case 1: return 1U << 1;		/* VIG1 */
	case 2: return 1U << 11;	/* DMA0 */
	case 3: return 1U << 12;	/* DMA1 */
	default: return 1U << 24;	/* DMA2 */
	}
}

static uintptr_t sspp_base(int i)
{
	switch (i) {	/* no pointer/offset table in .data: no relocations */
	case 0: return MDP_BASE + 0x5000;	/* VIG0 */
	case 1: return MDP_BASE + 0x7000;	/* VIG1 */
	case 2: return MDP_BASE + 0x25000;	/* DMA0 */
	case 3: return MDP_BASE + 0x27000;	/* DMA1 */
	default: return MDP_BASE + 0x29000;	/* DMA2 */
	}
}

int panel_init(struct panel_info *pi)
{
	pi->ctl = pi->pp = pi->sspp = -1;
	pi->ctl_top = pi->pp_te = pi->pp_ar_before = pi->pp_ar_after = 0;
	pi->src_addr = pi->src_stride = pi->src_format = pi->src_size = 0;
	pi->fmt_before = pi->unpack_before = pi->stride_before = 0;
	pi->converted = 0;
	pi->bpp = 4;

	for (int n = 0; n < N_CTL; n++) {
		uint32_t top = rd32(CTL(n) + CTL_TOP);
		if ((top & (1U << 17)) && (top & 0xf0)) {
			pi->ctl = n;
			pi->ctl_top = top;
			break;
		}
	}
	for (int n = 0; n < N_PP; n++) {
		uint32_t te = rd32(PP(n) + PP_TEAR_CHECK_EN);
		if (te & 1) {
			pi->pp = n;
			pi->pp_te = te;
			break;
		}
	}
	for (int i = 0; i < N_SSPP; i++) {
		uintptr_t s = sspp_base(i);
		pi->pipe_addr[i] = rd32(s + SSPP_SRC0_ADDR);
		pi->pipe_fmt[i] = rd32(s + SSPP_SRC_FORMAT);
		pi->pipe_stride[i] = rd32(s + SSPP_SRC_YSTRIDE0);
	}
	for (int i = 0; i < N_SSPP; i++) {
		uintptr_t s = sspp_base(i);
		uint32_t a = rd32(s + SSPP_SRC0_ADDR);
		if (a >= (uint32_t)FB_PHYS && a < (uint32_t)(FB_PHYS + FB_SIZE)) {
			pi->sspp = i;
			pi->src_addr = rd32(s + SSPP_SRC0_ADDR);
			pi->src_stride = rd32(s + SSPP_SRC_YSTRIDE0) & 0xffff;
			pi->src_format = rd32(s + SSPP_SRC_FORMAT);
			pi->src_size = rd32(s + SSPP_SRC_SIZE);
			break;
		}
	}
	if (pi->sspp >= 0)
		pi->bpp = ((pi->src_format >> 9) & 3) + 1;
	/* The console draws in whatever the pipe now scans. */
	con_set_format(pi->bpp, pi->sspp >= 0 ? pi->src_stride : 0);
	if (pi->pp < 0)
		return -1;	/* no tear check: nothing to drive the panel with */
	pi->pp_ar_before = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	/*
	 * grus ABL already runs autorefresh (0x80000001: every TE). Rewriting
	 * it and kicking CTL_START while it runs corrupts the pipe: the panel
	 * shows a striped band and stops updating (measured 2026-10-18, diag
	 * v7). So only switch it on when it is off.
	 */
	if (!(pi->pp_ar_before & (1U << 31))) {
		wr32(PP(pi->pp) + PP_AUTOREFRESH, (1U << 31) | 1);
		if (pi->ctl >= 0)
			wr32(CTL(pi->ctl) + CTL_START, 1);
	}
	pi->pp_ar_after = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	return 0;
}

int panel_to_xrgb(struct panel_info *pi)
{
	/* RGB888 splash (3 bytes/pixel, packed rows) -> XRGB8888, rows of FB_STRIDE. */
	if (pi->sspp >= 0 && ((pi->src_format & FMT_BPP_MASK) >> 9) == 2 &&
	    pi->src_stride == FB_W * 3) {
		uintptr_t s = sspp_base(pi->sspp);
		uint32_t fmt = pi->src_format, unpack = rd32(s + SSPP_SRC_UNPACK);
		pi->fmt_before = fmt;
		pi->unpack_before = unpack;
		pi->stride_before = pi->src_stride;
		fmt &= ~(FMT_BPP_MASK | FMT_UNPACK_CNT_MASK | FMT_ALPHA_EN | FMT_C3_BITS(3U));
		fmt |= (3U << 9) | (3U << 12) | FMT_C3_BITS(3U);
		unpack = (unpack & 0x00ffffffU) | (3U << 24);	/* element[3] = C3 (X) */
		wr32(s + SSPP_SRC_YSTRIDE0, (rd32(s + SSPP_SRC_YSTRIDE0) & 0xffff0000U) | FB_STRIDE);
		wr32(s + SSPP_SRC_UNPACK, unpack);
		wr32(s + SSPP_SRC_FORMAT, fmt);
		if (pi->ctl >= 0)
			wr32(CTL(pi->ctl) + CTL_FLUSH, sspp_flush_bit(pi->sspp));
		if (pi->ctl >= 0)
			wr32(CTL(pi->ctl) + CTL_START, 1);
		pi->src_format = rd32(s + SSPP_SRC_FORMAT);
		pi->src_stride = rd32(s + SSPP_SRC_YSTRIDE0) & 0xffff;
		pi->converted = 1;
	}
	if (!pi->converted)
		return -1;
	pi->bpp = 4;
	con_set_format(4, FB_STRIDE);
	return 0;
}

static void dump_block(const char *label, uintptr_t base, uint32_t first, uint32_t count)
{
	for (uint32_t i = 0; i < count; i += 2) {
		char a[] = "+000=", b[] = "+000=";
		static const char hx[] = "0123456789ABCDEF";
		uint32_t o0 = first + 4 * i, o1 = o0 + 4;
		a[1] = hx[(o0 >> 8) & 15]; a[2] = hx[(o0 >> 4) & 15]; a[3] = hx[o0 & 15];
		b[1] = hx[(o1 >> 8) & 15]; b[2] = hx[(o1 >> 4) & 15]; b[3] = hx[o1 & 15];
		con_hex2(label, a, rd32(base + o0), i + 1 < count ? b : 0, i + 1 < count ? rd32(base + o1) : 0);
	}
}

void panel_dump(void)
{
	con_hex2("MDSS", "hwver=", rd32(MDP_BASE + 0x0), "irq=", rd32(MDP_BASE + 0x10));
	dump_block("TOP", MDP_BASE + 0x1000, 0, 16);		/* sde-off 0x1000 */
	for (int n = 0; n < N_CTL; n++) {
		char l[] = "CTL0";
		l[3] = '0' + n;
		dump_block(l, CTL(n), 0, 12);
	}
	for (int n = 0; n < N_PP; n++) {
		char l[] = "PP0";
		l[2] = '0' + n;
		dump_block(l, PP(n), 0, 16);
	}
	for (int i = 0; i < N_SSPP; i++) {
		char l[] = "SSPP0";
		l[4] = '0' + i;
		dump_block(l, sspp_base(i), 0, 16);		/* size, xy, out, addr, stride, fmt, unpack */
	}
	for (int n = 0; n < 2; n++) {
		char l[] = "LM0";
		l[2] = '0' + n;
		dump_block(l, MDP_BASE + 0x45000 + 0x1000 * n, 0, 8);
	}
	for (int n = 0; n < 4; n++) {
		char l[] = "INTF0";
		l[4] = '0' + n;
		dump_block(l, MDP_BASE + 0x6b000 + 0x800 * n, 0, 4);
	}
	dump_block("DSI0", 0x0ae94000UL, 0, 16);
	/* What ABL left in the splash buffer: first 32 bytes and the middle row. */
	dump_block("FB", FB_PHYS, 0, 8);
	dump_block("FBmid", FB_PHYS + (uintptr_t)FB_W * 3 * (FB_H / 2), 0, 8);
}

#define SMMU_BASE	0x15000000UL
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }

/* AArch64 stage-1 walk, 4K granule; returns PA or 0, logs every level. */
static uint64_t smmu_walk(uint64_t ttbr, uint32_t tcr, uint64_t iova)
{
	uint32_t t0sz = tcr & 0x3f, tg0 = (tcr >> 14) & 3;
	if (tg0 != 0) {
		con_hex2("WALK", "tg0=", tg0, "unsupported", 0);
		return 0;
	}
	int va_bits = 64 - (int)t0sz;
	int level = 4 - (va_bits - 12 + 8) / 9;
	uint64_t table = ttbr & 0x0000fffffffff000ULL;
	for (; level <= 3; level++) {
		int shift = 12 + 9 * (3 - level);
		uint64_t idx = (iova >> shift) & 0x1ff;
		uint64_t d = rd64(table + idx * 8);
		char l[] = "WALK L0";
		l[6] = '0' + level;
		con_hex2(l, "hi=", (uint32_t)(d >> 32), "lo=", (uint32_t)d);
		if (!(d & 1))
			return 0;
		uint64_t oa = d & 0x0000fffffffff000ULL;
		if (level == 3 || !(d & 2))	/* page, or block at L1/L2 */
			return (oa & ~((1ULL << shift) - 1)) | (iova & ((1ULL << shift) - 1));
		table = oa;
	}
	return 0;
}

static void spin_ms(unsigned ms)
{
	uint64_t f, t0, t;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t0));
	do
		__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
	while (t - t0 < f / 1000 * ms);
}

void panel_sample(int n, unsigned ms)
{
	for (int i = 0; i < n; i++) {
		con_hex2("SAMPLE", "intr=", rd32(MDP_BASE + 0x1014), "ppline=", rd32(PP(0) + 0x2c));
		con_hex2("", "ppcnt=", rd32(PP(0) + 0x14), "dsififo=", rd32(0x0ae94000UL + 0xc));
		if (i + 1 < n)
			spin_ms(ms);
	}
}

uint64_t smmu_dump(void)
{
	uint32_t idr0 = rd32(SMMU_BASE + 0x20), idr1 = rd32(SMMU_BASE + 0x24);
	uint32_t pgsz = (idr1 & (1U << 31)) ? 0x10000 : 0x1000;
	uint32_t numpage = 1U << (((idr1 >> 28) & 7) + 1);
	uint32_t nsmr = idr0 & 0xff, ncb = idr1 & 0xff;
	uint64_t pa = 0;

	con_hex2("SMMU", "cr0=", rd32(SMMU_BASE + 0x0), "idr0=", idr0);
	con_hex2("SMMU", "idr1=", idr1, "idr2=", rd32(SMMU_BASE + 0x28));
	con_hex2("SMMU", "nsmr=", nsmr, "ncb=", ncb);
	for (uint32_t n = 0; n < nsmr && n < 256; n++) {
		uint32_t smr = rd32(SMMU_BASE + 0x800 + 4 * n);
		uint32_t id = smr & 0x7fff, mask = (smr >> 16) & 0x7fff;
		if (!(smr & (1U << 31)))
			continue;
		/* MDP stream IDs 0x880-0x888 / 0xc80-0xc88 */
		if (((0x880 ^ id) & ~mask & 0x7ff0) && ((0xc80 ^ id) & ~mask & 0x7ff0))
			continue;
		uint32_t s2cr = rd32(SMMU_BASE + 0xc00 + 4 * n), cb = s2cr & 0xff;
		con_hex2("SMR", "n=", n, "smr=", smr);
		con_hex2(" S2CR", "v=", s2cr, "type=", (s2cr >> 16) & 3);
		if (((s2cr >> 16) & 3) != 0 || cb >= ncb)
			continue;
		uintptr_t gr1 = SMMU_BASE + pgsz, cbb = SMMU_BASE + (uintptr_t)numpage * pgsz + (uintptr_t)cb * pgsz;
		con_hex2(" CBAR", "v=", rd32(gr1 + 4 * cb), "cba2r=", rd32(gr1 + 0x800 + 4 * cb));
		uint32_t sctlr = rd32(cbb + 0x0), tcr = rd32(cbb + 0x30);
		uint64_t ttbr0 = rd64(cbb + 0x20);
		con_hex2(" CB", "n=", cb, "sctlr=", sctlr);
		con_hex2(" CB", "tcr=", tcr, "tcr2=", rd32(cbb + 0x10));
		con_hex2(" TTBR0", "hi=", (uint32_t)(ttbr0 >> 32), "lo=", (uint32_t)ttbr0);
		con_hex2(" MAIR", "0=", rd32(cbb + 0x38), "1=", rd32(cbb + 0x3c));
		con_hex2(" FSR", "v=", rd32(cbb + 0x58), "far=", (uint32_t)rd64(cbb + 0x60));
		if (!(sctlr & 1)) {
			con_str(" CB", "translation off");
			continue;
		}
		static const uint32_t probe[3] = {0, 0x500000, 0x9a0000};
		for (int k = 0; k < 3; k++) {
			uint64_t va = FB_PHYS + probe[k];
			uint64_t p = smmu_walk(ttbr0, tcr, va);
			con_hex2(" IOVA", "va=", (uint32_t)va, "pa=", (uint32_t)p);
			con_hex2("", "pa_hi=", (uint32_t)(p >> 32), 0, 0);
			if (k == 0 && p && !pa)
				pa = p;
		}
	}
	return pa;
}

void panel_report_short(const struct panel_info *pi)
{
	con_hex2("CTL", "n=", (uint32_t)pi->ctl, "top=", pi->ctl_top);
	con_hex2("PP", "n=", (uint32_t)pi->pp, "ar=", pi->pp_ar_after);
	for (int i = 0; i < N_SSPP; i++) {
		char l[] = "P0";
		l[1] = '0' + i;
		con_hex2(l, "a=", pi->pipe_addr[i], "f=", pi->pipe_fmt[i]);
		con_hex2("", "st=", pi->pipe_stride[i], "sz=", rd32(sspp_base(i) + SSPP_SRC_SIZE));
	}
}

void panel_report(const struct panel_info *pi)
{
	con_hex2("PANEL", "ctl=", (uint32_t)pi->ctl, "top=", pi->ctl_top);
	con_hex2(" pp", "pp=", (uint32_t)pi->pp, "te=", pi->pp_te);
	con_hex2(" autoref", "was=", pi->pp_ar_before, "now=", pi->pp_ar_after);
	con_hex2(" sspp", "n=", (uint32_t)pi->sspp, "addr=", pi->src_addr);
	con_hex2(" sspp", "stride=", pi->src_stride, "fmt=", pi->src_format);
	con_hex2(" sspp", "size=", pi->src_size, "want=", FB_STRIDE);
	for (int i = 0; i < N_SSPP; i++)
		if (pi->pipe_addr[i]) {
			char l[] = " pipeN";
			l[5] = '0' + i;
			con_hex2(l, "a=", pi->pipe_addr[i], "f=", pi->pipe_fmt[i]);
			con_hex2("      ", "st=", pi->pipe_stride[i], 0, 0);
		}
	con_hex2(" scan", "bpp=", (uint32_t)pi->bpp, "conv=", (uint32_t)pi->converted);
	con_color_bars();
	if (pi->converted) {
		con_hex2(" rgb888", "fmt=", pi->fmt_before, "unpack=", pi->unpack_before);
		con_hex2(" ->xrgb", "stride=", pi->stride_before, "now=", pi->src_stride);
	}
	if (pi->sspp >= 0 && pi->src_stride != FB_STRIDE)
		con_str("PANEL", "WARNING: splash is not 32bpp 1080 wide");
}
#else
int panel_init(struct panel_info *pi)
{
	pi->ctl = pi->pp = pi->sspp = -1;
	pi->bpp = 4;
	con_set_format(4, FB_STRIDE);
	return 0;
}

int panel_to_xrgb(struct panel_info *pi)
{
	(void)pi;
	return 0;
}

void panel_report(const struct panel_info *pi)
{
	(void)pi;
}

void panel_report_short(const struct panel_info *pi)
{
	(void)pi;
}

void panel_dump(void)
{
}

uint64_t smmu_dump(void)
{
	return 0;
}

void panel_sample(int n, unsigned ms)
{
	(void)n;
	(void)ms;
}
#endif
