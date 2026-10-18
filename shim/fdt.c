/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

/*
 * /memory and /reserved-memory: enough to check that the RAM handed to xnu
 * really exists on this phone and is not a static carve-out. Qualcomm trees
 * use #address-cells = #size-cells = 2 at the root and in reserved-memory;
 * other cell sizes are reported as "unknown" (-1) rather than guessed.
 */
static int prefix(const char *name, const char *want)
{
	while (*want && *name == *want)
		name++, want++;
	return !*want && (*name == 0 || *name == '@');
}

static uint64_t be64(uintptr_t a)
{
	return (uint64_t)be32(a) << 32 | be32(a + 4);
}

/*
 * Walk depth-1 "memory" nodes (want_reserved = 0) or the children of
 * depth-1 "reserved-memory" (want_reserved = 1); call cb for each reg range.
 * Returns the number of ranges, or -1 for an unsupported tree.
 */
typedef int (*fdt_range_cb)(void *ctx, uint64_t base, uint64_t size);

static int fdt_ranges(uintptr_t fdt, int want_reserved, fdt_range_cb cb, void *ctx)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return -1;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_mem = 0, in_resv = 0, n = 0;
	uint32_t ac = 2, sc = 2;	/* root cells; checked below */

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			if (depth == 2) {
				in_mem = prefix(name, "memory");
				in_resv = prefix(name, "reserved-memory");
			}
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (depth == 2)
				in_mem = in_resv = 0;
			depth--;
			if (depth == 0)
				return n;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			const char *pname = (const char *)(strings + nameoff);
			uintptr_t val = p + 8;
			if (depth == 1 && len == 4 && streq(pname, "#address-cells"))
				ac = be32(val);
			else if (depth == 1 && len == 4 && streq(pname, "#size-cells"))
				sc = be32(val);
			else if (streq(pname, "reg") &&
				 ((!want_reserved && in_mem && depth == 2) ||
				  (want_reserved && in_resv && depth == 3))) {
				if (ac != 2 || sc != 2 || len % 16)
					return -1;
				for (uint32_t off = 0; off < len; off += 16) {
					n++;
					if (cb && cb(ctx, be64(val + off), be64(val + off + 8)))
						return n;
				}
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		case FDT_END:
			return n;
		default:
			return -1;
		}
	}
}

struct range_check {
	uint64_t base, end, hit_base, hit_size;
	int found;
};

static int contains_cb(void *ctx, uint64_t base, uint64_t size)
{
	struct range_check *c = ctx;
	if (c->base >= base && c->end <= base + size) {
		c->found = 1;
		return 1;
	}
	return 0;
}

static int overlap_cb(void *ctx, uint64_t base, uint64_t size)
{
	struct range_check *c = ctx;
	if (size && c->base < base + size && base < c->end) {
		c->found = 1;
		c->hit_base = base;
		c->hit_size = size;
		return 1;
	}
	return 0;
}

int fdt_ram_check(uintptr_t fdt, uint64_t base, uint64_t size, uint64_t *hit_base, uint64_t *hit_size)
{
	struct range_check c = { base, base + size, 0, 0, 0 };

	if (fdt_ranges(fdt, 0, contains_cb, &c) < 0)
		return -1;
	if (!c.found)
		return 1;	/* not inside one /memory range */
	c.found = 0;
	if (fdt_ranges(fdt, 1, overlap_cb, &c) < 0)
		return -1;
	if (c.found) {
		*hit_base = c.hit_base;
		*hit_size = c.hit_size;
		return 2;	/* overlaps a static carve-out */
	}
	return 0;
}

struct range_list {
	uint64_t *base, *size;
	int max, n;
};

static int list_cb(void *ctx, uint64_t base, uint64_t size)
{
	struct range_list *l = ctx;
	if (l->n < l->max) {
		l->base[l->n] = base;
		l->size[l->n] = size;
	}
	l->n++;
	return 0;
}

int fdt_memory_banks(uintptr_t fdt, uint64_t *base, uint64_t *size, int max)
{
	struct range_list l = { base, size, max, 0 };
	return fdt_ranges(fdt, 0, list_cb, &l) < 0 ? -1 : l.n;
}
