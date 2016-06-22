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

static void do_overlap(int16_t *first, int16_t *second)
{
    *first = 0xFFFF;
    *second = 0xAAAA;
}

int main(void)
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);


    int16_t *i16p1 = (int16_t *)((uintptr_t)base);
    int16_t *i16p2 = (int16_t *)((uintptr_t)base + 1);

    do_overlap(i16p1, i16p2);
    do_overlap(i16p1 + 2, i16p2 + 1);

    int32_t *i32p = (int32_t *)((uintptr_t)base + 32);
    int8_t *i8p = base + 33;

    *i32p = 1;
    *i8p = 2;

    return 0;
}
