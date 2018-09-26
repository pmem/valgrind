/*
 * Persistent memory checker.
 * Copyright (c) 2014-2018, Intel Corporation.
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
#include <stdio.h>

#define FILE_SIZE (16 * 1024 * 1024)

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    float *floatp = (float *)((uintptr_t)base);
    double *doublep = (double *)((uintptr_t)base + 16);

    *floatp = 1.0;
    *doublep = 2.0;

    return 0;
}
