// SPDX-License-Identifier: BSD-3-Clause
#define _DEFAULT_SOURCE

#include "osmem.h"
#include "block_meta.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGNMENT 8UL
#define HEAP_PREALLOC_SIZE (128UL * 1024UL)
#define MMAP_THRESHOLD (128UL * 1024UL)
#define META_SIZE ((sizeof(struct block_meta) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

/* The list contains heap blocks only, in ascending address order. */
static struct block_meta *heap_head;
static struct block_meta *heap_tail;
static int heap_preallocated;

static int align_size(size_t size, size_t *result)
{
	if (size > SIZE_MAX - (ALIGNMENT - 1)) {
		errno = ENOMEM;
		return 0;
	}

	*result = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	return 1;
}

static int allocation_size(size_t size, size_t *aligned, size_t *total)
{
	if (!align_size(size, aligned))
		return 0;
	if (*aligned > SIZE_MAX - META_SIZE) {
		errno = ENOMEM;
		return 0;
	}

	*total = *aligned + META_SIZE;
	return 1;
}

static void *block_payload(struct block_meta *block)
{
	return (char *)block + META_SIZE;
}

static struct block_meta *payload_block(void *ptr)
{
	return (struct block_meta *)((char *)ptr - META_SIZE);
}

static void initialize_block(struct block_meta *block, size_t size, int status)
{
	block->size = size;
	block->status = status;
	block->prev = NULL;
	block->next = NULL;
}

static struct block_meta *map_block(size_t aligned, size_t total)
{
	void *mapping = mmap(NULL, total, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (mapping == MAP_FAILED)
		return NULL;

	struct block_meta *block = mapping;

	initialize_block(block, aligned, STATUS_MAPPED);
	return block;
}

/* This is the only heap preallocation. A mapped allocation does not trigger it. */
static struct block_meta *preallocate_heap(void)
{
	void *memory = sbrk(HEAP_PREALLOC_SIZE);

	if (memory == (void *)-1)
		return NULL;

	struct block_meta *block = memory;

	initialize_block(block, HEAP_PREALLOC_SIZE - META_SIZE, STATUS_FREE);
	heap_head = block;
	heap_tail = block;
	heap_preallocated = 1;
	return block;
}

static void merge_with_next(struct block_meta *block)
{
	struct block_meta *next = block->next;

	block->size += META_SIZE + next->size;
	block->next = next->next;
	if (block->next)
		block->next->prev = block;
	else
		heap_tail = block;
}

static void coalesce_all(void)
{
	for (struct block_meta *block = heap_head; block && block->next;) {
		if (block->status == STATUS_FREE &&
		    block->next->status == STATUS_FREE)
			merge_with_next(block);
		else
			block = block->next;
	}
}

static struct block_meta *find_best_fit(size_t size)
{
	struct block_meta *best = NULL;

	for (struct block_meta *block = heap_head; block; block = block->next) {
		if (block->status != STATUS_FREE || block->size < size)
			continue;
		if (!best || block->size < best->size)
			best = block;
	}

	return best;
}

/* A free tail smaller than a metadata header and one aligned byte is unusable. */
static void split_block(struct block_meta *block, size_t size)
{
	if (block->size < size || block->size - size < META_SIZE + ALIGNMENT)
		return;

	struct block_meta *remainder =
		(struct block_meta *)((char *)block_payload(block) + size);

	initialize_block(remainder, block->size - size - META_SIZE, STATUS_FREE);
	remainder->prev = block;
	remainder->next = block->next;
	if (remainder->next)
		remainder->next->prev = remainder;
	else
		heap_tail = remainder;

	block->next = remainder;
	block->size = size;
}

static int extend_tail(struct block_meta *block, size_t requested)
{
	if (requested <= block->size)
		return 1;

	size_t extension = requested - block->size;

	if (extension > INTPTR_MAX) {
		errno = ENOMEM;
		return 0;
	}

	if (sbrk((intptr_t)extension) == (void *)-1)
		return 0;

	block->size = requested;
	return 1;
}

static struct block_meta *append_heap_block(size_t aligned, size_t total)
{
	if (total > INTPTR_MAX) {
		errno = ENOMEM;
		return NULL;
	}

	void *memory = sbrk((intptr_t)total);

	if (memory == (void *)-1)
		return NULL;

	struct block_meta *block = memory;

	initialize_block(block, aligned, STATUS_ALLOC);
	block->prev = heap_tail;
	if (heap_tail)
		heap_tail->next = block;
	else
		heap_head = block;

	heap_tail = block;
	return block;
}

/* Called with an aligned payload size and only for heap-backed requests. */
static void *allocate_heap(size_t aligned, size_t total)
{
	if (!heap_preallocated && !preallocate_heap())
		return NULL;

	coalesce_all();

	struct block_meta *block = find_best_fit(aligned);

	if (block) {
		split_block(block, aligned);
		block->status = STATUS_ALLOC;
		return block_payload(block);
	}

	/* The only block that may grow without a new metadata header is a free tail. */
	if (heap_tail && heap_tail->status == STATUS_FREE) {
		block = heap_tail;
		if (!extend_tail(block, aligned))
			return NULL;
		block->status = STATUS_ALLOC;
		return block_payload(block);
	}

	block = append_heap_block(aligned, total);
	return block ? block_payload(block) : NULL;
}

static void *allocate(size_t size, size_t threshold)
{
	if (size == 0)
		return NULL;

	size_t aligned, total;

	if (!allocation_size(size, &aligned, &total))
		return NULL;

	if (total >= threshold) {
		struct block_meta *block = map_block(aligned, total);

		return block ? block_payload(block) : NULL;
	}

	return allocate_heap(aligned, total);
}

void *os_malloc(size_t size)
{
	return allocate(size, MMAP_THRESHOLD);
}

void os_free(void *ptr)
{
	if (!ptr)
		return;

	struct block_meta *block = payload_block(ptr);

	if (block->status == STATUS_MAPPED) {
		int result = munmap(block, META_SIZE + block->size);

		DIE(result == -1, "munmap");
	} else if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (!nmemb || !size)
		return NULL;
	if (nmemb > SIZE_MAX / size) {
		errno = ENOMEM;
		return NULL;
	}

	size_t bytes = nmemb * size;
	long page_size = sysconf(_SC_PAGESIZE);

	if (page_size <= 0)
		page_size = 4096;

	void *ptr = allocate(bytes, (size_t)page_size);

	if (ptr)
		memset(ptr, 0, bytes);
	return ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *block = payload_block(ptr);

	/* Reallocating an already-freed heap block is explicitly rejected. */
	if (block->status == STATUS_FREE)
		return NULL;

	size_t aligned, total;

	if (!allocation_size(size, &aligned, &total))
		return NULL;

	if (block->status == STATUS_ALLOC) {
		if (block->size >= aligned) {
			split_block(block, aligned);
			return ptr;
		}

		/* Heap allocations that cross the mmap threshold must move. */
		if (total < MMAP_THRESHOLD) {
			/* Preserve each merge even if it is not sufficient to grow. */
			while (block->next && block->next->status == STATUS_FREE) {
				merge_with_next(block);
				if (block->size >= aligned) {
					split_block(block, aligned);
					return ptr;
				}
			}

			if (block == heap_tail && extend_tail(block, aligned))
				return ptr;
		}
	}

	/* Mapped blocks are always reallocated, never mremap-ed. */
	void *replacement = os_malloc(size);

	if (!replacement)
		return NULL;

	size_t copy_size = block->size < size ? block->size : size;

	memcpy(replacement, ptr, copy_size);
	os_free(ptr);
	return replacement;
}
