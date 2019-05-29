/*
 * Persistent memory checker.
 * Copyright (c) 2014-2015, Intel Corporation.
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
#include "../common.h"
#include <stdint.h>

#define FILE_SIZE (16 * 1024 * 1024)
#define EMIT_LOG_VALUE "EMIT_TEST_VALUE"

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    int8_t *i8p = base;
    int16_t *i16p = (int16_t *)((uintptr_t)base + 8);
    int32_t *i32p = (int32_t *)((uintptr_t)base + 16);

    VALGRIND_PMC_LOG_STORES;
    /* dirty stores */
    *i8p = 1;
    /* full persist */
    VALGRIND_PMC_DO_FLUSH(base, 8);
    VALGRIND_PMC_DO_FENCE;
    VALGRIND_PMC_NO_LOG_STORES;
    VALGRIND_PMC_DO_FENCE;
    VALGRIND_PMC_ADD_LOG_REGION(i16p, 8);
    *i16p = 2;
    VALGRIND_PMC_DO_FLUSH(i16p, 8);
    VALGRIND_PMC_DO_FENCE;
    *i32p = 3;
    VALGRIND_PMC_DO_FLUSH(i32p, 8);
    i32p += 8;
    *i32p = 3;
    VALGRIND_PMC_FULL_REORDER;
    VALGRIND_PMC_ONLY_FAULT;
    VALGRIND_PMC_PARTIAL_REORDER;
    VALGRIND_PMC_STOP_REORDER_FAULT;
    VALGRIND_PMC_DEFAULT_REORDER;
    VALGRIND_PMC_EMIT_LOG(EMIT_LOG_VALUE);
    return 0;
}
