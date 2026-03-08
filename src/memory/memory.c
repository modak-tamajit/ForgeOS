/*
 * ForgeOS - Custom Memory Allocator
 * src/memory/memory.c
 *
 * Free-list allocator over a static 16 MB heap.
 * Supports coalescing, split on alloc, and integrity checks.
 *
 * Architecture:
 *   - Single contiguous heap (s_heap[])
 *   - All blocks (free and allocated) are linked in a PHYSICAL doubly-linked
 *     list (s_phys_list) ordered by address, enabling O(1) coalescing.
 *   - A separate FREE list (s_free_list) chains only free blocks for fast
 *     first-fit search.
 *   - Each block has a header: magic | size | is_free | phys_next | phys_prev
 *     | free_next | free_prev
 */

#include "../../include/memory.h"

/* ── Block Header (internal) ─────────────────────────────────────────────── */
typedef struct mem_block {
    uint32_t         magic;      /* FORGE_MAGIC_FREE or FORGE_MAGIC_ALLOC   */
    size_t           size;       /* payload bytes (not including header)    */
    bool             is_free;
    struct mem_block *phys_next; /* next block in physical address order    */
    struct mem_block *phys_prev;
    struct mem_block *free_next; /* next block in free list                 */
    struct mem_block *free_prev;
    const char       *file;      /* debug: allocation site                  */
    int               line;
} mem_block_t;

/* ── Internal Heap ───────────────────────────────────────────────────────── */
static uint8_t           s_heap[FORGE_HEAP_SIZE];
static mem_block_t      *s_free_list  = NULL;   /* head of free list        */
static mem_block_t      *s_phys_first = NULL;   /* first physical block     */
static bool              s_initialized = false;
static forge_mem_stats_t s_stats = {0};
static pthread_mutex_t   s_lock  = PTHREAD_MUTEX_INITIALIZER;

/* ── Alignment Helper ────────────────────────────────────────────────────── */
static inline size_t align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

/* ── Free-list helpers ───────────────────────────────────────────────────── */
static void fl_push(mem_block_t *b) {
    b->free_next = s_free_list;
    b->free_prev = NULL;
    if (s_free_list) s_free_list->free_prev = b;
    s_free_list = b;
}

