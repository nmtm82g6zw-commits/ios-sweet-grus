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
	if (pi->pp < 0) {
		if (pi->ctl >= 0)
			wr32(CTL(pi->ctl) + CTL_START, 1);
		return -1;	/* no tear check: autorefresh would never fire */
	}
	pi->pp_ar_before = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	/* Every TE (frame count 1), like the Linux SDE autorefresh path. */
	wr32(PP(pi->pp) + PP_AUTOREFRESH, (1U << 31) | 1);
	pi->pp_ar_after = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	/* Autorefresh starts with the next kickoff. */
	if (pi->ctl >= 0)
		wr32(CTL(pi->ctl) + CTL_START, 1);
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
#endif
