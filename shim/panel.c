/*
 * Keep a DSI command-mode panel scanning the splash framebuffer (grus).
 *
 * Register layout: SDE v4 (sdm670-sde.dtsi offsets from mdss_mdp@ae00000,
 * Linux sde_hw_ctl.c / sde_hw_pingpong.c / sde_hw_sspp.c):
 *   CTL n   0x2000 + 0x200 n   TOP 0x14 (bit 17 command mode, 7:4 intf), START 0x1c
 *   PP n    0x71000 + 0x800 n  TEAR_CHECK_EN 0x00, AUTOREFRESH_CONFIG 0x30
 *                              (bit 31 enable, low bits: TE frames per refresh)
 *   SSPP    SRC_SIZE 0x00, SRC0_ADDR 0x14, SRC_YSTRIDE0 0x24, SRC_FORMAT 0x30
 * The display is left running by ABL, so the MDP is powered and clocked.
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
		if (rd32(s + SSPP_SRC0_ADDR) == (uint32_t)FB_PHYS) {
			pi->sspp = i;
			pi->src_addr = rd32(s + SSPP_SRC0_ADDR);
			pi->src_stride = rd32(s + SSPP_SRC_YSTRIDE0) & 0xffff;
			pi->src_format = rd32(s + SSPP_SRC_FORMAT);
			pi->src_size = rd32(s + SSPP_SRC_SIZE);
			break;
		}
	}
	if (pi->pp < 0)
		return -1;	/* no tear check: autorefresh would never fire */
	pi->pp_ar_before = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	/* Every TE (frame count 1), like the Linux SDE autorefresh path. */
	wr32(PP(pi->pp) + PP_AUTOREFRESH, (1U << 31) | 1);
	pi->pp_ar_after = rd32(PP(pi->pp) + PP_AUTOREFRESH);
	/* Autorefresh starts with the next kickoff. */
	if (pi->ctl >= 0)
		wr32(CTL(pi->ctl) + CTL_START, 1);
	return 0;
}

void panel_report(const struct panel_info *pi)
{
	con_hex2("PANEL", "ctl=", (uint32_t)pi->ctl, "top=", pi->ctl_top);
	con_hex2(" pp", "pp=", (uint32_t)pi->pp, "te=", pi->pp_te);
	con_hex2(" autoref", "was=", pi->pp_ar_before, "now=", pi->pp_ar_after);
	con_hex2(" sspp", "n=", (uint32_t)pi->sspp, "addr=", pi->src_addr);
	con_hex2(" sspp", "stride=", pi->src_stride, "fmt=", pi->src_format);
	con_hex2(" sspp", "size=", pi->src_size, "want=", FB_STRIDE);
	if (pi->sspp >= 0 && pi->src_stride != FB_STRIDE)
		con_str("PANEL", "WARNING: splash is not 32bpp 1080 wide");
}
#else
int panel_init(struct panel_info *pi)
{
	pi->ctl = pi->pp = pi->sspp = -1;
	return 0;
}

void panel_report(const struct panel_info *pi)
{
	(void)pi;
}
#endif
