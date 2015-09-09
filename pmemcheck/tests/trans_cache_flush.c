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
    int64_t *i64p = base;

    VALGRIND_PMC_START_TX_N(1234);

    /* first three should be merged, fourth cached */
    VALGRIND_PMC_ADD_TO_TX_N(1234, i16p, sizeof (*i16p));

    /* check for flush of empty cache */
    *i16p = 9;

    ++i16p;
    VALGRIND_PMC_ADD_TO_TX_N(1234, i16p, sizeof (*i16p));
    ++i16p;
    VALGRIND_PMC_ADD_TO_TX_N(1234, i16p, sizeof (*i16p));
    ++i16p;
    VALGRIND_PMC_ADD_TO_TX_N(1234, i16p, sizeof (*i16p));

    /* trigger flush + merge on write */
    *i64p = 9;
    /* ignore persistency related errors */
    VALGRIND_PMC_SET_CLEAN(i64p, sizeof (*i64p));

    VALGRIND_PMC_END_TX_N(1234);

    return 0;
}
