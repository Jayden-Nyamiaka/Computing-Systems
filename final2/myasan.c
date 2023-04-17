#define _GNU_SOURCE 1

#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "util.h"

static const size_t PAGE_SIZE = 4096;
typedef uint8_t page_t[PAGE_SIZE];

static void *const START_PAGE = (void *) ((size_t) 1 << 32);
static const size_t MAX_HEAP_SIZE = (size_t) 1 << 30;
static const int HEAP_MMAP_FLAGS = MAP_ANONYMOUS | MAP_PRIVATE;
static const size_t HEADER_MAGIC = 0x0123456789ABCDEF;

typedef struct {
    size_t magic;
    size_t size;
    bool is_allocated;
} header_t;

static bool is_initialized = false;
static page_t *current_page;

static size_t pages_round_up(size_t size) {
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

static void set_header(page_t *header_page, size_t size, bool is_allocated) {
    // Task 5
    assert(0 == mprotect((void *) header_page, sizeof(page_t), PROT_WRITE | PROT_READ));

    // Task 1
    header_t *head = (header_t *) header_page;
    head->magic = HEADER_MAGIC;
    head->size = size;
    head->is_allocated = is_allocated;

    // Task 5
    assert(0 == mprotect((void *) header_page, sizeof(page_t), PROT_NONE));
}

static void *get_payload(page_t *header_page, size_t size) {
    // Task 1
    size_t allocation_size = (pages_round_up(size) + 1) * sizeof(page_t);
    return (void *) header_page + allocation_size - size;
}

static void check_for_leaks(void) {
    // Prevent memory leaks from stdout
    fclose(stdout);

    // Task 4
    header_t *head = (header_t *) START_PAGE;
    while ((void *) head < (void *) current_page) {
        // Task 5
        assert(0 == mprotect((void *) head, sizeof(page_t), PROT_WRITE | PROT_READ));

        // Task 4
        if (head->is_allocated) {
            report_memory_leak(get_payload((page_t *) head, head->size), head->size);
        }
        void *next = (void *) head + (pages_round_up(head->size) + 1) * sizeof(page_t);

        // Task 5
        assert(0 == mprotect((void *) head, sizeof(page_t), PROT_NONE));
        head = next;
    }
}

/* Task 5 */
static void sigsegv_handler(int signum, siginfo_t *siginfo, void *context) {
    (void) signum;
    (void) context;

    void *accessed = (void *) siginfo->si_addr;
    if (accessed < START_PAGE || accessed >= (void *) (current_page + 1)) {
        report_seg_fault(accessed);
        return;
    }
    report_invalid_heap_access(accessed);
}

static void asan_init(void) {
    if (is_initialized) {
        return;
    }

    // Task 5
    struct sigaction act;
    act.sa_sigaction = sigsegv_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);

    // Avoid buffering on stdout
    setbuf(stdout, NULL);

    current_page = mmap(START_PAGE, MAX_HEAP_SIZE,
                        PROT_NONE, // Task 5
                        HEAP_MMAP_FLAGS, -1, 0);
    assert(current_page == START_PAGE);

    atexit(check_for_leaks);

    is_initialized = true;
}

void *malloc(size_t size) {
    asan_init();

    size_t pages_necessary = pages_round_up(size);

    // Store the size of the allocation at the beginning of the page before the payload
    page_t *header_page = current_page;
    set_header(header_page, size, true);
    current_page += 1 + pages_necessary;

    // Task 5
    assert(0 == mprotect((void *) header_page + sizeof(page_t),
                         pages_necessary * sizeof(page_t), PROT_WRITE | PROT_READ));

    // Provide the user with the END of the first page
    return get_payload(header_page, size);
}

void free(void *ptr) {
    asan_init();

    if (ptr == NULL) {
        return;
    }

    // Task 2
    header_t *head =
        (header_t *) (ptr - ((size_t) ptr % sizeof(page_t)) - sizeof(page_t));

    // Task 5
    mprotect((void *) head, sizeof(page_t),
             PROT_WRITE | PROT_READ); // no assert bc of error case

    // Task 3
    if ((void *) head < START_PAGE || (void *) head > (void *) current_page ||
        head->magic != HEADER_MAGIC || get_payload((void *) head, head->size) != ptr) {
        report_invalid_free(ptr);
    }
    // Task 2
    if (!head->is_allocated) {
        report_double_free(ptr, head->size);

        // Task 5
        assert(0 == mprotect((void *) head, sizeof(page_t), PROT_NONE));

        return;
    }
    head->is_allocated = false;

    // Task 5
    assert(0 == mprotect((void *) head, (pages_round_up(head->size) + 1) * sizeof(page_t),
                         PROT_NONE));
}
