/*
 * Persistent memory checker.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, or (at your option) any later version, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include "common.h"
#include <stdint.h>

#define FILE_SIZE (16 * 1024 * 1024)

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    int16_t *i16p = base;
    int32_t *i32p = (int32_t *)(i16p + 1);

    VALGRIND_PMC_LOG_STORES;

    /* dirty stores, both on the same cache line */
    *i16p = 1;
    *i32p = 2;
    /* full persist first store only */
    VALGRIND_PMC_DO_FLUSH(i16p, sizeof (*i16p));
    VALGRIND_PMC_DO_FENCE;
    VALGRIND_PMC_DO_COMMIT;
    VALGRIND_PMC_DO_FENCE;

    /* move far enough, for example to the next page */
    int64_t *i64p = (int64_t *)((uintptr_t )base + 4096);
    *i64p = 7;
    /* split the store in half with a flush */
    void *flush_base = (uint8_t *)i64p + 4;
    VALGRIND_PMC_DO_FLUSH(flush_base, 4);
    VALGRIND_PMC_DO_FENCE;
    VALGRIND_PMC_DO_COMMIT;

    return 0;
}
