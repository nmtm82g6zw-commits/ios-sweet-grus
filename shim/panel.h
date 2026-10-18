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
};

int panel_init(struct panel_info *pi);
void panel_report(const struct panel_info *pi);
