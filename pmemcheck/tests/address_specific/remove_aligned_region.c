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
#include "../../pmemcheck.h"


int main ( void )
{
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x100, 0x40);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;

    /* head aligned */
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(0x100, 0x10);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;

    /* tail aligned */
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(0x130, 0x10);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;

    return 0;
}
