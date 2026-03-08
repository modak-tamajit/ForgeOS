/*
 * ForgeOS - Shell Built-ins & Readline
 * src/shell/shell_builtins.c
 *
 * Implements: cd, pwd, echo, export, unset, exit, help,
 *             history, jobs, fg, bg, alias, source, clear, mem
 * Also: a minimal readline with arrow-key history browsing,
 *       tab-completion, and Ctrl-C/Ctrl-L handling.
 */

#include "../../include/shell.h"
#include "../../include/memory.h"
#include "../../include/editor.h"
#include "../../include/server.h"
#include "../../include/process.h"

/* ── Built-in Handlers ───────────────────────────────────────────────────── */

static int builtin_cd(forge_shell_t *sh, forge_cmd_t *cmd) {
    const char *dir;
    if (cmd->argc < 2) {
        dir = getenv("HOME");
        if (!dir) dir = "/";
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        dir = getenv("OLDPWD");
        if (!dir) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
    } else {
        dir = cmd->argv[1];
    }

    char old[FORGE_MAX_PATH];
    if (getcwd(old, sizeof(old))) setenv("OLDPWD", old, 1);

    if (chdir(dir) != 0) {
        fprintf(stderr, "forge: cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }
    char new_cwd[FORGE_MAX_PATH];
    if (getcwd(new_cwd, sizeof(new_cwd))) {
        setenv("PWD", new_cwd, 1);
        strncpy(g_forge.cwd, new_cwd, sizeof(g_forge.cwd) - 1);
    }
    UNUSED(sh);
    return 0;
}

static int builtin_pwd(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(cmd); UNUSED(sh);
    char cwd[FORGE_MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
    return 0;
}

static int builtin_echo(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    bool newline = true;
    int start = 1;
    if (cmd->argc > 1 && strcmp(cmd->argv[1], "-n") == 0) {
        newline = false;
        start = 2;
    }
    for (int i = start; i < cmd->argc; i++) {
        if (i > start) putchar(' ');
        /* Interpret common escape sequences */
        const char *s = cmd->argv[i];
        while (*s) {
            if (*s == '\\' && *(s+1)) {
                s++;
                switch (*s) {
                    case 'n':  putchar('\n'); break;
                    case 't':  putchar('\t'); break;
                    case 'r':  putchar('\r'); break;
                    case '\\': putchar('\\'); break;
                    case 'e':  putchar('\033'); break;
                    default:   putchar('\\'); putchar(*s); break;
                }
            } else {
                putchar(*s);
            }
            s++;
        }
    }
    if (newline) putchar('\n');
    return 0;
}

static int builtin_export(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    if (cmd->argc < 2) {
        extern char **environ;
        for (char **e = environ; *e; e++) printf("export %s\n", *e);
        return 0;
    }
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (eq) {
            char name[128]; int nlen = (int)(eq - cmd->argv[i]);
            strncpy(name, cmd->argv[i], nlen); name[nlen] = '\0';
            setenv(name, eq + 1, 1);
        } else {
            /* export existing var */
            const char *val = getenv(cmd->argv[i]);
            if (val) setenv(cmd->argv[i], val, 1);
        }
    }
    return 0;
}

static int builtin_unset(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    for (int i = 1; i < cmd->argc; i++) unsetenv(cmd->argv[i]);
    return 0;
}

static int builtin_exit(forge_shell_t *sh, forge_cmd_t *cmd) {
    int code = (cmd->argc > 1) ? atoi(cmd->argv[1]) : sh->last_status;
    g_forge.running = false;
    sh->last_status = code;
    return FORGE_EXIT;
}

static int builtin_history(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(cmd);
    history_print(&sh->history);
    return 0;
}

static int builtin_jobs(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(cmd);
    for (int i = 0; i < sh->job_count; i++) {
        forge_job_t *j = &sh->jobs[i];
        const char *status_str =
            j->status == JOB_RUNNING  ? "Running" :
            j->status == JOB_STOPPED  ? "Stopped" : "Done";
        printf("[%d] %-10s  %d  %s\n", j->id, status_str, j->pid, j->cmd);
    }
    return 0;
}

static int builtin_clear(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh); UNUSED(cmd);
    printf(T_CLEAR);
    fflush(stdout);
    return 0;
}

static int builtin_mem(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh); UNUSED(cmd);
    forge_mem_print_stats();
    forge_mem_check_integrity();
    return 0;
}

