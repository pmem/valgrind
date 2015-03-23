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

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "../pmemcheck.h"

/**
* \brief Makes and maps a temporary file.
* \param[in] size The size of the file.
*/
void *
make_map_tmpfile(size_t size)
{
    static char file_path[] = "./pmemcheck.XXXXXX";

    int fd;
    if ((fd = mkstemp(file_path)) < 0) {
        return NULL;
    }

    unlink(file_path);

    if ((errno = posix_fallocate(fd, 0, size)) != 0) {
        int oerrno = errno;
        if (fd != -1)
            close(fd);
        errno = oerrno;
        return NULL;
    }

    void *base;
    if ((base = mmap(NULL, size, PROT_WRITE, MAP_PRIVATE|MAP_NORESERVE, fd,
            0)) == MAP_FAILED) {
        int oerrno = errno;
        if (fd != -1)
            close(fd);
        errno = oerrno;
        return NULL;
    }

    close(fd);
    
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(base, size);

    return base;
}
