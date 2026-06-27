# Custom Memory Allocator (C)

A from-scratch implementation of `malloc` and `free` in C using `sbrk` and `mmap`, replicating core glibc behavior. Built to understand allocator internals at the system call level.

---

## How it works

The allocator maintains a **singly-linked free list** of block headers on the heap. Each header stores the block size, a free/used flag, a next pointer, and whether the block was `sbrk`- or `mmap`-allocated.

**Allocation path:**

- `size < 128 KB` → `sbrk()` extends the heap; the free list is searched first (first-fit), and any oversized block is **split** so the remainder stays usable
- `size ≥ 128 KB` → `mmap(MAP_ANONYMOUS)` allocates directly from the kernel; `munmap` on free returns it immediately, avoiding heap bloat

**Fragmentation control:**

- **Splitting** — when a free block is larger than needed, it is divided into an exact-fit block and a new free block for the remainder
- **Coalescing** — on every `myfree()`, adjacent free blocks (both forward and backward) are merged into a single larger block

This mirrors the design of glibc's ptmalloc, including the 128 KB `mmap` threshold.

---

## Build & run

```bash
# compile
gcc -O2 -o test_allocator memallocator/memallocator.c test/test_allocator.c

# run tests + benchmarks
./test_allocator
```

---

## Test results

```
── Correctness Tests ───────────────────────────────────────
  PASS: basic alloc/free 64 bytes
  PASS: free(NULL) is safe
  PASS: mymalloc(0) returns NULL
  PASS: write/readback 128 bytes
  PASS: 200 sequential allocs/frees
  PASS: block reused after free (same address)
  PASS: large (200 KB) mmap alloc/free
  PASS: coalescing adjacent free blocks

  8 / 8 tests passed

── Fragmentation Test ──────────────────────────────────────
  Allocations        : 1000 blocks (64 / 256 bytes alternating)
  Heap footprint     : 92752 bytes
  Live bytes         : 128000 bytes
  External frag      : 0.00%
  RESULT             : PASS (< 5% threshold)

── Benchmarks ──────────────────────────────────────────────
  Sequential malloc/free  (10000 × 64 B)   : 0.050 ms
  Mixed-size  malloc/free (10000 × varied)  : 0.054 ms
  Batch alloc then free   (10000 × 64 B)   : 193 ms
  Large mmap  malloc/free (500   × 200 KB) : 2.6 ms
```

The batch benchmark (alloc all → free all) is slower by design: `find_free` is O(n) over the free list, so 10K live blocks means 10K walks per allocation. A segregated free list would fix this — left as a future improvement.

---

## Files

```
memallocator/
  memallocator.h   — public API (mymalloc, myfree)
  memallocator.c   — allocator implementation
test/
  test_allocator.c — correctness tests, fragmentation benchmark, timing benchmarks
```

---

## Known limitations

- **O(n) allocation** — first-fit over a linked list; degrades with many live blocks
- **No thread safety** — `free_list` is a global; concurrent use requires locking
- **No boundary tags** — coalescing requires a full forward traversal to find the predecessor; a doubly-linked list or footer tags would make it O(1)

---

## References

- [glibc malloc internals](https://sourceware.org/glibc/wiki/MallocInternals)
- K&R C §8.7 — A Storage Allocator
