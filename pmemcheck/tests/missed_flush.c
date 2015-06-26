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
#include <stdint.h>

#include "common.h"

#define FILE_SIZE (16 * 1024 * 1024)

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    /* flush should be registered as superfluous */
    VALGRIND_PMC_DO_FLUSH(base, 64);
    /* flush should be registered as superfluous */
    VALGRIND_PMC_DO_FLUSH((uintptr_t)base + 64, 65);
    /* flush should be registered as superfluous */
    VALGRIND_PMC_DO_FLUSH(0, 64);
    return 0;
}