static void fl_remove(mem_block_t *b) {
    if (b->free_prev) b->free_prev->free_next = b->free_next;
    else              s_free_list              = b->free_next;
    if (b->free_next) b->free_next->free_prev = b->free_prev;
    b->free_next = b->free_prev = NULL;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
int forge_mem_init(void) {
    pthread_mutex_lock(&s_lock);

    memset(s_heap, 0, sizeof(s_heap));
    memset(&s_stats, 0, sizeof(s_stats));

    s_phys_first = (mem_block_t *)s_heap;
    s_phys_first->magic      = FORGE_MAGIC_FREE;
    s_phys_first->size       = FORGE_HEAP_SIZE - sizeof(mem_block_t);
    s_phys_first->is_free    = true;
    s_phys_first->phys_next  = NULL;
    s_phys_first->phys_prev  = NULL;
    s_phys_first->free_next  = NULL;
    s_phys_first->free_prev  = NULL;
    s_phys_first->file       = NULL;
    s_phys_first->line       = 0;

    s_free_list = s_phys_first;
    s_stats.heap_size = FORGE_HEAP_SIZE;
    s_initialized = true;

    pthread_mutex_unlock(&s_lock);
    return FORGE_OK;
}

void forge_mem_destroy(void) {
    pthread_mutex_lock(&s_lock);
    s_free_list   = NULL;
    s_phys_first  = NULL;
    s_initialized = false;
    pthread_mutex_unlock(&s_lock);
}

/* ── Split ───────────────────────────────────────────────────────────────── */
static void split_block(mem_block_t *blk, size_t needed) {
    size_t total_avail = blk->size;
    size_t min_split   = sizeof(mem_block_t) + FORGE_MIN_BLOCK_SIZE;

    if (total_avail < needed + min_split)
        return; /* not worth splitting — give the whole block */

    /* New free block right after the allocated payload */
    mem_block_t *nb = (mem_block_t *)((uint8_t *)blk + sizeof(mem_block_t) + needed);
    nb->magic      = FORGE_MAGIC_FREE;
    nb->size       = total_avail - needed - sizeof(mem_block_t);
    nb->is_free    = true;
    nb->file       = NULL;
    nb->line       = 0;
    nb->free_next  = NULL;
    nb->free_prev  = NULL;

    /* Insert into physical list after blk */
    nb->phys_prev = blk;
    nb->phys_next = blk->phys_next;
    if (blk->phys_next) blk->phys_next->phys_prev = nb;
    blk->phys_next = nb;

    /* Add to free list */
    fl_push(nb);

    blk->size = needed;
    s_stats.block_count++;
}

/* ── Coalesce: merge blk with adjacent free neighbours ───────────────────── */
static void coalesce(mem_block_t *blk) {
    /* Merge with next physical block if free */
    mem_block_t *next = blk->phys_next;
    if (next && next->is_free && next->magic == FORGE_MAGIC_FREE) {
        fl_remove(next);
        blk->size += sizeof(mem_block_t) + next->size;
        blk->phys_next = next->phys_next;
        if (next->phys_next) next->phys_next->phys_prev = blk;
        s_stats.block_count--;
    }

    /* Merge with previous physical block if free */
    mem_block_t *prev = blk->phys_prev;
    if (prev && prev->is_free && prev->magic == FORGE_MAGIC_FREE) {
        fl_remove(blk);   /* blk is already in free list; remove it first */
        fl_remove(prev);
        prev->size += sizeof(mem_block_t) + blk->size;
        prev->phys_next = blk->phys_next;
        if (blk->phys_next) blk->phys_next->phys_prev = prev;
        s_stats.block_count--;
        blk = prev;       /* coalesced block is now 'prev' */
    }

    /* Re-add to free list */
    fl_push(blk);
}

/* ── forge_malloc ────────────────────────────────────────────────────────── */
void *forge_malloc_impl(size_t size, const char *file, int line) {
    if (!s_initialized || size == 0) return NULL;

    size = align_up(size, FORGE_ALIGNMENT);

    pthread_mutex_lock(&s_lock);

    mem_block_t *cur = s_free_list;
    while (cur) {
        if (cur->size >= size) {
            fl_remove(cur);
            split_block(cur, size);

            cur->is_free = false;
            cur->magic   = FORGE_MAGIC_ALLOC;
            cur->file    = file;
            cur->line    = line;

            s_stats.total_allocated += cur->size;
            s_stats.alloc_count++;
            if (s_stats.total_allocated > s_stats.peak_usage)
                s_stats.peak_usage = s_stats.total_allocated;

            pthread_mutex_unlock(&s_lock);
            return (void *)((uint8_t *)cur + sizeof(mem_block_t));
        }
        cur = cur->free_next;
    }

    pthread_mutex_unlock(&s_lock);
    forge_err("forge_malloc(%zu) OOM at %s:%d", size, file, line);
    return NULL;
}

/* ── forge_calloc ────────────────────────────────────────────────────────── */
void *forge_calloc_impl(size_t nmemb, size_t size, const char *file, int line) {
    if (nmemb == 0 || size == 0) return NULL;
    size_t total = nmemb * size;
    void *ptr = forge_malloc_impl(total, file, line);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/* ── forge_realloc ───────────────────────────────────────────────────────── */
void *forge_realloc_impl(void *ptr, size_t new_size, const char *file, int line) {
    if (!ptr)    return forge_malloc_impl(new_size, file, line);
    if (!new_size) { forge_free_impl(ptr, file, line); return NULL; }

    mem_block_t *blk = (mem_block_t *)((uint8_t *)ptr - sizeof(mem_block_t));
    if (blk->magic != FORGE_MAGIC_ALLOC) {
        forge_err("realloc: corrupt block at %p (%s:%d)", ptr, file, line);
        return NULL;
    }

    new_size = align_up(new_size, FORGE_ALIGNMENT);
    if (blk->size >= new_size) {
        pthread_mutex_lock(&s_lock);
        s_stats.realloc_count++;
        pthread_mutex_unlock(&s_lock);
        return ptr;
    }

    void *new_ptr = forge_malloc_impl(new_size, file, line);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, blk->size < new_size ? blk->size : new_size);
    forge_free_impl(ptr, file, line);

    pthread_mutex_lock(&s_lock);
    s_stats.realloc_count++;
    pthread_mutex_unlock(&s_lock);
    return new_ptr;
}

/* ── forge_free ──────────────────────────────────────────────────────────── */
void forge_free_impl(void *ptr, const char *file, int line) {
    if (!ptr || !s_initialized) return;

    mem_block_t *blk = (mem_block_t *)((uint8_t *)ptr - sizeof(mem_block_t));

    pthread_mutex_lock(&s_lock);

    if (blk->magic == FORGE_MAGIC_FREE) {
        forge_err("double-free at %p (%s:%d)", ptr, file, line);
        pthread_mutex_unlock(&s_lock);
        return;
    }
    if (blk->magic != FORGE_MAGIC_ALLOC) {
        forge_err("corrupt block at %p (%s:%d) magic=0x%08X", ptr, file, line, blk->magic);
        pthread_mutex_unlock(&s_lock);
        return;
    }

    s_stats.total_allocated -= blk->size;
    s_stats.total_freed     += blk->size;
    s_stats.free_count++;

    blk->is_free = true;
    blk->magic   = FORGE_MAGIC_FREE;
    blk->file    = file;
    blk->line    = line;

    coalesce(blk);

    pthread_mutex_unlock(&s_lock);
}

/* ── Statistics ──────────────────────────────────────────────────────────── */
forge_mem_stats_t forge_mem_get_stats(void) {
    pthread_mutex_lock(&s_lock);
    forge_mem_stats_t st = s_stats;
    pthread_mutex_unlock(&s_lock);
    return st;
}

double forge_mem_utilization(void) {
    if (s_stats.heap_size == 0) return 0.0;
    return (double)s_stats.total_allocated / (double)s_stats.heap_size * 100.0;
}

void forge_mem_print_stats(void) {
    forge_mem_stats_t st = forge_mem_get_stats();
    printf(C_BCYN "\n┌─────────────── Memory Allocator Statistics ───────────────┐\n" C_RESET);
    printf("  Heap capacity    : " C_BWHT "%8zu KB" C_RESET "\n", st.heap_size / 1024);
    printf("  In use now       : " C_BGRN "%8zu bytes" C_RESET "\n", st.total_allocated);
    printf("  Peak usage       : " C_BYEL "%8zu bytes" C_RESET "\n", st.peak_usage);
    printf("  Total freed      : " C_BGRN "%8zu bytes" C_RESET "\n", st.total_freed);
    printf("  Utilization      : " C_BWHT "%7.2f %%" C_RESET "\n", forge_mem_utilization());
    printf("  Alloc calls      : " C_BWHT "%8llu" C_RESET "\n", (unsigned long long)st.alloc_count);
    printf("  Free  calls      : " C_BWHT "%8llu" C_RESET "\n", (unsigned long long)st.free_count);
    printf("  Realloc calls    : " C_BWHT "%8llu" C_RESET "\n", (unsigned long long)st.realloc_count);
    printf(C_BCYN "└────────────────────────────────────────────────────────────┘\n" C_RESET);
}

int forge_mem_check_integrity(void) {
    pthread_mutex_lock(&s_lock);
    int errors = 0;
    mem_block_t *cur = s_phys_first;
    while (cur) {
        if (cur->magic != FORGE_MAGIC_FREE && cur->magic != FORGE_MAGIC_ALLOC) {
            forge_err("integrity: bad magic 0x%08X at %p", cur->magic, (void *)cur);
            errors++;
        }
        cur = cur->phys_next;
    }
    if (errors == 0)
        forge_ok("%s", "Heap integrity check passed");
    pthread_mutex_unlock(&s_lock);
    return errors;
}

void forge_mem_dump_blocks(void) {
    pthread_mutex_lock(&s_lock);
    printf(C_BCYN "=== Physical Block Dump ===\n" C_RESET);
    int i = 0;
    mem_block_t *cur = s_phys_first;
    while (cur) {
        printf("  [%3d] addr=%p  size=%7zu  %s  magic=0x%08X\n",
               i++, (void *)cur, cur->size,
               cur->is_free ? C_BGRN "FREE " C_RESET : C_BYEL "ALLOC" C_RESET,
               cur->magic);
        cur = cur->phys_next;
    }
    printf(C_DIM "  (%d total blocks)\n" C_RESET, i);
    pthread_mutex_unlock(&s_lock);
}
