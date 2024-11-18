/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>

#include "printf.h"
#include "block_meta.h"

typedef struct block_meta block_meta_t;

#define MMAP_THRESHOLD (128 * 1024)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define META_SIZE (ALIGN(sizeof(block_meta_t)))

#define MIN(a, b) (((a) < (b))?(a):(b))

void *os_malloc(size_t size);
void os_free(void *ptr);
void *os_calloc(size_t nmemb, size_t size);
void *os_realloc(void *ptr, size_t size);
