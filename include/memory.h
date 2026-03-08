/*
 * ForgeOS - Custom Memory Allocator
 * include/memory.h
 *
 * Implements a free-list allocator with block headers, coalescing,
 * and detailed allocation statistics for diagnostics.
 */

#ifndef FORGE_MEMORY_H
#define FORGE_MEMORY_H

#include "forgeos.h"

/* ── Configuration ───────────────────────────────────────────────────────── */
#define FORGE_HEAP_SIZE        (16 * 1024 * 1024)   /* 16 MB static heap    */
#define FORGE_MIN_BLOCK_SIZE   32                    /* min alloc granularity */
#define FORGE_ALIGNMENT        16                    /* byte alignment        */
#define FORGE_MAGIC_FREE       0xDEADBEEF
#define FORGE_MAGIC_ALLOC      0xCAFEBABE

/* Block layout is internal to memory.c — not exposed in this header */

/* ── Statistics ──────────────────────────────────────────────────────────── */
typedef struct {
    size_t   total_allocated;       /* bytes currently allocated             */
    size_t   total_freed;           /* cumulative bytes freed                */
    size_t   peak_usage;            /* high-water mark                       */
    uint64_t alloc_count;           /* number of successful allocations      */
    uint64_t free_count;            /* number of frees                       */
    uint64_t realloc_count;
    size_t   heap_size;             /* total heap capacity                   */
    size_t   fragmented_bytes;      /* wasted bytes in free blocks < min     */
    int      block_count;           /* total blocks (free + allocated)       */
} forge_mem_stats_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Lifecycle */
int   forge_mem_init(void);
void  forge_mem_destroy(void);

/* Core allocators (use macros below in normal code) */
void *forge_malloc_impl(size_t size, const char *file, int line);
void *forge_calloc_impl(size_t nmemb, size_t size, const char *file, int line);
void *forge_realloc_impl(void *ptr, size_t new_size, const char *file, int line);
void  forge_free_impl(void *ptr, const char *file, int line);

/* Debug-tracked macros */
#define forge_malloc(size)          forge_malloc_impl(size,  __FILE__, __LINE__)
#define forge_calloc(n, size)       forge_calloc_impl(n, size, __FILE__, __LINE__)
#define forge_realloc(ptr, size)    forge_realloc_impl(ptr, size, __FILE__, __LINE__)
#define forge_free(ptr)             forge_free_impl(ptr, __FILE__, __LINE__)

/* Diagnostics */
forge_mem_stats_t  forge_mem_get_stats(void);
void               forge_mem_print_stats(void);
void               forge_mem_dump_blocks(void);
int                forge_mem_check_integrity(void);
double             forge_mem_utilization(void);

#endif /* FORGE_MEMORY_H */
