#pragma once
#include <stdint.h>

#include "board.h"

#define FB_BASE		FB_PHYS		/* FB_W/FB_H/FB_STRIDE: board.h */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

extern volatile uint32_t *con_fb;

/* Pixel layout the display really scans (grus ABL: 3 bytes, stride 3240). */
void con_set_format(int bytes_per_pixel, uint32_t stride);
void con_color_bars(void);

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
