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
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>


int main ( void )
{
    static char file_path[] = "./pmemcheck.testfile";

    int fd;
    if ((fd = open(file_path, O_CREAT | O_RDWR)) < 0) {
        return NULL;
    }
    int size = 2048;
    if ((errno = posix_fallocate(fd, 0, size)) != 0) {
        int oerrno = errno;
        if (fd != -1)
            close(fd);
        errno = oerrno;
        return 1;
    }

    void *base;
    if ((base = mmap(NULL, size, PROT_WRITE, MAP_PRIVATE|MAP_NORESERVE, fd,
            0)) == MAP_FAILED) {
        int oerrno = errno;
        if (fd != -1)
            close(fd);
        errno = oerrno;
        return 1;
    }

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(fd, base, size);

    unlink(file_path);
    close(fd);

    return 0;
}
