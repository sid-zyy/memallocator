#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../memallocator/memallocator.h"

#define NUM_ALLOCS 10000

double benchmark_malloc_free() {
    clock_t start = clock();
    
    for (int i = 0; i < NUM_ALLOCS; i++) {
        void *ptr = mymalloc(64);
        myfree(ptr);
    }
    
    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main() {
    printf("running benchmarks with %d allocations\n\n", NUM_ALLOCS);
    
    printf("benchmark 1: sequential malloc/free (64 bytes)\n");
    double t1 = benchmark_malloc_free();
    printf("time: %.4f seconds\n\n", t1);
}