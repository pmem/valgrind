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
#include "../../pmemcheck.h"


int main ( void )
{
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x100, 0x10);
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x110, 0x10);
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x120, 0x10);

    /* overlaps tail of first, whole second and head of third regions */
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(0x10B, 0x16);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x140, 0x60);
    /* remove region within the mapping */
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(0x150, 0x10);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(0x200, 0x60);
    /* remove an exact match */
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(0x200, 0x60);
    VALGRIND_PMC_PRINT_PMEM_MAPPINGS;
    return 0;
}
