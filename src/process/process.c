/*
 * ForgeOS - Process Monitor
 * src/process/process.c
 *
 * Reads /proc/[pid]/stat, /proc/stat, /proc/meminfo.
 * Renders a top(1)-like TUI with live CPU/memory bars,
 * sortable process list, and keyboard controls.
 */

#include "../../include/process.h"
#include "../../include/memory.h"

/* ── Utility ─────────────────────────────────────────────────────────────── */
void format_bytes(long bytes, char *buf, int bufsz) {
    if (bytes < 1024)             snprintf(buf, bufsz, "%ld B",  bytes);
    else if (bytes < 1024*1024)   snprintf(buf, bufsz, "%.1f K", bytes / 1024.0);
    else if (bytes < 1024*1024*1024) snprintf(buf, bufsz, "%.1f M", bytes / (1024.0*1024));
    else                          snprintf(buf, bufsz, "%.1f G", bytes / (1024.0*1024*1024));
}

void format_uptime(double secs, char *buf, int bufsz) {
    int days  = (int)(secs / 86400);
    int hours = (int)(secs / 3600)  % 24;
    int mins  = (int)(secs / 60)    % 60;
    int s     = (int)secs           % 60;
    if (days > 0)
        snprintf(buf, bufsz, "%dd %02d:%02d:%02d", days, hours, mins, s);
    else
        snprintf(buf, bufsz, "%02d:%02d:%02d", hours, mins, s);
}

const char *proc_state_name(proc_state_t s) {
    switch (s) {
        case PROC_STATE_RUNNING:  return "R";
        case PROC_STATE_SLEEPING: return "S";
        case PROC_STATE_DISK:     return "D";
        case PROC_STATE_ZOMBIE:   return "Z";
        case PROC_STATE_STOPPED:  return "T";
        case PROC_STATE_IDLE:     return "I";
        default:                  return "?";
    }
}

/* ── Read /proc/stat ─────────────────────────────────────────────────────── */
int procmon_read_sys(sys_stats_t *sys) {
    memset(sys, 0, sizeof(*sys));

    /* /proc/stat */
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return FORGE_ERR;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu",
                   &sys->cpu_user, &sys->cpu_nice, &sys->cpu_system,
                   &sys->cpu_idle, &sys->cpu_iowait,
                   &sys->cpu_irq,  &sys->cpu_softirq);
            sys->cpu_total = sys->cpu_user + sys->cpu_nice + sys->cpu_system +
                             sys->cpu_idle + sys->cpu_iowait +
                             sys->cpu_irq  + sys->cpu_softirq;
        } else if (strncmp(line, "procs_running", 13) == 0) {
            sscanf(line, "procs_running %d", &sys->num_running);
        } else if (strncmp(line, "procs_blocked", 13) == 0) {
            int blocked;
            sscanf(line, "procs_blocked %d", &blocked);
            UNUSED(blocked);
        }
    }
    fclose(f);

    /* /proc/meminfo */
    f = fopen("/proc/meminfo", "r");
    if (!f) return FORGE_ERR;
    while (fgets(line, sizeof(line), f)) {
        long val;
        if (sscanf(line, "MemTotal: %ld kB",     &val) == 1) sys->mem_total_kb     = val;
        if (sscanf(line, "MemFree: %ld kB",      &val) == 1) sys->mem_free_kb      = val;
        if (sscanf(line, "MemAvailable: %ld kB", &val) == 1) sys->mem_available_kb = val;
        if (sscanf(line, "Buffers: %ld kB",      &val) == 1) sys->mem_buffers_kb   = val;
        if (sscanf(line, "Cached: %ld kB",       &val) == 1) sys->mem_cached_kb    = val;
        if (sscanf(line, "SwapTotal: %ld kB",    &val) == 1) sys->swap_total_kb    = val;
        if (sscanf(line, "SwapFree: %ld kB",     &val) == 1) sys->swap_free_kb     = val;
    }
    fclose(f);

    if (sys->mem_total_kb > 0)
        sys->mem_usage_pct = 100.0 * (sys->mem_total_kb - sys->mem_available_kb) / sys->mem_total_kb;

    /* /proc/loadavg */
    f = fopen("/proc/loadavg", "r");
    if (f) {
        fscanf(f, "%lf %lf %lf", &sys->load_1, &sys->load_5, &sys->load_15);
        fclose(f);
    }

    /* /proc/uptime */
    f = fopen("/proc/uptime", "r");
    if (f) { fscanf(f, "%lf", &sys->uptime_s); fclose(f); }

    return FORGE_OK;
}

