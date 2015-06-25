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

    int32_t *i32p = base;
    int8_t *i8p = i32p;

    VALGRIND_PMC_START_TX_N(1234);
    VALGRIND_PMC_START_TX_N(12345);

    VALGRIND_PMC_ADD_TO_TX_N(1234, i32p, sizeof (*i32p));
    VALGRIND_PMC_ADD_TO_TX_N(12345, i8p, sizeof (*i8p));

    VALGRIND_PMC_END_TX_N(12345);

    VALGRIND_PMC_REMOVE_FROM_TX_N(1234, i32p, sizeof (*i32p));
    /* add two adjacent regions within i32p */
    VALGRIND_PMC_ADD_TO_TX_N(1234, i8p, sizeof (*i8p));
    ++i8p;
    VALGRIND_PMC_ADD_TO_TX_N(1234, i8p, sizeof (*i8p));

    /* after this add, only cache should be present */
    VALGRIND_PMC_ADD_TO_TX_N(1234, i32p, sizeof (*i32p));
    /* clear cache - no more regions within this transaction */
    VALGRIND_PMC_REMOVE_FROM_TX_N(1234, i32p, sizeof (*i32p));
    /* make store within i32p where a region was previously registered */
    *i8p = 42;

    VALGRIND_PMC_END_TX_N(1234);


    return 0;
}
