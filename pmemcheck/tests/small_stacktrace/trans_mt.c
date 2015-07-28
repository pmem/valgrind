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
#include <stdint.h>
#include <pthread.h>

#define FILE_SIZE (16 * 1024 * 1024)

/*
 * Perform tx in a thread.
 */
static void *
make_tx(void *arg)
{
    int8_t *i8p = arg;
    int16_t *i16p = (int16_t *)((uintptr_t)arg + 8);
    int32_t *i32p = (int32_t *)((uintptr_t)arg + 16);
    int64_t *i64p = (int64_t *)((uintptr_t)arg + 24);

    /* transaction not ended on purpose */
    VALGRIND_PMC_START_TX;

    VALGRIND_PMC_ADD_TO_TX(i32p, sizeof (*i32p));
    /* dirty stores */
    *i8p = 1;
    *i16p = 2;
    *i32p = 3;
    *i64p = 4;
    return NULL;
}

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    pthread_t t1;
    pthread_t t2;
    pthread_create(&t1, NULL, make_tx, base);
    pthread_create(&t2, NULL, make_tx, base);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