/* ── Read a single /proc/[pid]/stat ──────────────────────────────────────── */
int procmon_read_proc(proc_entry_t *e, pid_t pid) {
    memset(e, 0, sizeof(*e));
    e->pid = pid;
    e->state = PROC_STATE_UNKNOWN;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return FORGE_ERR;

    /* comm may contain spaces so we read past the parentheses */
    char raw[4096];
    if (!fgets(raw, sizeof(raw), f)) { fclose(f); return FORGE_ERR; }
    fclose(f);

    /* Find comm */
    char *lp = strchr(raw, '(');
    char *rp = strrchr(raw, ')');
    if (!lp || !rp) return FORGE_ERR;
    int nlen = (int)(rp - lp - 1);
    if (nlen > (int)sizeof(e->name) - 1) nlen = sizeof(e->name) - 1;
    strncpy(e->name, lp + 1, nlen);
    e->name[nlen] = '\0';

    /* Parse rest of the fields after ')' - fields are after "comm) " */
    /* Format: state ppid pgrp sid ... utime stime cutime cstime priority nice num_threads */
    char state_c = '?';
    int ppid_val = 0;
    unsigned long utime_v=0, stime_v=0, cutime_v=0, cstime_v=0;
    long prio_v=0, nice_v=0, nthreads_v=0;
    long vsize_v=0, rss_v=0;
    sscanf(rp + 2, "%c %d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
           "%lu %lu %lu %lu %ld %ld %ld %*d %*d %ld %ld",
           &state_c, &ppid_val,
           &utime_v, &stime_v, &cutime_v, &cstime_v,
           &prio_v, &nice_v, &nthreads_v,
           &vsize_v, &rss_v);
    e->ppid = (pid_t)ppid_val;
    e->utime = utime_v; e->stime = stime_v;
    e->cutime = cutime_v; e->cstime = cstime_v;
    e->priority = prio_v; e->nice = nice_v;
    e->num_threads = nthreads_v;
    e->vsize = vsize_v; e->rss = rss_v;

    e->state = (proc_state_t)state_c;

    /* Read username */
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (f) {
        char line[256];
        uid_t uid = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "Uid: %u", &uid) == 1) break;
        }
        fclose(f);
        struct passwd *pw = getpwuid(uid);
        strncpy(e->user, pw ? pw->pw_name : "?", sizeof(e->user) - 1);
        e->uid = uid;
    }

    /* Read cmdline */
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    f = fopen(path, "r");
    if (f) {
        int n = fread(e->cmd, 1, sizeof(e->cmd) - 1, f);
        fclose(f);
        for (int i = 0; i < n; i++) if (e->cmd[i] == '\0') e->cmd[i] = ' ';
        e->cmd[n] = '\0';
        forge_trim(e->cmd);
    }
    if (!e->cmd[0]) strncpy(e->cmd, e->name, sizeof(e->cmd) - 1);

    return FORGE_OK;
}

