#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the previous block and whether it is allocated (stored in low bit) */
    size_t prev_footer;
    /** The size of the current block and whether it is allocated (stored in low bit) */
    size_t header;
    /**
     * We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allow us to obtain a
     * pointer to the start of the payload.
     */
    uint8_t payload[];
} block_t;

typedef struct free_node {
    struct free_node *prev;
    struct free_node *next;
} free_node_t;

// 8 bytes used
static free_node_t *mm_free_list_root = NULL;

/* BASIC FUNCTIONS */

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

// Gets the needed size of a block from a given payload
static size_t get_block_size_from_payload_size(size_t payload_size) {
    return round_up(sizeof(block_t) + payload_size, ALIGNMENT);
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}

// Extracts the blocks free node (has equivalent memory placement to payload)
static free_node_t *get_free_node_from_block(block_t *block) {
    return (free_node_t *) ((void *) block + ALIGNMENT);
}

/**
 * Gets the next block after the given block
 * Returns NULL if there is no next block (next->header = 0)
 */
static block_t *get_next_block(block_t *block) {
    block_t *next = (void *) block + get_size(block);
    if (next->header == 0) {
        return NULL;
    }
    return next;
}

/**
 * Gets the previous block before the given block.
 * Returns NULL if there is no previous block (prev_footer = 0)
 */
static block_t *get_prev_block(block_t *block) {
    if (block->prev_footer == 0) {
        return NULL;
    }
    size_t prev_block_size = block->prev_footer & ~1;
    return (block_t *) ((void *) block - prev_block_size);
}

/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
}

/**
 * Set's a block's footer with the given size and allocation state
 * Note: Goes past the current block and into the "next block" to do this.
 */
static void set_footer(block_t *block, size_t size, bool is_allocated) {
    size_t *footer = (void *) block + size;
    *footer = size | is_allocated;
}

/* NODE LIST FUNCTIONS */

/**
 * Adds a node to at the start of its corresponding segregated list.
 */
static void add_node(block_t *block) {
    if (block == NULL) {
        return;
    }

    free_node_t *node = get_free_node_from_block(block);
    node->next = NULL;
    node->prev = NULL;

    if (mm_free_list_root == NULL) {
        mm_free_list_root = node;
        return;
    }

    node->next = mm_free_list_root;
    mm_free_list_root->prev = node;
    mm_free_list_root = node;
}

/**
 * Removes a block's free node from the list of nodes by splicing
 * the node before and after.
 */
static void remove_node(block_t *block) {
    if (block == NULL) {
        return;
    }
    free_node_t *node = get_free_node_from_block(block);

    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
    if (node->prev != NULL) {
        node->prev->next = node->next;
    }
    else {
        mm_free_list_root = node->next;
    }
}

/** KEY MM HELPER FUNCTIONS */

/**
 * Sets up a free block by coalescing it with any adjacent free blocks
 * and adding it at the start (FIFO) of its corresponding segregated list.
 */
static void set_up_new_free_block(block_t *block) {
    size_t block_size = get_size(block);
    block_t *next_block = get_next_block(block);
    block_t *prev_block = get_prev_block(block);

    // If the block after is free, prepares to coalesce it
    if (next_block != NULL && !is_allocated(next_block)) {
        block_size += get_size(next_block);
        remove_node(next_block);
    }

    // If the block before is free, prepares to coalesce it
    if (prev_block != NULL && !is_allocated(prev_block)) {
        block_size += get_size(prev_block);
        remove_node(prev_block);

        // New start of block is prev_block since it comes before in memory
        block = prev_block;
    }

    set_header(block, block_size, false);
    set_footer(block, block_size, false);
    add_node(block);
}

/**
 * Splits the given block into two blocks.
 * 1st allocated with size block_size; 2nd unallocated with remaining space.
 * Note: Assumes given block size is already 16-byte aligned.
 * If size of block already equals block_size, it just sets block to allocated.
 */