static int builtin_edit(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    extern int editor_main(const char *filename);
    return editor_main(cmd->argc > 1 ? cmd->argv[1] : NULL);
}

static int builtin_server(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    extern int server_main(int port, const char *root);
    int port = (cmd->argc > 1) ? atoi(cmd->argv[1]) : SERVER_DEFAULT_PORT;
    const char *root = (cmd->argc > 2) ? cmd->argv[2] : SERVER_DEFAULT_ROOT;
    printf(C_BGRN "[forge] Starting HTTP server on port %d, root=%s\n" C_RESET, port, root);
    printf(C_DIM  "        Press Ctrl-C to stop.\n" C_RESET);
    return server_main(port, root);
}

static int builtin_top(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh); UNUSED(cmd);
    extern int procmon_main(void);
    return procmon_main();
}

static int builtin_help(forge_shell_t *sh, forge_cmd_t *cmd);

/* ── Built-in Table ──────────────────────────────────────────────────────── */
static const builtin_t s_builtins[] = {
    { "cd",      builtin_cd,      "cd [DIR]             Change working directory"      },
    { "pwd",     builtin_pwd,     "pwd                  Print working directory"        },
    { "echo",    builtin_echo,    "echo [-n] [ARGS...]   Print arguments"               },
    { "export",  builtin_export,  "export [NAME=VAL]    Set environment variable"       },
    { "unset",   builtin_unset,   "unset NAME           Remove environment variable"    },
    { "exit",    builtin_exit,    "exit [CODE]          Exit the shell"                 },
    { "history", builtin_history, "history              Show command history"            },
    { "jobs",    builtin_jobs,    "jobs                 List background jobs"           },
    { "clear",   builtin_clear,   "clear                Clear the terminal"             },
    { "mem",     builtin_mem,     "mem                  Show memory allocator stats"    },
    { "edit",    builtin_edit,    "edit [FILE]          Open the ForgeOS text editor"   },
    { "server",  builtin_server,  "server [PORT] [ROOT] Start the HTTP server"          },
    { "top",     builtin_top,     "top                  Launch process monitor"         },
    { "help",    builtin_help,    "help                 Show this help"                 },
};
#define BUILTIN_COUNT ((int)(sizeof(s_builtins) / sizeof(s_builtins[0])))

static int builtin_help(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh); UNUSED(cmd);
    printf(C_BCYN "\n  ForgeOS v%s — Built-in Commands\n\n" C_RESET, FORGEOS_VERSION_STR);
    for (int i = 0; i < BUILTIN_COUNT; i++)
        printf("  " C_BWHT "%-10s" C_RESET "  %s\n",
               s_builtins[i].name, s_builtins[i].help);
    printf("\n" C_DIM
           "  Operators: | (pipe)  >  >>  <  2>  &  ;  &&  ||\n"
           "  Use Ctrl-C to interrupt, Ctrl-L to clear, Up/Down for history.\n\n"
           C_RESET);
    return 0;
}

/* ── Built-in Dispatcher ─────────────────────────────────────────────────── */
int shell_run_builtin(forge_shell_t *sh, forge_cmd_t *cmd) {
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(s_builtins[i].name, cmd->argv[0]) == 0)
            return s_builtins[i].fn(sh, cmd);
    }
    return FORGE_ERR_NOTFOUND;
}

const builtin_t *shell_get_builtins(int *count) {
    if (count) *count = BUILTIN_COUNT;
    return s_builtins;
}

/* ── History ─────────────────────────────────────────────────────────────── */
void history_add(forge_history_t *h, const char *line) {
    if (!line || !*line) return;
    /* Avoid duplicate consecutive entries */
    if (h->count > 0) {
        int prev = (h->head - 1 + SHELL_MAX_HISTORY) % SHELL_MAX_HISTORY;
        if (strcmp(h->entries[prev], line) == 0) return;
    }
    strncpy(h->entries[h->head], line, FORGE_MAX_INPUT - 1);
    h->head = (h->head + 1) % SHELL_MAX_HISTORY;
    if (h->count < SHELL_MAX_HISTORY) h->count++;
    h->cursor = h->count;
}

