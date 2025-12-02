# Custom Memory Allocator (C)

This project implements a simple custom memory allocator in C, using both
`sbrk()` and `mmap()` depending on allocation size. It provides replacement
functions `memalloc()` and `memfree()` that work similarly to `malloc()` and
`free()`.

The allocator uses a linked list of memory blocks, splitting and coalescing
blocks to manage fragmentation.

---

## Features

### ✔ Supports two allocation modes
- **Small allocations (< 128 KB)** → allocated via `sbrk()`
- **Large allocations (≥ 128 KB)** → allocated via `mmap()`

### ✔ Block metadata stored in a header
Each block contains:
- size of the block  
- free/used flag  
- next pointer  
- allocation type (mmap or sbrk)

### ✔ Splitting free blocks
If a block is larger than required, it is split into:
- a used block  
- a new free block  

### ✔ Coalescing
Adjacent free blocks are merged to reduce fragmentation.