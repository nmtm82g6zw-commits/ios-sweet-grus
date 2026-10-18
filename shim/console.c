/*
 * Minimal text console on the ABL splash framebuffer. With the MMU off
 * every access is Device memory, so stay aligned (-mstrict-align).
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

volatile uint32_t *con_fb = (volatile uint32_t *)FB_BASE;
static uint32_t con_bg;
static int row;
static int con_bpp;		/* 0 = not set: 4 */
static uint32_t con_stride;

void con_set_format(int bytes_per_pixel, uint32_t stride)
{
	con_bpp = bytes_per_pixel == 3 ? 3 : 4;
	con_stride = stride ? stride : (uint32_t)FB_W * con_bpp;
}

/* One pixel; c is 0xAARRGGBB, stored B,G,R(,A) like the splash formats. */
static inline void put_px(int x, int y, uint32_t c)
{
	if (con_bpp == 3) {
		volatile uint8_t *p = (volatile uint8_t *)FB_BASE + (uintptr_t)y * con_stride + (uintptr_t)x * 3;
		p[0] = (uint8_t)c;
		p[1] = (uint8_t)(c >> 8);
		p[2] = (uint8_t)(c >> 16);
	} else {
		uint32_t st = con_stride ? con_stride : FB_STRIDE;
		con_fb[(uintptr_t)y * (st / 4) + x] = c;
	}
}

void con_clear(uint32_t bg)
{
	for (int y = 0; y < FB_H; y++)
		for (int x = 0; x < FB_W; x++)
			put_px(x, y, bg);
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			uint32_t c = (g[gy] & (0x80 >> gx)) ? fg : con_bg;
			for (int sy = 0; sy < SCALE; sy++)
				for (int sx = 0; sx < SCALE; sx++)
					put_px(x0 + gx * SCALE + sx, y0 + gy * SCALE + sy, c);
		}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

/* Three 120x60 blocks, red / green / blue, on the current row: channel order check. */
void con_color_bars(void)
{
	int y0 = MARGIN + row * CH_H;
	for (int b = 0; b < 3; b++) {
		uint32_t c = b == 0 ? 0xffff0000U : b == 1 ? 0xff00ff00U : 0xff0000ffU;
		for (int y = y0; y < y0 + 2 * CH_H && y < FB_H; y++)
			for (int x = MARGIN + 400 + b * 140; x < MARGIN + 520 + b * 140; x++)
				put_px(x, y, c);
	}
	put_str(0, "RGB", YELLOW);
	row += 2;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
