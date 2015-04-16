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
#include "../common.h"

int main ( void )
{
    VALGRIND_PMC_LOG_STORES;
    VALGRIND_PMC_DO_FLUSH(0x0, 2);
    VALGRIND_PMC_DO_FLUSH(0x10, 7);
    VALGRIND_PMC_DO_FLUSH(0x20, 87);

    return 0;
}
