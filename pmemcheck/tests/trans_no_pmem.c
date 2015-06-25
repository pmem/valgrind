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
#include <stdint.h>

#include "../pmemcheck.h"

int main ( void )
{

    int8_t i8;
    int16_t i16;

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&i8, sizeof (i8));

    VALGRIND_PMC_START_TX;

    VALGRIND_PMC_ADD_TO_TX(&i16, sizeof (i16));
    i16 = 2;

    VALGRIND_PMC_ADD_TO_TX(&i8, sizeof (i8));
    i8 = 1;

    VALGRIND_PMC_WRITE_STATS;

    VALGRIND_PMC_REMOVE_FROM_TX(&i16, sizeof (i16));
    VALGRIND_PMC_REMOVE_FROM_TX(&i8, sizeof (i8));

    i16 = 2;
    i8 = 1;

    VALGRIND_PMC_END_TX;

    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&i8, sizeof (i8));

    return 0;
}
