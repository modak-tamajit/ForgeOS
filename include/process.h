/*
 * ForgeOS - Process Monitor
 * include/process.h
 *
 * A top(1)-like monitor reading from /proc that displays live
 * CPU/memory usage, process list, and system summary.
 */

#ifndef FORGE_PROCESS_H
#define FORGE_PROCESS_H

#include "forgeos.h"

/* ── Configuration ───────────────────────────────────────────────────────── */
#define PROC_MAX_ENTRIES     512
#define PROC_NAME_LEN        256
#define PROC_CMD_LEN         512
#define PROC_UPDATE_MS       1000    /* refresh interval in milliseconds    */
#define PROC_TOP_COUNT       20      /* rows visible in default view        */

/* ── Process State ───────────────────────────────────────────────────────── */
typedef enum {
    PROC_STATE_RUNNING  = 'R',
    PROC_STATE_SLEEPING = 'S',
    PROC_STATE_DISK     = 'D',
    PROC_STATE_ZOMBIE   = 'Z',
    PROC_STATE_STOPPED  = 'T',
    PROC_STATE_IDLE     = 'I',
    PROC_STATE_UNKNOWN  = '?',
} proc_state_t;

/* ── Per-process Information ─────────────────────────────────────────────── */
typedef struct {
    pid_t         pid;
    pid_t         ppid;
    uid_t         uid;
    char          name  [PROC_NAME_LEN];
    char          cmd   [PROC_CMD_LEN];
    char          user  [64];
    proc_state_t  state;

    /* CPU */
    unsigned long utime;           /* user mode ticks                      */
    unsigned long stime;           /* kernel mode ticks                    */
    unsigned long cutime;
    unsigned long cstime;
    unsigned long starttime;
    double        cpu_percent;     /* computed over sample interval        */

    /* Memory (pages → bytes via page_size) */
    long          vsize;           /* virtual memory size in bytes         */
    long          rss;             /* resident set size in pages           */
    double        mem_percent;

    /* I/O */
    long          priority;
    long          nice;
    long          num_threads;
} proc_entry_t;

/* ── System-wide Stats ───────────────────────────────────────────────────── */
typedef struct {
    /* CPU (from /proc/stat) */
    unsigned long cpu_user;
    unsigned long cpu_nice;
    unsigned long cpu_system;
    unsigned long cpu_idle;
    unsigned long cpu_iowait;
    unsigned long cpu_irq;
    unsigned long cpu_softirq;
    unsigned long cpu_total;
    double        cpu_usage_pct;    /* 0–100                               */

    /* Memory (from /proc/meminfo) */
    long          mem_total_kb;
    long          mem_free_kb;
    long          mem_available_kb;
    long          mem_buffers_kb;
    long          mem_cached_kb;
    long          swap_total_kb;
    long          swap_free_kb;
    double        mem_usage_pct;

    /* Load */
    double        load_1;
    double        load_5;
    double        load_15;

    /* Counts */
    int           num_procs;
    int           num_running;
    int           num_sleeping;
    int           num_zombie;

    /* Uptime */
    double        uptime_s;
} sys_stats_t;

/* ── Sort Modes ──────────────────────────────────────────────────────────── */
typedef enum {
    SORT_BY_CPU  = 0,
    SORT_BY_MEM,
    SORT_BY_PID,
    SORT_BY_NAME,
    SORT_BY_STATE,
    SORT_COUNT,
} proc_sort_t;

/* ── Monitor State ───────────────────────────────────────────────────────── */
typedef struct {
    proc_entry_t  entries     [PROC_MAX_ENTRIES];
    proc_entry_t  prev_entries[PROC_MAX_ENTRIES];  /* for delta CPU calc   */
    int           count;
    int           prev_count;

    sys_stats_t   sys;
    sys_stats_t   prev_sys;

    proc_sort_t   sort_mode;
    bool          sort_reverse;
    int           scroll_offset;
    bool          running;
    int           update_ms;
    int           screen_rows;
    int           screen_cols;

    /* filter */
    char          filter[64];
    bool          filter_active;

    unsigned long prev_cpu_total;
    unsigned long prev_cpu_idle;
} proc_monitor_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
int   procmon_init(proc_monitor_t *pm);
void  procmon_destroy(proc_monitor_t *pm);
void  procmon_run(proc_monitor_t *pm);           /* interactive TUI loop   */

/* Data collection */
int   procmon_read_all(proc_monitor_t *pm);
int   procmon_read_sys(sys_stats_t *sys);
int   procmon_read_proc(proc_entry_t *e, pid_t pid);
void  procmon_compute_cpu(proc_monitor_t *pm);

/* Sorting */
void  procmon_sort(proc_monitor_t *pm);
int   proc_cmp_cpu  (const void *a, const void *b);
int   proc_cmp_mem  (const void *a, const void *b);
int   proc_cmp_pid  (const void *a, const void *b);
int   proc_cmp_name (const void *a, const void *b);

/* Display */
void  procmon_draw(proc_monitor_t *pm);
void  procmon_draw_header(proc_monitor_t *pm);
void  procmon_draw_cpu_bar(double pct, int width);
void  procmon_draw_mem_bar(double pct, int width);
void  procmon_draw_proc_list(proc_monitor_t *pm);
void  procmon_handle_key(proc_monitor_t *pm, int key);

/* Utility */
const char *proc_state_name(proc_state_t s);
void        format_bytes(long bytes, char *buf, int bufsz);
void        format_uptime(double secs, char *buf, int bufsz);

#endif /* FORGE_PROCESS_H */

/* Entry point for standalone process monitor */
int procmon_main(void);
