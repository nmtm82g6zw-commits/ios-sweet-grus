#pragma once
#include <stdint.h>

/* Boot the SWEETXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb);
/* 1 if the ABL ramdisk holds a SWEETXNU payload. */
int loader_has_payload(uintptr_t dtb);