const char *history_get(forge_history_t *h, int offset) {
    if (offset < 0 || offset >= h->count) return NULL;
    int idx = (h->head - 1 - offset + SHELL_MAX_HISTORY * 2) % SHELL_MAX_HISTORY;
    return h->entries[idx];
}

void history_print(forge_history_t *h) {
    int start = h->count > 20 ? h->count - 20 : 0;
    for (int i = start; i < h->count; i++) {
        const char *e = history_get(h, h->count - 1 - i);
        if (e) printf("  %4d  %s\n", i + 1, e);
    }
}

void history_save(forge_history_t *h, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    int start = h->count > 200 ? h->count - 200 : 0;
    for (int i = start; i < h->count; i++) {
        const char *e = history_get(h, h->count - 1 - i);
        if (e) fprintf(f, "%s\n", e);
    }
    fclose(f);
}

void history_load(forge_history_t *h, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[FORGE_MAX_INPUT];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (*line) history_add(h, line);
    }
    fclose(f);
}

/* ── Minimal Readline ────────────────────────────────────────────────────── */
/* Supports: printable chars, Backspace/Del, Home/End, Left/Right arrows,
   Up/Down history navigation, Ctrl-C (clear line), Ctrl-L (clear screen),
   Ctrl-A/E (home/end), basic Tab completion (filenames). */

static void rl_redraw(const char *prompt, const char *buf, int len, int pos) {
    printf("\r" T_CLEAR_LINE "%s%.*s", prompt, len, buf);
    /* Move cursor back if not at end */
    if (pos < len) {
        printf("\033[%dD", len - pos);
    }
    fflush(stdout);
}

static int rl_read_escape(void) {
    char seq[4] = {0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
    if (seq[0] != '[' && seq[0] != 'O') return seq[0];
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            char c; if (read(STDIN_FILENO, &c, 1) != 1) return '\033';
            if (c == '~') {
                switch (seq[1]) {
                    case '1': case '7': return EKEY_HOME;
                    case '4': case '8': return EKEY_END;
                    case '3': return EKEY_DEL;
                    case '5': return EKEY_PAGE_UP;
                    case '6': return EKEY_PAGE_DOWN;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return EKEY_ARROW_UP;
                case 'B': return EKEY_ARROW_DOWN;
                case 'C': return EKEY_ARROW_RIGHT;
                case 'D': return EKEY_ARROW_LEFT;
                case 'H': return EKEY_HOME;
                case 'F': return EKEY_END;
            }
        }
    }
    return '\033';
}

/* Simple filename tab completion */
static int rl_tab_complete(const char *buf, int len, char *out, int outsz) {
    /* Find the start of the last word */
    int word_start = len;
    while (word_start > 0 && buf[word_start-1] != ' ') word_start--;
    const char *prefix = buf + word_start;

    /* Look for matching files */
    DIR *dir = opendir(".");
    if (!dir) return 0;

    char match[FORGE_MAX_PATH] = "";
    int matches = 0;
    struct dirent *de;
    size_t plen = strlen(prefix);
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, prefix, plen) == 0) {
            if (matches == 0) strncpy(match, de->d_name, sizeof(match) - 1);
            matches++;
        }
    }
    closedir(dir);

    if (matches == 1) {
        /* Unique completion: replace prefix with full name */
        strncpy(out, buf, word_start);
        strncat(out, match, outsz - word_start - 1);
        struct stat st;
        if (stat(match, &st) == 0 && S_ISDIR(st.st_mode))
            strncat(out, "/", outsz - strlen(out) - 1);
        return (int)strlen(out);
    }
    return -1; /* no unique completion */
}

