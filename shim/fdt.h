#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/*
 * 0: [base, base+size) lies inside one /memory range and overlaps no static
 * /reserved-memory child; 1: not inside /memory; 2: overlaps the carve-out
 * returned in hit_base and hit_size; -1: tree not understood.
 */
int fdt_ram_check(uintptr_t fdt, uint64_t base, uint64_t size, uint64_t *hit_base, uint64_t *hit_size);
/* /memory reg ranges (up to max stored); returns the total count or -1. */
int fdt_memory_banks(uintptr_t fdt, uint64_t *base, uint64_t *size, int max);
