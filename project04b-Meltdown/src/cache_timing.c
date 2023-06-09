#include "cache_timing.h"

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

const size_t REPEATS = 100000;

int main() {
    uint64_t sum_miss = 0;
    uint64_t sum_hit = 0;

    page_t *page = calloc(1, PAGE_SIZE);

    for (size_t i = 0; i < REPEATS; i++) {
        flush_cache_line((void *) page);
        uint64_t miss = time_read(page);
        uint64_t hit = time_read(page);
        if (hit >= miss) {
            continue;
        }
        sum_miss += miss;
        sum_hit += hit;
    }

    printf("average miss = %" PRIu64 "\n", sum_miss / REPEATS);
    printf("average hit  = %" PRIu64 "\n", sum_hit / REPEATS);
}
