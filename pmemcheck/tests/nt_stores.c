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
#include "common.h"
#include <xmmintrin.h>

#define FILE_SIZE (16 * 1024 * 1024)

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    off_t dest_off = 4096;

    __m128i r128;
    __m128i *source128 = base;
    __m128i *dest128 = (__m128i *)((uintptr_t)base + dest_off);

    int source32 = 15;
    int *dest32 = (int *)((uintptr_t)base + dest_off + sizeof (*dest128));

    r128 = _mm_loadu_si128(source128);

    /* do non-temporal stores */
    _mm_stream_si128(dest128, r128);
    _mm_stream_si32(dest32, source32);

    return 0;
}
