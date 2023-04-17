#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the block and whether it is allocated (stored in the low bit) */
    size_t header;
    /**
     * We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allow us to obtain a
     * pointer to the start of the payload.
     */
    uint8_t payload[];
} block_t;

/** The first and last blocks on the heap */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}

/**
 * Coalesces the given block with all free blocks in line after it.
 */
static void coalesce_block(block_t *block) {
    // No blocks after heap last block, so nothing to coalesce
    if (block == mm_heap_last) {
        return;
    }

    size_t block_size = get_size(block);
    for (block_t *next = (void *) block + block_size; !is_allocated(next);
         next = (void *) block + block_size) {
        block_size += get_size(next);
        set_header(block, block_size, false);

        if (mm_heap_last == next) {
            mm_heap_last = block;
            return;
        }
    }
}

/**
 * Splits the given block into two blocks.
 * 1st allocated with size block_size; 2nd unallocated with remaining space.
 * Note: Assumes given block size is already 16-byte aligned.
 * If size of block already equals block_size, it just sets block to allocated.-
 */
static void split_block(block_t *block, size_t block_size) {
    size_t old_block_size = get_size(block);
    if (old_block_size == block_size) {
        set_header(block, block_size, true);
        return;
    }

    block_t *excess_block = (void *) block + block_size;
    size_t excess_block_size = old_block_size - block_size;
    set_header(excess_block, excess_block_size, false);
    if (mm_heap_last == block) {
        mm_heap_last = excess_block;
    }

    set_header(block, block_size, true);
}

/**
 * Finds best fitting free block with least amount of bytes to hold given size.
 * If no block is large enough, returns NULL.
 */
static block_t *find_fit(size_t size) {
    block_t *best = NULL;
    size_t smallest = 0;
    // Traverse the blocks in the heap using the implicit list
    for (block_t *curr = mm_heap_first; mm_heap_last != NULL && curr <= mm_heap_last;
         curr = (void *) curr + get_size(curr)) {
        // Checks that the block is free
        if (!is_allocated(curr)) {
            // Delayed coalesces curr block with all following free blocks
            coalesce_block(curr);

            size_t curr_size = get_size(curr);
            // If fit is perfect, skip all the checking and return that block
            if (curr_size == size) {
                return curr;
            }
            /* Compares all possible fits to find smallest fit
               for blocks larger than size. */
            if (curr_size > size) {
                if (best == NULL || (best != NULL && curr_size < smallest)) {
                    best = curr;
                    smallest = curr_size;
                }
            }
        }
    }
    return best;
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    // We want the first payload to start at ALIGNMENT bytes from the start of the heap
    void *padding = mem_sbrk(ALIGNMENT - sizeof(block_t));
    if (padding == (void *) -1) {
        return false;
    }

    // Initialize the heap with no blocks
    mm_heap_first = NULL;
    mm_heap_last = NULL;
    return true;
}

/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    // The block must have enough space for a header and be 16-byte aligned
    size = round_up(sizeof(block_t) + size, ALIGNMENT);

    // If there is a large enough free block, use it
    block_t *block = find_fit(size);
    if (block != NULL) {
        split_block(block, size);
        return block->payload;
    }

    // Otherwise, a new block needs to be allocated at the end of the heap
    block = mem_sbrk(size);
    if (block == (void *) -1) {
        return NULL;
    }

    // Update mm_heap_first and mm_heap_last since we extended the heap
    if (mm_heap_first == NULL) {
        mm_heap_first = block;
    }
    mm_heap_last = block;

    // Initialize the block with the allocated size
    set_header(block, size, true);
    return block->payload;
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }

    // Mark the block as unallocated
    block_t *block = block_from_payload(ptr);
    set_header(block, get_size(block), false);
}

/**
 * mm_realloc - Change the size of the block by mm_mallocing a new block,
 *      copying its data, and mm_freeing the old block.
 */
void *mm_realloc(void *old_ptr, size_t size) {
    // Edge case (unconventional) uses
    if (size == 0) {
        mm_free(old_ptr);
        return NULL;
    }
    else if (old_ptr == NULL) {
        return mm_malloc(size);
    }

    block_t *old_block = block_from_payload(old_ptr);
    size_t old_block_size = get_size(old_block);

    // The block must have enough space for a header and be 16-byte aligned
    size_t block_size = round_up(sizeof(block_t) + size, ALIGNMENT);

    // Asking for same amount of space
    if (old_block_size == block_size) {
        return old_ptr;
        // Asking for less space
    }
    else if (block_size < old_block_size) {
        split_block(old_block, block_size);
        return old_ptr;
        // Asking for more space
    }
    else {
        void *ptr = mm_malloc(size);
        if (ptr == NULL) {
            return NULL;
        }
        size_t old_payload_size = old_block_size - sizeof(block_t);
        memcpy(ptr, old_ptr, old_payload_size);
        mm_free(old_ptr);
        return ptr;
    }
}

/**
 * mm_calloc - Allocate the block and set it to zero.
 */
void *mm_calloc(size_t nmemb, size_t size) {
    void *ptr = mm_malloc(nmemb * size);
    if (ptr == NULL) {
        return NULL;
    }
    memset(ptr, 0, nmemb * size);
    return ptr;
}

/**
 * mm_checkheap - So simple, it doesn't need a checker!
 * Incomplete implementation that only checks the following if heap isn't empty
 *      - Checks that the first block is at the start of the heap
 *      - Checks that the size of every block is 16-byte aligned
 */
void mm_checkheap(void) {
    if (mm_heap_first == NULL) {
        return;
    }
    if (mm_heap_first != mem_heap_lo() + ALIGNMENT - sizeof(block_t)) {
        fprintf(stderr,
                "Heap first block is not at the start of the heap. "
                "Heap_lo: %p,  \t  Heap_first: %p\n",
                mem_heap_lo(), mm_heap_first);
    }
    block_t *second_to_last = NULL;
    block_t *curr = mm_heap_first;
    for (curr = mm_heap_first; mm_heap_last != NULL && curr <= mm_heap_last;
         curr = (void *) curr + get_size(curr), second_to_last = curr) {
        if (get_size(curr) % ALIGNMENT != 0) {
            fprintf(stderr,
                    "Size of the block is not 16-byte aligned. "
                    "Size: %zu\n",
                    get_size(curr));
        }
    }
}
