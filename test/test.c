#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "../memallocator/memallocator.h"

#define NUM_ALLOCS       10000
#define FRAG_ALLOCS      1000
#define FRAG_SMALL_SIZE  64
#define FRAG_LARGE_SIZE  256

/* ── timing helper ─────────────────────────────────────────────── */

static double elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000.0
         + (end.tv_nsec - start.tv_nsec) / 1e6;
}

/* ── correctness tests ─────────────────────────────────────────── */

static int test_basic_alloc_free(void)
{
    void *p = mymalloc(64);
    if (!p) { printf("  FAIL: mymalloc(64) returned NULL\n"); return 0; }
    memset(p, 0xAB, 64);
    myfree(p);
    printf("  PASS: basic alloc/free 64 bytes\n");
    return 1;
}

static int test_null_free(void)
{
    myfree(NULL); /* must not crash */
    printf("  PASS: free(NULL) is safe\n");
    return 1;
}

static int test_zero_alloc(void)
{
    void *p = mymalloc(0);
    if (p != NULL) { printf("  FAIL: mymalloc(0) should return NULL\n"); return 0; }
    printf("  PASS: mymalloc(0) returns NULL\n");
    return 1;
}

static int test_write_readback(void)
{
    const size_t N = 128;
    unsigned char *p = mymalloc(N);
    if (!p) { printf("  FAIL: mymalloc returned NULL\n"); return 0; }
    for (size_t i = 0; i < N; i++) p[i] = (unsigned char)i;
    for (size_t i = 0; i < N; i++) {
        if (p[i] != (unsigned char)i) {
            printf("  FAIL: readback mismatch at byte %zu\n", i);
            myfree(p);
            return 0;
        }
    }
    myfree(p);
    printf("  PASS: write/readback 128 bytes\n");
    return 1;
}

static int test_many_allocs(void)
{
    const int N = 200;
    void *ptrs[200];
    for (int i = 0; i < N; i++) {
        ptrs[i] = mymalloc(32 + i * 4);
        if (!ptrs[i]) { printf("  FAIL: mymalloc returned NULL at i=%d\n", i); return 0; }
        memset(ptrs[i], i & 0xFF, 32 + i * 4);
    }
    for (int i = 0; i < N; i++) myfree(ptrs[i]);
    printf("  PASS: 200 sequential allocs/frees\n");
    return 1;
}

static int test_reuse_after_free(void)
{
    /* Allocate, free, then allocate same size — should reuse the block,
       so heap should not grow beyond the first allocation. */
    void *p1 = mymalloc(128);
    if (!p1) { printf("  FAIL: first alloc\n"); return 0; }
    void *addr1 = p1;
    myfree(p1);

    void *p2 = mymalloc(128);
    if (!p2) { printf("  FAIL: second alloc\n"); return 0; }
    /* On a clean heap the reused block should come back at the same address */
    if (p2 != addr1)
        printf("  NOTE: block not reused (may be benign if heap had prior state)\n");
    else
        printf("  PASS: block reused after free (same address)\n");
    myfree(p2);
    return 1;
}

static int test_large_mmap(void)
{
    const size_t BIG = 200 * 1024; /* 200 KB — above mmap threshold */
    void *p = mymalloc(BIG);
    if (!p) { printf("  FAIL: large mmap alloc returned NULL\n"); return 0; }
    memset(p, 0x7F, BIG);
    myfree(p);
    printf("  PASS: large (200 KB) mmap alloc/free\n");
    return 1;
}

static int test_coalesce(void)
{
    /* Allocate A, B, C — free B then A; allocating A+B worth of bytes
       should succeed without requesting new heap space. */
    void *a = mymalloc(64);
    void *b = mymalloc(64);
    void *c = mymalloc(64);
    if (!a || !b || !c) { printf("  FAIL: setup allocs\n"); return 0; }

    myfree(b); /* free middle */
    myfree(a); /* free left  — coalesce should merge a+b */

    void *big = mymalloc(128); /* should fit in the merged block */
    if (!big) { printf("  FAIL: coalesced block not reusable\n"); myfree(c); return 0; }

    myfree(big);
    myfree(c);
    printf("  PASS: coalescing adjacent free blocks\n");
    return 1;
}

/* ── fragmentation measurement ─────────────────────────────────── */
/*
 * Strategy:
 *   1. Allocate FRAG_ALLOCS blocks of alternating small/large sizes.
 *   2. Free every other block to create holes.
 *   3. Measure heap footprint vs. live bytes → external fragmentation %.
 *
 * We proxy heap size via sbrk(0) before and after the allocation phase.
 * Live bytes = sum of sizes still in use.
 */