int shell_readline(forge_shell_t *sh, char *buf, int bufsz) {
    forge_enable_raw_mode();

    int len = 0, pos = 0;
    int hist_offset = -1;
    char saved[FORGE_MAX_INPUT] = "";
    memset(buf, 0, bufsz);

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            forge_disable_raw_mode();
            return -1;
        }

        if (c == '\r' || c == '\n') {
            buf[len] = '\0';
            printf("\r\n");
            forge_disable_raw_mode();
            return len;
        }

        if (c == '\033') {
            int key = rl_read_escape();
            if (key == EKEY_ARROW_UP || key == EKEY_ARROW_DOWN) {
                if (hist_offset == -1) strncpy(saved, buf, FORGE_MAX_INPUT - 1);
                hist_offset += (key == EKEY_ARROW_UP) ? 1 : -1;
                if (hist_offset < 0) {
                    hist_offset = -1;
                    strncpy(buf, saved, bufsz - 1);
                } else {
                    const char *e = history_get(&sh->history, hist_offset);
                    if (e) strncpy(buf, e, bufsz - 1);
                    else    hist_offset = MAX(0, sh->history.count - 1);
                }
                len = pos = (int)strlen(buf);
                rl_redraw(sh->prompt, buf, len, pos);
            } else if (key == EKEY_ARROW_LEFT  && pos > 0)   { pos--; rl_redraw(sh->prompt, buf, len, pos); }
            else if (key == EKEY_ARROW_RIGHT && pos < len)   { pos++; rl_redraw(sh->prompt, buf, len, pos); }
            else if (key == EKEY_HOME)                       { pos = 0;   rl_redraw(sh->prompt, buf, len, pos); }
            else if (key == EKEY_END)                        { pos = len; rl_redraw(sh->prompt, buf, len, pos); }
            else if (key == EKEY_DEL && pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--; buf[len] = '\0';
                rl_redraw(sh->prompt, buf, len, pos);
            }
            continue;
        }

        /* Ctrl-C */
        if (c == CTRL_KEY('c')) {
            printf("^C\r\n");
            buf[0] = '\0'; len = 0; pos = 0;
            forge_disable_raw_mode();
            return 0;
        }

        /* Ctrl-L */
        if (c == CTRL_KEY('l')) {
            printf(T_CLEAR);
            shell_print_prompt(sh);
            rl_redraw(sh->prompt, buf, len, pos);
            continue;
        }

        /* Ctrl-A / Ctrl-E */
        if (c == CTRL_KEY('a')) { pos = 0;   rl_redraw(sh->prompt, buf, len, pos); continue; }
        if (c == CTRL_KEY('e')) { pos = len; rl_redraw(sh->prompt, buf, len, pos); continue; }

        /* Ctrl-K: kill to end */
        if (c == CTRL_KEY('k')) {
            buf[pos] = '\0'; len = pos;
            rl_redraw(sh->prompt, buf, len, pos);
            continue;
        }

        /* Ctrl-U: kill to beginning */
        if (c == CTRL_KEY('u')) {
            memmove(buf, buf + pos, len - pos);
            len -= pos; pos = 0; buf[len] = '\0';
            rl_redraw(sh->prompt, buf, len, pos);
            continue;
        }

        /* Ctrl-W: kill word backward */
        if (c == CTRL_KEY('w')) {
            int start = pos;
            while (start > 0 && buf[start-1] == ' ') start--;
            while (start > 0 && buf[start-1] != ' ') start--;
            int del = pos - start;
            memmove(buf + start, buf + pos, len - pos);
            len -= del; pos = start; buf[len] = '\0';
            rl_redraw(sh->prompt, buf, len, pos);
            continue;
        }

        /* Tab completion */
        if (c == '\t') {
            char completed[FORGE_MAX_INPUT];
            int newlen = rl_tab_complete(buf, len, completed, sizeof(completed));
            if (newlen > 0) {
                strncpy(buf, completed, bufsz - 1);
                len = pos = newlen;
                rl_redraw(sh->prompt, buf, len, pos);
            }
            continue;
        }

        /* Backspace */
        if (c == 127 || c == '\b') {
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos);
                pos--; len--; buf[len] = '\0';
                rl_redraw(sh->prompt, buf, len, pos);
            }
            continue;
        }

        /* Printable characters */
        if (isprint((unsigned char)c) && len < bufsz - 1) {
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = c; pos++; len++; buf[len] = '\0';
            rl_redraw(sh->prompt, buf, len, pos);
        }
    }
}
