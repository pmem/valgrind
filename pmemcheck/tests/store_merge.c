/*
 * Persistent memory checker.
 * Copyright (c) 2016, Intel Corporation.
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

static void fake_memcpy(void *dst, const void *src, size_t size)
{
    uint8_t *to = (uint8_t *)dst;
    uint8_t *from = (uint8_t *)src;
    int i;
    for (i = 0; i < size; ++i)
        to[i] = from[i];
}

static void fake_memset(void *dst, int c, size_t size)
{
    uint8_t *to = (uint8_t *)dst;
    int i;
    for (i = 0; i < size; ++i)
        to[i] = c;
}

static void merge_memcpy(int8_t *start) {
    *start = 1;
    /* advance by two, to make a gap */
    start += 2;
    *start = 2;
    /* fill the gap */
    --start;
    *start = 3;
}

static void overlap_test_memset(int16_t *first, int16_t *second)
{
    *first = 1;
    *second = 2;
}

int main(void)
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    /* will not be merged */
    int8_t *i8p = base;
    int16_t *i16p = (int16_t *)((uintptr_t)base + 1);

    /* will be merged */
    int32_t *i32p = (int32_t *)((uintptr_t)base + 8);
    int64_t *i64p = (int64_t *)((uintptr_t)base + 64);

    *i8p = 1;
    VALGRIND_PMC_DO_FLUSH(i8p, sizeof(*i8p));
    *i16p = 2;

    fake_memset(i32p, 1, 4 * sizeof(*i32p));

    fake_memcpy(i64p, i8p, 4 * sizeof(*i64p));

    merge_memcpy(i8p + 512);

    /* advance the pointer */
    i16p += 512;
    int16_t *i16p_ovelap = (int16_t *)((uintptr_t)i16p + 1);
    overlap_test_memset(i16p, i16p_ovelap);
    overlap_test_memset(i16p + 2, i16p_ovelap + 1);

    /* minimal distance non-adjacent stores */
    i16p += 4;
    *i16p = 0;
    i16p += 2;
    *i16p = 1;
    i16p += 2;
    *i16p = 2;

    return 0;
}
