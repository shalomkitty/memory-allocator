/* SPDX-License-Identifier: BSD-3-Clause */
#include "osmem.h"
#include "block_meta.h"

#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#define CHECK_ALIGNED(p) assert(((uintptr_t)(p) % 8U) == 0U)

static struct block_meta *meta(void *ptr)
{
	return (struct block_meta *)((char *)ptr - sizeof(struct block_meta));
}

static void test_first_allocation_and_thresholds(void)
{
	assert(os_malloc(0) == NULL);
	assert(os_calloc(0, 100) == NULL);
	assert(os_calloc(100, 0) == NULL);
	assert(os_realloc(NULL, 0) == NULL);

	void *first = os_malloc(131032);
	assert(first);
	CHECK_ALIGNED(first);
	assert(meta(first)->status == STATUS_ALLOC);

	void *second = os_malloc(10);
	assert(second);
	assert((uintptr_t)second - (uintptr_t)first == 131072U);
	CHECK_ALIGNED(second);
	assert(meta(second)->size == 16);

	void *mapped = os_malloc(131040);
	assert(mapped && meta(mapped)->status == STATUS_MAPPED);
	os_free(mapped);

	void *mapped_calloc = os_calloc(4064, 1);
	assert(mapped_calloc && meta(mapped_calloc)->status == STATUS_MAPPED);
	for (size_t i = 0; i < 4064; i++)
		assert(((unsigned char *)mapped_calloc)[i] == 0);
	os_free(mapped_calloc);

	os_free(second);
	os_free(first);
	write(STDOUT_FILENO, "PASS: preallocation, alignment, thresholds, zero-size calls\n", sizeof("PASS: preallocation, alignment, thresholds, zero-size calls\n") - 1);
}

static void test_split_coalesce_best_fit(void)
{
	void *a = os_malloc(128);
	void *b = os_malloc(512);
	void *c = os_malloc(128);
	assert(a && b && c);

	os_free(b);
	void *d = os_malloc(200);
	assert(d == b);
	void *e = os_malloc(200);
	assert(e == (char *)d + 200 + sizeof(struct block_meta) + 0);

	os_free(d);
	os_free(e);
	void *f = os_malloc(480);
	assert(f == b);
	os_free(a);
	os_free(c);
	os_free(f);

	void *huge = os_malloc(131032);
	assert(huge);
	os_free(huge);
	write(STDOUT_FILENO, "PASS: split, coalesce, best fit and block reuse\n", sizeof("PASS: split, coalesce, best fit and block reuse\n") - 1);
}

static void test_realloc_and_calloc(void)
{
	void *a = os_malloc(100);
	void *b = os_malloc(200);
	void *c = os_malloc(200);
	void *guard = os_malloc(64);
	assert(a && b && c && guard);

	memset(a, 0xa5, 100);
	os_free(b);
	os_free(c);
	void *grown = os_realloc(a, 490);
	assert(grown == a);
	for (size_t i = 0; i < 100; i++)
		assert(((unsigned char *)grown)[i] == 0xa5);

	void *shrunk = os_realloc(grown, 32);
	assert(shrunk == grown);
	for (size_t i = 0; i < 32; i++)
		assert(((unsigned char *)shrunk)[i] == 0xa5);

	void *mapped = os_realloc(shrunk, 200000);
	assert(mapped && meta(mapped)->status == STATUS_MAPPED);
	for (size_t i = 0; i < 32; i++)
		assert(((unsigned char *)mapped)[i] == 0xa5);

	void *back = os_realloc(mapped, 16);
	assert(back && meta(back)->status == STATUS_ALLOC);
	for (size_t i = 0; i < 16; i++)
		assert(((unsigned char *)back)[i] == 0xa5);
	assert(os_realloc(back, 0) == NULL);
	assert(os_realloc(back, 8) == NULL); /* Freed heap pointer: invalid. */

	void *zeroed = os_calloc(250, 4);
	assert(zeroed);
	for (size_t i = 0; i < 1000; i++)
		assert(((unsigned char *)zeroed)[i] == 0);
	os_free(zeroed);
	os_free(guard);
	write(STDOUT_FILENO, "PASS: realloc in-place, preserve data, mmap and calloc\n", sizeof("PASS: realloc in-place, preserve data, mmap and calloc\n") - 1);
}

static void test_overflow_and_null(void)
{
	assert(os_malloc(SIZE_MAX) == NULL);
	assert(os_calloc(SIZE_MAX, 2) == NULL);
	assert(os_calloc(2, SIZE_MAX) == NULL);
	assert(os_realloc(NULL, SIZE_MAX) == NULL);
	os_free(NULL);
	write(STDOUT_FILENO, "PASS: arithmetic overflow and NULL handling\n", sizeof("PASS: arithmetic overflow and NULL handling\n") - 1);
}

static uint32_t prng_state = 0x1234abcdU;

static uint32_t prng(void)
{
	prng_state ^= prng_state << 13;
	prng_state ^= prng_state >> 17;
	prng_state ^= prng_state << 5;
	return prng_state;
}

static void test_stress(void)
{
	enum { N = 128, STEPS = 18000 };
	unsigned char *ptr[N] = {0};
	size_t length[N] = {0};
	unsigned char stamp[N] = {0};

	for (size_t step = 0; step < STEPS; step++) {
		size_t i = prng() % N;
		uint32_t action = prng() % 3;
		if (ptr[i]) {
			for (size_t j = 0; j < length[i]; j++)
				assert(ptr[i][j] == stamp[i]);
		}
		if (!ptr[i]) {
			length[i] = 1 + (prng() % 10000);
			ptr[i] = os_malloc(length[i]);
			assert(ptr[i]);
			CHECK_ALIGNED(ptr[i]);
			stamp[i] = (unsigned char)prng();
			memset(ptr[i], stamp[i], length[i]);
		} else if (action == 0) {
			os_free(ptr[i]);
			ptr[i] = NULL;
			length[i] = 0;
		} else {
			size_t old = length[i];
			size_t new_len = 1 + (prng() % 14000);
			unsigned char *next = os_realloc(ptr[i], new_len);
			assert(next);
			for (size_t j = 0; j < (old < new_len ? old : new_len); j++)
				assert(next[j] == stamp[i]);
			ptr[i] = next;
			length[i] = new_len;
			memset(ptr[i], stamp[i], length[i]);
		}
	}

	for (size_t i = 0; i < N; i++) {
		if (ptr[i]) {
			for (size_t j = 0; j < length[i]; j++)
				assert(ptr[i][j] == stamp[i]);
			os_free(ptr[i]);
		}
	}
	write(STDOUT_FILENO, "PASS: 18,000 randomized allocation / free / realloc operations\n", sizeof("PASS: 18,000 randomized allocation / free / realloc operations\n") - 1);
}

int main(void)
{
	assert(sizeof(struct block_meta) == 32);
	test_first_allocation_and_thresholds();
	test_split_coalesce_best_fit();
	test_realloc_and_calloc();
	test_overflow_and_null();
	test_stress();
	write(STDOUT_FILENO, "ALL LOCAL TESTS PASSED\n", sizeof("ALL LOCAL TESTS PASSED\n") - 1);
	return 0;
}
