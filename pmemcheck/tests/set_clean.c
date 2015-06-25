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
#include "../pmemcheck.h"
#include <stdint.h>

static struct tester {
    int64_t a;
    int32_t b;
    int32_t c;
} Test_struct;

int main ( void )
{
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&Test_struct, sizeof (Test_struct));

    /* register some stores to the Test_struct */
    Test_struct.a = 1;
    Test_struct.b = 2;
    Test_struct.c = 3;

    /* set different state to the stores */
    VALGRIND_PMC_DO_FLUSH(&(Test_struct.a), sizeof (Test_struct.a));
    VALGRIND_PMC_DO_FENCE;
    VALGRIND_PMC_DO_FLUSH(&(Test_struct.b), sizeof (Test_struct.b));

    VALGRIND_PMC_WRITE_STATS;

    /* start setting the Test_struct as clean */

    int16_t *slice = (void *)&(Test_struct.b);
    VALGRIND_PMC_SET_CLEAN(slice - 1, sizeof (int64_t));

    VALGRIND_PMC_WRITE_STATS;

    VALGRIND_PMC_SET_CLEAN(&(Test_struct.c), sizeof (Test_struct.c));

    VALGRIND_PMC_WRITE_STATS;

    VALGRIND_PMC_SET_CLEAN(&(Test_struct.a), sizeof (Test_struct.a));

    VALGRIND_PMC_WRITE_STATS;

    return 0;
}
