#include "osmem.h"

block_meta_t *head;
size_t MMAP_MINVALUE = MMAP_THRESHOLD;

void add_block(block_meta_t *block)
{
	block_meta_t *curr_block = head;

	while (curr_block->next)
		curr_block = curr_block->next;

	curr_block->next = block;
	block->prev = curr_block;
	block->next = NULL;
}

void remove_block(block_meta_t *block)
{
	if (block->prev)
		block->prev->next = block->next;

	if (block->next)
		block->next->prev = block->prev;

	if (block == head) {
		if (head->next) {
			head = block->next;
			head->prev = NULL;
		} else {
			head = NULL;
		}
	}
}

block_meta_t *find_free_block(size_t size)
// best-fit search heurisitcs
{
	block_meta_t *curr_block = head;
	block_meta_t *best_block = NULL;

	while (curr_block) {
		if (curr_block->status == STATUS_FREE && curr_block->size >= size
			&& (!best_block || best_block->size > curr_block->size)) {
			best_block = curr_block;
		}
		curr_block = curr_block->next;
	}
	return best_block;
}

block_meta_t *expand_block(size_t size)
{
	block_meta_t *curr_block = head;

	while (curr_block->next)
		curr_block = curr_block->next;

	if (curr_block->status != STATUS_FREE)
		return NULL;

	assert(curr_block->size < size);
	sbrk(size - curr_block->size);
	curr_block->status = STATUS_ALLOC;
	curr_block->size = size;
	return curr_block;
}


void split_block(block_meta_t *block, int size)
{
	if (block->size > META_SIZE + size) {
		block_meta_t *next_block = (block_meta_t *)(((char *)block) + META_SIZE + size);

		next_block->size = block->size - (META_SIZE + size);
		next_block->status = STATUS_FREE;
		next_block->next = block->next;
		next_block->prev = block;
		if (block->next)
			block->next->prev = next_block;

		block->next = next_block;
		block->size = size;
	}
	block->status = STATUS_ALLOC;
}

void coalesce(block_meta_t *curr_block)
{
	if (!curr_block || !curr_block->next)
		return;

	block_meta_t *next_block = curr_block->next;

	if (curr_block->status != STATUS_FREE || next_block->status != STATUS_FREE)
		return;
	curr_block->size += META_SIZE + next_block->size;
	remove_block(next_block);
}

block_meta_t *find_block_by_ptr(void *ptr)
{
	block_meta_t *curr_block = head;

	while (curr_block && (void *)(curr_block + 1) != ptr)
		curr_block = curr_block->next;

	return curr_block;
}


block_meta_t *request(size_t size)
{
	static int first_brk;
	block_meta_t *block;
	size_t block_size = META_SIZE + size;

	if (block_size <= MMAP_MINVALUE) {
		block = sbrk(!first_brk ? MMAP_THRESHOLD : block_size);
		block->status = STATUS_ALLOC;
		block->next = NULL;
		block->prev = NULL;
		if (!first_brk) {
			first_brk = 1;
			if (MMAP_THRESHOLD - META_SIZE - size > META_SIZE) {
				block_meta_t *next_block = (block_meta_t *)(((char *)block) + META_SIZE + size);

				block->next = next_block;
				next_block->prev = block;
				next_block->next = NULL;
				next_block->status = STATUS_FREE;
				next_block->size = MMAP_THRESHOLD - size - META_SIZE;
			}
		}
		DIE(block == (void *) -1, "sbrk() allocation failed");
	} else {
		block = mmap(NULL, block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		block->status = STATUS_MAPPED;
		block->next = NULL;
		block->prev = NULL;
		DIE(block == MAP_FAILED, "mmap() allocation failed");
	}
	block->size = size;
	return block;
}


void *os_malloc(size_t size)
{
	if (!size)
		return NULL;

	size = ALIGN(size);
	if (head == NULL) {
		head = request(size);
		return head + 1;
	}
	if (size + META_SIZE >= MMAP_MINVALUE) {
		block_meta_t *requested_block = request(size);

		add_block(requested_block);
		return requested_block + 1;
	}
	block_meta_t *free_block = find_free_block(size);

	if (free_block) {
		split_block(free_block, size);
		return free_block + 1;
	}
	block_meta_t *block = expand_block(size);

	if (block)
		return block + 1;

	block_meta_t *requested_block = request(size);

	add_block(requested_block);
	return requested_block + 1;
}


void os_free(void *ptr)
{
	if (!ptr)
		return;

	block_meta_t *curr_block = find_block_by_ptr(ptr);

	if (!curr_block)
		return;


	if (curr_block->status == STATUS_MAPPED) {
		remove_block(curr_block);
		int result = munmap(curr_block, curr_block->size + META_SIZE);

		DIE(result == -1, "mumap() deallocation failed");
		return;
	}
	assert(curr_block->status == STATUS_ALLOC);
	curr_block->status = STATUS_FREE;
	coalesce(curr_block);
	coalesce(curr_block->prev);
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (!nmemb || !size)
		return NULL;


	MMAP_MINVALUE = getpagesize();
	void *ptr = os_malloc(nmemb * size);

	MMAP_MINVALUE = MMAP_THRESHOLD;

	memset(ptr, 0, nmemb * size);
	return ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);

	if (!size) {
		os_free(ptr);
		return NULL;
	}
	size = ALIGN(size);
	block_meta_t *curr_block = find_block_by_ptr(ptr);

	if (curr_block->status == STATUS_FREE)
		return NULL;

	if (curr_block->status == STATUS_MAPPED) {
		void *new_ptr = os_malloc(size);

		memcpy(new_ptr, ptr, MIN(size, curr_block->size));
		os_free(ptr);
		return new_ptr;
	}
	assert(curr_block->status == STATUS_ALLOC);
	if (curr_block->size == size)
		return ptr;

	if (curr_block->size > size) {
		split_block(curr_block, size);
		return ptr;
	}
	assert(size > curr_block->size);

	if (!curr_block->next) {
		sbrk(size - curr_block->size);
		curr_block->size = size;
		return ptr;
	}

	if (curr_block->next->status == STATUS_FREE) {
		if (curr_block->next->size + META_SIZE >= size - curr_block->size) {
			if (size <= curr_block->size + META_SIZE) {
				remove_block(curr_block->next);
			} else {
				split_block(curr_block->next, size - curr_block->size - META_SIZE);
				curr_block->size += curr_block->next->size + META_SIZE;
				remove_block(curr_block->next);
			}
			return ptr;
		}
	}

	if (!find_free_block(size) && size + META_SIZE <= MMAP_THRESHOLD) {
		block_meta_t *last_block = NULL;
		block_meta_t *aux_block = head;

		while (aux_block->next) {
			if (aux_block->status != STATUS_MAPPED)
				last_block = aux_block;
			aux_block = aux_block->next;
		}
		if (last_block->status == STATUS_FREE) {
			last_block->status = STATUS_ALLOC;
			void *new_ptr = sbrk(size - last_block->size + META_SIZE);
			(void) new_ptr;
			last_block->size = size;
			memcpy(last_block + 1, ptr, curr_block->size);
			os_free(ptr);
			return last_block + 1;
		}
	}

	void *new_ptr = os_malloc(size);

	memcpy(new_ptr, ptr, curr_block->size);
	os_free(ptr);
	return new_ptr;
}
