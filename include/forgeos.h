/*
 * ███████╗ ██████╗ ██████╗  ██████╗ ███████╗ ██████╗ ███████╗
 * ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝██╔═══██╗██╔════╝
 * █████╗  ██║   ██║██████╔╝██║  ███╗█████╗  ██║   ██║███████╗
 * ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  ██║   ██║╚════██║
 * ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗╚██████╔╝███████║
 * ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚══════╝
 *
 * ForgeOS - A Terminal Operating Environment Built in C
 * Master Header: Global Types, Constants, and Declarations
 *
 * Copyright (c) 2024 ForgeOS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef FORGEOS_H
#define FORGEOS_H

/* ── Standard Library ────────────────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

/* ── POSIX ───────────────────────────────────────────────────────────────── */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <dirent.h>
#include <pwd.h>
#include <pthread.h>
#include <dlfcn.h>

/* ── Version ─────────────────────────────────────────────────────────────── */
#define FORGEOS_VERSION_MAJOR  1
#define FORGEOS_VERSION_MINOR  0
#define FORGEOS_VERSION_PATCH  0
#define FORGEOS_VERSION_STR    "1.0.0"
#define FORGEOS_NAME           "ForgeOS"
#define FORGEOS_CODENAME       "Ironclad"

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define FORGE_MAX_INPUT       4096
#define FORGE_MAX_ARGS        128
#define FORGE_MAX_PATH        512
#define FORGE_MAX_CMD         256
#define FORGE_HISTORY_SIZE    500
#define FORGE_MAX_PLUGINS     32
#define FORGE_MAX_ENV_VARS    256

/* ── Terminal Colors ─────────────────────────────────────────────────────── */
#define C_RESET     "\033[0m"
#define C_BOLD      "\033[1m"
#define C_DIM       "\033[2m"
#define C_ITALIC    "\033[3m"
#define C_UNDERLINE "\033[4m"
#define C_RED       "\033[31m"
#define C_GREEN     "\033[32m"
#define C_YELLOW    "\033[33m"
#define C_BLUE      "\033[34m"
#define C_MAGENTA   "\033[35m"
#define C_CYAN      "\033[36m"
#define C_WHITE     "\033[37m"
#define C_BRED      "\033[1;31m"
#define C_BGRN      "\033[1;32m"
#define C_BYEL      "\033[1;33m"
#define C_BBLU      "\033[1;34m"
#define C_BMAG      "\033[1;35m"
#define C_BCYN      "\033[1;36m"
#define C_BWHT      "\033[1;37m"
#define C_BG_BLACK  "\033[40m"
#define C_BG_RED    "\033[41m"
#define C_BG_GREEN  "\033[42m"
#define C_BG_BLUE   "\033[44m"
#define C_BG_CYAN   "\033[46m"
#define C_BG_WHITE  "\033[47m"

/* ── Terminal Escape Sequences ───────────────────────────────────────────── */
#define T_CLEAR         "\033[2J\033[H"
#define T_CLEAR_LINE    "\033[2K\r"
#define T_CURSOR_UP     "\033[A"
#define T_CURSOR_DOWN   "\033[B"
#define T_CURSOR_RIGHT  "\033[C"
#define T_CURSOR_LEFT   "\033[D"
#define T_CURSOR_HIDE   "\033[?25l"
#define T_CURSOR_SHOW   "\033[?25h"
#define T_SAVE_CURSOR   "\033[s"
#define T_REST_CURSOR   "\033[u"
#define T_ALT_SCREEN_ON  "\033[?1049h"
#define T_ALT_SCREEN_OFF "\033[?1049l"

/* ── Utility Macros ──────────────────────────────────────────────────────── */
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define CLAMP(x, lo, hi)    MIN(MAX(x, lo), hi)
#define UNUSED(x)           (void)(x)

#define forge_log(fmt, ...) \
    fprintf(stderr, C_DIM "[forge] " C_RESET fmt "\n", ##__VA_ARGS__)

#define forge_err(fmt, ...) \
    fprintf(stderr, C_BRED "[error] " C_RESET fmt "\n", ##__VA_ARGS__)

/* forge_ok / forge_warn accept optional format args */
#define forge_ok(fmt, ...) \
    fprintf(stdout, C_BGRN "[ok]    " C_RESET fmt "\n", ##__VA_ARGS__)

#define forge_warn(fmt, ...) \
    fprintf(stderr, C_BYEL "[warn]  " C_RESET fmt "\n", ##__VA_ARGS__)

/* ── Return Codes ────────────────────────────────────────────────────────── */
typedef enum {
    FORGE_OK           =  0,
    FORGE_ERR          = -1,
    FORGE_ERR_NOMEM    = -2,
    FORGE_ERR_NOTFOUND = -3,
    FORGE_ERR_PERM     = -4,
    FORGE_ERR_IO       = -5,
    FORGE_ERR_ARGS     = -6,
    FORGE_EXIT         =  1,
} forge_status_t;

/* ── Global Runtime State ────────────────────────────────────────────────── */
typedef struct {
    bool running;
    bool debug_mode;
    char cwd[FORGE_MAX_PATH];
    char hostname[256];
    char username[64];
    int  last_exit_code;
    struct termios orig_termios;
    bool raw_mode_active;
} forge_runtime_t;

extern forge_runtime_t g_forge;

/* ── Shared Utility Functions ────────────────────────────────────────────── */
void        forge_init(void);
void        forge_shutdown(void);
void        forge_print_banner(void);
const char *forge_version(void);
void        forge_enable_raw_mode(void);
void        forge_disable_raw_mode(void);
int         forge_get_terminal_size(int *rows, int *cols);
char       *forge_trim(char *str);
char       *forge_strdup(const char *s);
bool        forge_str_ends_with(const char *str, const char *suffix);
void        forge_sleep_ms(int ms);

#endif /* FORGEOS_H */