static void split_block(block_t *block, size_t block_size) {
    size_t old_block_size = get_size(block);

    // Ensures there's enough space for the new block plus an empty free block
    if (old_block_size < block_size + 2 * ALIGNMENT) {
        set_header(block, old_block_size, true);
        set_footer(block, old_block_size, true);
        return;
    }

    // Sets block as allocated with new size
    set_header(block, block_size, true);
    set_footer(block, block_size, true);

    // If size already equals desired size, nothing to split, so returns
    if (old_block_size == block_size) {
        return;
    }

    // Makes new free block from excess space
    block_t *excess_block = (void *) block + block_size;
    size_t excess_block_size = old_block_size - block_size;
    set_header(excess_block, excess_block_size, false);
    set_footer(excess_block, excess_block_size, false);
    add_node(excess_block);
}

/**
 * Finds best fitting free block with least amount of bytes to hold given size.
 * If no block is large enough, returns NULL.
 */
static block_t *find_fit(size_t block_size) {
    block_t *best = NULL;
    size_t smallest = 0;

    for (free_node_t *node = mm_free_list_root; node != NULL; node = node->next) {
        block_t *block = block_from_payload(node);

        size_t curr_size = get_size(block);

        // If fit is perfect, skips all the checking and return that block
        if (curr_size == block_size) {
            remove_node(block);
            return block;
        }

        /* Compares all possible fits to find smallest fit
            for blocks larger than size. */
        if (curr_size > block_size) {
            if (best == NULL || (best != NULL && curr_size < smallest)) {
                best = block;
                smallest = curr_size;
            }
        }
    }
    if (best != NULL) {
        remove_node(best);
    }

    return best;
}

/** MM LIBRARY FUNCTIONS */

/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    mm_free_list_root = NULL;

    // Initializes heap with border footer and header setting them equal to 0
    size_t *border_start_footer = mem_sbrk(sizeof(size_t));
    size_t *border_end_header = mem_sbrk(sizeof(size_t));
    if (border_start_footer == (void *) -1 || border_end_header == (void *) -1) {
        return false;
    }

    // Setting them equal to 0 marks them as the borders for the heap
    *border_start_footer = 0;
    *border_end_header = 0;
    return true;
}

/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    size = get_block_size_from_payload_size(size);

    // If there is a large enough free block, use it
    block_t *block = find_fit(size);
    if (block != NULL) {
        split_block(block, size);
        return block->payload;
    }

    // Otherwise, a new block needs to be allocated at the end of the heap
    void *payload_ptr = mem_sbrk(size);
    if (payload_ptr == (void *) -1) {
        return NULL;
    }

    // Goes back to memory previously allocated for footer and header
    block = payload_ptr - ALIGNMENT;

    // Gets border header (at header of next block) and sets it to 0
    size_t *border_end_header = payload_ptr + size - sizeof(size_t);
    *border_end_header = 0;

    // Sets the header and footer for our newly allocated block
    set_header(block, size, true);
    set_footer(block, size, true);

    return payload_ptr;
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }

    // Sets up the new block by coalescing and adding it to the free lists
    block_t *block = block_from_payload(ptr);
    set_up_new_free_block(block);
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

    // Must have enough space for a header and be 16-byte aligned
    size_t block_size = get_block_size_from_payload_size(size);

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
 * Incomplete implementation that only checks...
 *      - That the header and footer borders are 0
 *      - That the header and footer for each block are equal
 */
void mm_checkheap(void) {
    void *heap_end = mem_heap_lo() + ALIGNMENT;
    block_t *first_block = (block_t *) mem_heap_lo();
    block_t *next_block = (void *) first_block + get_size(first_block);
    if (first_block->prev_footer != 0) {
        fprintf(stderr,
                "ERROR: First Block Prev Footer (should be Border Start Footer) doesn't "
                "equal 0.\n");
        return;
    }
    for (block_t *block = first_block; block->header != 0;
         block = next_block, next_block = (void *) block + get_size(block)) {
        if (block->header != next_block->prev_footer) {
            fprintf(stderr, "ERROR: Block header and footer aren't equal.\n");
            return;
        }
        if (next_block->header == 0) {
            heap_end = &next_block->header + sizeof(size_t *);
        }
    }
}
