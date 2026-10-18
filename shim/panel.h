#pragma once
#include <stdint.h>

/*
 * Command-mode panels (grus): ABL's MDP only sends the splash buffer to the
 * panel on a kickoff, so nothing drawn into it would ever appear. panel_init()
 * turns on ping-pong autorefresh for the active command-mode pipe, so the MDP
 * resends the buffer on every TE like a video-mode panel, and records what it
 * found. A no-op (returns 0) on video-mode boards.
 */
struct panel_info {
	int ctl, pp, sspp;		/* -1 when not found */
	uint32_t ctl_top, pp_te, pp_ar_before, pp_ar_after;
	uint32_t src_addr, src_stride, src_format, src_size;
	uint32_t fmt_before, unpack_before, stride_before;	/* splash as ABL left it */
	int converted;		/* 1: RGB888 splash switched to XRGB8888 */
	int bpp;		/* bytes/pixel the pipe scans after panel_init (3 or 4) */
	uint32_t pipe_addr[5], pipe_fmt[5], pipe_stride[5];
};

int panel_init(struct panel_info *pi);
/* Switch an RGB888 splash pipe to XRGB8888 (FB_STRIDE); 0 if done. */
int panel_to_xrgb(struct panel_info *pi);
void panel_report(const struct panel_info *pi);
/* Short register dump for the format-independent windows. */
void panel_report_short(const struct panel_info *pi);
