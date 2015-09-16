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
#include <xmmintrin.h>

//#define	_mm_clflush(addr)\
//	asm volatile("clflush %0" : "+m" (*(volatile char *)addr));
#define	_mm_sfence()\
	asm volatile(".byte 0x0f, 0xae, 0xf8");
#define	_mm_pcommit()\
	asm volatile(".byte 0x66, 0x0f, 0xae, 0xf8");

#define FILE_SIZE (16 * 1024 * 1024)

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    int64_t *i64p = base;

    /* dirty stores */
    *i64p = 4;
    _mm_clflush(base);
    /* flush should be registered as "invalid" */
    _mm_clflush(base);
    _mm_sfence();
    /* flush should be registered as "invalid" */
    _mm_clflush(base);
    _mm_pcommit();
    /* flush should be registered as "invalid" */
    _mm_clflush(base);
    _mm_sfence();

    _mm_pcommit();
    _mm_sfence();
    return 0;
}
