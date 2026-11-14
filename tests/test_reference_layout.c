/* SPDX-License-Identifier: BSD-3-Clause */
#include "osmem.h"

#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#define SAY(s) ((void)write(STDOUT_FILENO, s, sizeof(s) - 1))

static void check_offset(void *base, void *ptr, uintptr_t offset)
{
	assert(ptr);
	assert((uintptr_t)ptr - (uintptr_t)base == offset);
}

int main(void)
{
	/* Corresponds to the upstream test-malloc-arrays.ref beginning. */
	void *base = os_malloc(131032);
	assert(base);

	const size_t sizes[] = {10, 25, 40, 80, 160, 350, 421, 633, 1000, 2024, 4000};
	const uintptr_t offsets[] = {
		0x20000, 0x20030, 0x20070, 0x200b8, 0x20128,
		0x201e8, 0x20368, 0x20530, 0x207d0, 0x20bd8, 0x213e0
	};
	void *ptrs[sizeof(sizes) / sizeof(sizes[0])];

	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		ptrs[i] = os_malloc(sizes[i]);
		check_offset(base, ptrs[i], offsets[i]);
	}

	/* Upstream test-malloc-block-reuse.ref reuses these exact addresses. */
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		os_free(ptrs[i]);
		void *replacement = os_malloc(sizes[i]);
		assert(replacement == ptrs[i]);
		ptrs[i] = replacement;
	}

	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
		os_free(ptrs[i]);
	os_free(base);

	SAY("PASS: upstream reference payload offsets and block reuse\n");
	return 0;
}