/* ── Read All Processes ───────────────────────────────────────────────────── */
int procmon_read_all(proc_monitor_t *pm) {
    /* Move current to prev */
    pm->prev_count = pm->count;
    memcpy(pm->prev_entries, pm->entries, sizeof(proc_entry_t) * pm->count);

    pm->count = 0;
    pm->sys.num_procs = pm->sys.num_zombie = pm->sys.num_sleeping = 0;

    DIR *dir = opendir("/proc");
    if (!dir) return FORGE_ERR;

    struct dirent *de;
    while ((de = readdir(dir)) && pm->count < PROC_MAX_ENTRIES) {
        if (!isdigit(de->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(de->d_name);
        if (procmon_read_proc(&pm->entries[pm->count], pid) == FORGE_OK) {
            pm->sys.num_procs++;
            switch (pm->entries[pm->count].state) {
                case PROC_STATE_ZOMBIE:   pm->sys.num_zombie++;   break;
                case PROC_STATE_SLEEPING: pm->sys.num_sleeping++; break;
                case PROC_STATE_RUNNING:  pm->sys.num_running++;  break;
                default: break;
            }
            pm->count++;
        }
    }
    closedir(dir);
    return FORGE_OK;
}

/* ── Compute CPU % per Process ───────────────────────────────────────────── */
void procmon_compute_cpu(proc_monitor_t *pm) {
    unsigned long delta_total = pm->sys.cpu_total - pm->prev_cpu_total;
    if (delta_total == 0) delta_total = 1;

    long page_size = sysconf(_SC_PAGE_SIZE);

    for (int i = 0; i < pm->count; i++) {
        proc_entry_t *cur = &pm->entries[i];
        cur->mem_percent = 100.0 * (cur->rss * page_size)
                           / (pm->sys.mem_total_kb * 1024.0);
        cur->cpu_percent = 0.0;

        /* Find previous entry with same PID */
        for (int j = 0; j < pm->prev_count; j++) {
            if (pm->prev_entries[j].pid == cur->pid) {
                unsigned long prev_time = pm->prev_entries[j].utime + pm->prev_entries[j].stime;
                unsigned long cur_time  = cur->utime + cur->stime;
                unsigned long delta_proc = cur_time > prev_time ? cur_time - prev_time : 0;
                cur->cpu_percent = 100.0 * delta_proc / delta_total;
                break;
            }
        }
    }
}

/* ── Sorting ─────────────────────────────────────────────────────────────── */
int proc_cmp_cpu (const void *a, const void *b) {
    return ((proc_entry_t *)b)->cpu_percent > ((proc_entry_t *)a)->cpu_percent ? 1 : -1;
}
int proc_cmp_mem (const void *a, const void *b) {
    return ((proc_entry_t *)b)->mem_percent > ((proc_entry_t *)a)->mem_percent ? 1 : -1;
}
int proc_cmp_pid (const void *a, const void *b) {
    return ((proc_entry_t *)a)->pid - ((proc_entry_t *)b)->pid;
}
int proc_cmp_name(const void *a, const void *b) {
    return strcmp(((proc_entry_t *)a)->name, ((proc_entry_t *)b)->name);
}

void procmon_sort(proc_monitor_t *pm) {
    typedef int (*cmp_fn)(const void *, const void *);
    cmp_fn fns[] = { proc_cmp_cpu, proc_cmp_mem, proc_cmp_pid, proc_cmp_name, proc_cmp_cpu };
    int fn_idx = pm->sort_mode < SORT_COUNT ? pm->sort_mode : 0;
    qsort(pm->entries, pm->count, sizeof(proc_entry_t), fns[fn_idx]);
}

/* ── Horizontal Bar ──────────────────────────────────────────────────────── */
void procmon_draw_cpu_bar(double pct, int width) {
    int filled = (int)(pct / 100.0 * width);
    if (filled > width) filled = width;
    const char *color = pct > 80 ? C_BRED : pct > 50 ? C_BYEL : C_BGRN;
    printf("%s", color);
    for (int i = 0; i < width; i++) fputs(i < filled ? "#" : "-", stdout);
    printf(C_RESET);
}

void procmon_draw_mem_bar(double pct, int width) {
    int filled = (int)(pct / 100.0 * width);
    if (filled > width) filled = width;
    const char *color = pct > 80 ? C_BRED : pct > 60 ? C_BYEL : C_BBLU;
    printf("%s", color);
    for (int i = 0; i < width; i++) fputs(i < filled ? "=" : "-", stdout);
    printf(C_RESET);
}

/* ── TUI Header ──────────────────────────────────────────────────────────── */
void procmon_draw_header(proc_monitor_t *pm) {
    sys_stats_t *s = &pm->sys;
    char uptime_buf[32], mem_used[16], mem_total[16];
    format_uptime(s->uptime_s, uptime_buf, sizeof(uptime_buf));
    format_bytes((s->mem_total_kb - s->mem_available_kb) * 1024, mem_used, sizeof(mem_used));
    format_bytes(s->mem_total_kb * 1024, mem_total, sizeof(mem_total));

    /* Line 1: title + uptime + load */
    printf(C_BG_BLACK C_BCYN " ForgeOS top " C_RESET
           "  uptime: " C_BWHT "%s" C_RESET
           "  load: " C_BWHT "%.2f %.2f %.2f" C_RESET "\n",
           uptime_buf, s->load_1, s->load_5, s->load_15);

    /* Line 2: tasks */
    printf("  Tasks: " C_BWHT "%d" C_RESET " total  "
           C_BGRN "%d" C_RESET " running  "
           C_BCYN "%d" C_RESET " sleeping  "
           C_BRED "%d" C_RESET " zombie\n",
           s->num_procs, s->num_running, s->num_sleeping, s->num_zombie);

    /* Line 3: CPU bar */
    printf("  CPU%%: [");
    procmon_draw_cpu_bar(s->cpu_usage_pct, 30);
    printf("] " C_BWHT "%.1f%%" C_RESET "\n", s->cpu_usage_pct);

    /* Line 4: Memory bar */
    printf("  Mem:  [");
    procmon_draw_mem_bar(s->mem_usage_pct, 30);
    printf("] " C_BWHT "%s / %s" C_RESET " (%.1f%%)\n",
           mem_used, mem_total, s->mem_usage_pct);

    /* Swap */
    double swap_used_kb = s->swap_total_kb - s->swap_free_kb;
    double swap_pct = s->swap_total_kb > 0 ? 100.0 * swap_used_kb / s->swap_total_kb : 0;
    char sw_used[16], sw_total[16];
    format_bytes((long)swap_used_kb * 1024, sw_used, sizeof(sw_used));
    format_bytes(s->swap_total_kb * 1024, sw_total, sizeof(sw_total));
    printf("  Swap: [");
    procmon_draw_mem_bar(swap_pct, 30);
    printf("] " C_BWHT "%s / %s" C_RESET "\n\n", sw_used, sw_total);

    /* Column headers */
    static const char *sort_labels[SORT_COUNT] = { "CPU%", "MEM%", "PID", "NAME", "STATE" };
    printf(C_BOLD C_BG_BLUE "  %-6s %-12s %-10s %6s %6s  %-20s  [sort: %s]" C_RESET "\n",
           "PID", "USER", "STATE",
           "CPU%", "MEM%",
           "COMMAND",
           pm->sort_mode < SORT_COUNT ? sort_labels[pm->sort_mode] : "?");
    printf(C_DIM "  %-6s %-12s %-10s %6s %6s  %-30s\n" C_RESET,
           "------", "------------", "----------",
           "------", "------", "------------------------------");
}

/* ── TUI Process List ────────────────────────────────────────────────────── */
void procmon_draw_proc_list(proc_monitor_t *pm) {
    int visible = pm->screen_rows - 10;  /* rows available after header */
    if (visible < 1) visible = 1;

    for (int i = pm->scroll_offset; i < pm->count && i < pm->scroll_offset + visible; i++) {
        proc_entry_t *e = &pm->entries[i];

        /* Filter */
        if (pm->filter_active &&
            strstr(e->cmd, pm->filter) == NULL &&
            strstr(e->name, pm->filter) == NULL)
            continue;

        /* Color by state */
        const char *state_col =
            e->state == PROC_STATE_RUNNING  ? C_BGRN :
            e->state == PROC_STATE_ZOMBIE   ? C_BRED :
            e->state == PROC_STATE_STOPPED  ? C_BYEL : C_RESET;

        /* High CPU warning */
        const char *cpu_col = e->cpu_percent > 50 ? C_BRED :
                              e->cpu_percent > 20 ? C_BYEL : C_RESET;

        /* Truncate command */
        char cmd_short[36];
        strncpy(cmd_short, e->cmd, 35);
        cmd_short[35] = '\0';

        printf("  " C_BWHT "%-6d" C_RESET " %-12s %s%-10s" C_RESET
               " %s%5.1f%%" C_RESET " %5.1f%%  %s\n",
               e->pid, e->user,
               state_col, proc_state_name(e->state),
               cpu_col, e->cpu_percent,
               e->mem_percent,
               cmd_short);
    }
}

/* ── Full Screen Draw ────────────────────────────────────────────────────── */
void procmon_draw(proc_monitor_t *pm) {
    forge_get_terminal_size(&pm->screen_rows, &pm->screen_cols);

    printf(T_ALT_SCREEN_ON T_CLEAR);
    procmon_draw_header(pm);
    procmon_draw_proc_list(pm);

    /* Help bar at bottom */
    printf(T_SAVE_CURSOR);
    printf("\033[%d;0H", pm->screen_rows);
    printf(C_BG_BLACK C_DIM
           " q=Quit  c=CPU  m=Mem  p=PID  n=Name  ↑↓=Scroll  r=Refresh  /=Filter"
           C_RESET);
    printf(T_REST_CURSOR);
    fflush(stdout);
}

/* ── Key Handler ─────────────────────────────────────────────────────────── */
void procmon_handle_key(proc_monitor_t *pm, int key) {
    switch (key) {
        case 'q': case 'Q': pm->running = false; break;
        case 'c': pm->sort_mode = SORT_BY_CPU;  break;
        case 'm': pm->sort_mode = SORT_BY_MEM;  break;
        case 'p': pm->sort_mode = SORT_BY_PID;  break;
        case 'n': pm->sort_mode = SORT_BY_NAME; break;
        case 'r': case ' ': break; /* force refresh */
        case 'k': {
            /* Kill process - prompt for PID (simplified) */
            break;
        }
        case '\033': {
            char seq[3];
            if (read(STDIN_FILENO, seq, 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, seq, 1) == 1) {
                    if (seq[0] == 'A' && pm->scroll_offset > 0) pm->scroll_offset--;
                    if (seq[0] == 'B' && pm->scroll_offset < pm->count - 1) pm->scroll_offset++;
                }
            }
            break;
        }
    }
}

/* ── Init / Destroy ──────────────────────────────────────────────────────── */
int procmon_init(proc_monitor_t *pm) {
    memset(pm, 0, sizeof(*pm));
    pm->sort_mode  = SORT_BY_CPU;
    pm->running    = true;
    pm->update_ms  = PROC_UPDATE_MS;
    forge_get_terminal_size(&pm->screen_rows, &pm->screen_cols);
    return FORGE_OK;
}

void procmon_destroy(proc_monitor_t *pm) { UNUSED(pm); }

/* ── Main Loop ───────────────────────────────────────────────────────────── */
void procmon_run(proc_monitor_t *pm) {
    forge_enable_raw_mode();

    /* Non-blocking reads for key input */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    while (pm->running) {
        /* Read system and process data */
        procmon_read_sys(&pm->sys);

        /* CPU delta */
        unsigned long delta_total = pm->sys.cpu_total - pm->prev_cpu_total;
        unsigned long delta_idle  = pm->sys.cpu_idle  - pm->prev_cpu_idle;
        if (delta_total > 0)
            pm->sys.cpu_usage_pct = 100.0 * (delta_total - delta_idle) / delta_total;
        pm->prev_cpu_total = pm->sys.cpu_total;
        pm->prev_cpu_idle  = pm->sys.cpu_idle;

        procmon_read_all(pm);
        procmon_compute_cpu(pm);
        procmon_sort(pm);
        procmon_draw(pm);

        /* Poll for key input during sleep */
        int elapsed = 0;
        while (elapsed < pm->update_ms && pm->running) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1)
                procmon_handle_key(pm, (int)c);
            forge_sleep_ms(50);
            elapsed += 50;
        }
    }

    /* Restore */
    fcntl(STDIN_FILENO, F_SETFL, flags);
    forge_disable_raw_mode();
    printf(T_ALT_SCREEN_OFF);
    printf(T_CURSOR_SHOW);
    fflush(stdout);
}

/* ── Entry Point ─────────────────────────────────────────────────────────── */
int procmon_main(void) {
    proc_monitor_t pm;
    procmon_init(&pm);
    procmon_run(&pm);
    procmon_destroy(&pm);
    return 0;
}