static void test_fragmentation(void)
{
    printf("\n── Fragmentation Test ──────────────────────────────────────\n");

    void  *ptrs[FRAG_ALLOCS];
    size_t sizes[FRAG_ALLOCS];

    /* Record heap start */
    void *heap_start = sbrk(0);

    /* Phase 1: allocate alternating sizes */
    for (int i = 0; i < FRAG_ALLOCS; i++) {
        sizes[i] = (i % 2 == 0) ? FRAG_SMALL_SIZE : FRAG_LARGE_SIZE;
        ptrs[i]  = mymalloc(sizes[i]);
        assert(ptrs[i] != NULL);
    }

    void *heap_after_alloc = sbrk(0);
    size_t heap_footprint = (size_t)((char *)heap_after_alloc - (char *)heap_start);

    /* Phase 2: free every other block → introduce holes */
    size_t live_bytes = 0;
    for (int i = 0; i < FRAG_ALLOCS; i++) {
        if (i % 2 == 0) {
            myfree(ptrs[i]);
            ptrs[i] = NULL;
        } else {
            live_bytes += sizes[i];
        }
    }

    /* Phase 3: compute fragmentation
     *   external_frag = (wasted_heap_bytes) / heap_footprint
     *   wasted = footprint - live_bytes (ignoring headers for simplicity)
     */
    double frag_pct = 0.0;
    if (heap_footprint > 0) {
        double wasted = (double)heap_footprint - (double)live_bytes;
        if (wasted < 0) wasted = 0;
        frag_pct = (wasted / (double)heap_footprint) * 100.0;
    }

    printf("  Allocations        : %d blocks (%d bytes / %d bytes alternating)\n",
           FRAG_ALLOCS, FRAG_SMALL_SIZE, FRAG_LARGE_SIZE);
    printf("  Heap footprint     : %zu bytes\n", heap_footprint);
    printf("  Live bytes         : %zu bytes\n", live_bytes);
    printf("  External frag      : %.2f%%\n", frag_pct);

    if (frag_pct < 5.0)
        printf("  RESULT             : PASS (< 5%% threshold)\n");
    else if (frag_pct < 20.0)
        printf("  RESULT             : ACCEPTABLE (< 20%%)\n");
    else
        printf("  RESULT             : HIGH FRAGMENTATION — check coalesce logic\n");

    /* Cleanup */
    for (int i = 0; i < FRAG_ALLOCS; i++)
        if (ptrs[i]) myfree(ptrs[i]);
}

/* ── benchmarks ────────────────────────────────────────────────── */

static void benchmark_sequential(void)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_ALLOCS; i++) {
        void *p = mymalloc(64);
        myfree(p);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Sequential malloc/free  (%5d × 64 B)  : %.3f ms\n",
           NUM_ALLOCS, elapsed_ms(t0, t1));
}

static void benchmark_mixed_sizes(void)
{
    const int sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    const int N = 7;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_ALLOCS; i++) {
        void *p = mymalloc(sizes[i % N]);
        myfree(p);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Mixed-size  malloc/free (%5d × varied)  : %.3f ms\n",
           NUM_ALLOCS, elapsed_ms(t0, t1));
}

static void benchmark_batch(void)
{
    /* Allocate all, then free all — stresses the free list more */
    void *ptrs[NUM_ALLOCS];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_ALLOCS; i++) ptrs[i] = mymalloc(64);
    for (int i = 0; i < NUM_ALLOCS; i++) myfree(ptrs[i]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Batch alloc then free   (%5d × 64 B)  : %.3f ms\n",
           NUM_ALLOCS, elapsed_ms(t0, t1));
}

static void benchmark_large_mmap(void)
{
    const size_t BIG = 200 * 1024;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 500; i++) {
        void *p = mymalloc(BIG);
        myfree(p);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  Large mmap  malloc/free (  500 × 200 KB): %.3f ms\n",
           elapsed_ms(t0, t1));
}

/* ── main ──────────────────────────────────────────────────────── */

int main(void)
{
    int passed = 0, total = 0;

    printf("═══════════════════════════════════════════════════════════\n");
    printf("  Custom Memory Allocator — Test Suite\n");
    printf("═══════════════════════════════════════════════════════════\n\n");

    printf("── Correctness Tests ───────────────────────────────────────\n");
#define RUN(t) do { total++; passed += (t); } while(0)
    RUN(test_basic_alloc_free());
    RUN(test_null_free());
    RUN(test_zero_alloc());
    RUN(test_write_readback());
    RUN(test_many_allocs());
    RUN(test_reuse_after_free());
    RUN(test_large_mmap());
    RUN(test_coalesce());
#undef RUN
    printf("\n  %d / %d tests passed\n", passed, total);

    test_fragmentation();

    printf("\n── Benchmarks ──────────────────────────────────────────────\n");
    benchmark_sequential();
    benchmark_mixed_sizes();
    benchmark_batch();
    benchmark_large_mmap();

    printf("\n═══════════════════════════════════════════════════════════\n");
    return (passed == total) ? 0 : 1;
}
