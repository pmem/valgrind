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

/* Thread worker arguments. */
struct thread_ops {
    /* The txid to contribute to and close. */
    int txid;

    /* What to modify. */
    int32_t *i32p;
};

/*
 * Perform tx in a thread.
 */
static void *
make_tx(void *arg)
{
    struct thread_ops *args = arg;

    /* transaction not ended on purpose */
    VALGRIND_PMC_ADD_THREAD_TX_N(args->txid);

    VALGRIND_PMC_ADD_TO_TX_N(args->txid, args->i32p, sizeof (*(args->i32p)));
    /* dirty stores */
    *(args->i32p) = 3;

    VALGRIND_PMC_END_TX_N(args->txid);
    return NULL;
}

int main ( void )
{
    /* make, map and register a temporary file */
    void *base = make_map_tmpfile(FILE_SIZE);

    struct thread_ops arg;

    arg.txid = 1234;
    arg.i32p = (int32_t *)(base);

    VALGRIND_PMC_START_TX_N(arg.txid);

    pthread_t t1;
    pthread_create(&t1, NULL, make_tx, &arg);
    pthread_join(t1, NULL);

    return 0;
}
