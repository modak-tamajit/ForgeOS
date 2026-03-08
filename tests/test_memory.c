/*
 * ForgeOS - Memory Allocator Tests
 * tests/test_memory.c
 *
 * A minimal test harness that validates forge_malloc/free/calloc/realloc.
 */

#include "../include/forgeos.h"
#include "../include/memory.h"

/* ── Test Harness ────────────────────────────────────────────────────────── */
static int s_tests_run    = 0;
static int s_tests_passed = 0;
static int s_tests_failed = 0;

#define TEST(name)      do { printf(C_BCYN "\n  [TEST] " name C_RESET "\n"); s_tests_run++; } while(0)
#define EXPECT_TRUE(cond) \
    do { if (cond) { printf(C_BGRN "    ✓ " #cond C_RESET "\n"); s_tests_passed++; } \
         else      { printf(C_BRED "    ✗ " #cond " (FAILED at " __FILE__ ":%d)\n" C_RESET, __LINE__); s_tests_failed++; } } while(0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_NE(a, b) EXPECT_TRUE((a) != (b))
#define EXPECT_NULL(p)  EXPECT_TRUE((p) == NULL)
#define EXPECT_NNULL(p) EXPECT_TRUE((p) != NULL)

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_basic_malloc(void) {
    TEST("Basic malloc / free");
    void *p = forge_malloc(64);
    EXPECT_NNULL(p);
    forge_free(p);

    void *p2 = forge_malloc(1);
    EXPECT_NNULL(p2);
    forge_free(p2);

    void *p3 = forge_malloc(0);
    EXPECT_NULL(p3);   /* size 0 → NULL */
}

static void test_calloc(void) {
    TEST("calloc zeroes memory");
    int *arr = forge_calloc(16, sizeof(int));
    EXPECT_NNULL(arr);
    bool all_zero = true;
    for (int i = 0; i < 16; i++) if (arr[i] != 0) { all_zero = false; break; }
    EXPECT_TRUE(all_zero);
    forge_free(arr);
}

static void test_write_read(void) {
    TEST("Write and read back allocated memory");
    char *buf = forge_malloc(128);
    EXPECT_NNULL(buf);
    strcpy(buf, "ForgeOS memory allocator test string!");
    EXPECT_TRUE(strcmp(buf, "ForgeOS memory allocator test string!") == 0);
    forge_free(buf);
}

static void test_multiple_allocs(void) {
    TEST("Multiple simultaneous allocations");
    void *ptrs[64];
    for (int i = 0; i < 64; i++) {
        ptrs[i] = forge_malloc(128);
        EXPECT_NNULL(ptrs[i]);
        memset(ptrs[i], i & 0xFF, 128);
    }
    /* Verify contents */
    bool ok = true;
    for (int i = 0; i < 64; i++) {
        uint8_t *b = (uint8_t *)ptrs[i];
        for (int j = 0; j < 128; j++) if (b[j] != (i & 0xFF)) { ok = false; break; }
    }
    EXPECT_TRUE(ok);
    for (int i = 0; i < 64; i++) forge_free(ptrs[i]);
}

static void test_realloc(void) {
    TEST("Realloc grow / shrink");
    char *p = forge_malloc(64);
    EXPECT_NNULL(p);
    strcpy(p, "hello");
    p = forge_realloc(p, 256);
    EXPECT_NNULL(p);
    EXPECT_TRUE(strcmp(p, "hello") == 0);
    p = forge_realloc(p, 8);
    EXPECT_NNULL(p);
    forge_free(p);
}

static void test_large_alloc(void) {
    TEST("Large allocation (1 MB)");
    size_t sz = 1024 * 1024;
    void *p = forge_malloc(sz);
    EXPECT_NNULL(p);
    memset(p, 0xAB, sz);
    forge_free(p);
}

static void test_stats(void) {
    TEST("Allocation statistics");
    forge_mem_stats_t before = forge_mem_get_stats();
    void *p = forge_malloc(512);
    forge_mem_stats_t after = forge_mem_get_stats();
    EXPECT_TRUE(after.alloc_count == before.alloc_count + 1);
    EXPECT_TRUE(after.total_allocated >= before.total_allocated + 512);
    forge_free(p);
    forge_mem_stats_t final = forge_mem_get_stats();
    EXPECT_TRUE(final.free_count == before.free_count + 1);
}

static void test_integrity(void) {
    TEST("Heap integrity check");
    void *p = forge_malloc(256);
    EXPECT_NNULL(p);
    int errors = forge_mem_check_integrity();
    EXPECT_EQ(errors, 0);
    forge_free(p);
    errors = forge_mem_check_integrity();
    EXPECT_EQ(errors, 0);
}

static void test_stress(void) {
    TEST("Stress test: random alloc/free cycles (1000 ops)");
    void *ptrs[100] = {0};
    srand(42);
    for (int i = 0; i < 1000; i++) {
        int idx = rand() % 100;
        if (ptrs[idx]) { forge_free(ptrs[idx]); ptrs[idx] = NULL; }
        else           { ptrs[idx] = forge_malloc(rand() % 512 + 1); }
    }
    for (int i = 0; i < 100; i++) if (ptrs[i]) forge_free(ptrs[i]);
    EXPECT_EQ(forge_mem_check_integrity(), 0);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    printf(C_BCYN
           "\n╔═══════════════════════════════════════╗\n"
           "║  ForgeOS Memory Allocator Test Suite  ║\n"
           "╚═══════════════════════════════════════╝\n" C_RESET);

    forge_mem_init();

    test_basic_malloc();
    test_calloc();
    test_write_read();
    test_multiple_allocs();
    test_realloc();
    test_large_alloc();
    test_stats();
    test_integrity();
    test_stress();

    forge_mem_print_stats();

    printf(C_BCYN "\n══════════════════════════════════════════\n" C_RESET);
    printf("  Tests run:    %d\n", s_tests_run);
    printf("  " C_BGRN "Passed: %d" C_RESET "\n", s_tests_passed);
    if (s_tests_failed > 0)
        printf("  " C_BRED "FAILED: %d" C_RESET "\n", s_tests_failed);
    else
        printf("  " C_BGRN "All tests passed! ✓" C_RESET "\n");
    printf(C_BCYN "══════════════════════════════════════════\n\n" C_RESET);

    forge_mem_destroy();
    return s_tests_failed > 0 ? 1 : 0;
}
