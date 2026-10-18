#pragma once
#include <stdint.h>
void test_ufs(void);
int ufs_diag_open(void);
int ufs_save_log(const char *stage);
uint32_t ufs_diag_lba(void);
