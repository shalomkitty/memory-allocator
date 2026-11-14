/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DIE(assertion, message) do { \
    if (assertion) { perror(message); exit(errno ? errno : 1); } \
} while (0)

struct block_meta {
    size_t size;
    int status;
    struct block_meta *prev;
    struct block_meta *next;
};

#define STATUS_FREE 0
#define STATUS_ALLOC 1
#define STATUS_MAPPED 2
