/*
 * ForgeOS - Unix-like Command Shell
 * include/shell.h
 */

#ifndef FORGE_SHELL_H
#define FORGE_SHELL_H

#include "forgeos.h"

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define SHELL_MAX_PIPES      16
#define SHELL_MAX_HISTORY    FORGE_HISTORY_SIZE
#define SHELL_PROMPT_MAX     512

/* ── Token Types ─────────────────────────────────────────────────────────── */
typedef enum {
    TOK_WORD, TOK_PIPE, TOK_REDIR_IN, TOK_REDIR_OUT,
    TOK_REDIR_APPEND, TOK_REDIR_ERR, TOK_BG, TOK_SEMICOLON,
    TOK_AND, TOK_OR, TOK_EOF,
} token_type_t;

typedef struct {
    token_type_t type;
    char         value[FORGE_MAX_INPUT];
} token_t;

/* ── Command ─────────────────────────────────────────────────────────────── */
typedef struct forge_cmd_s {
    char  *argv[FORGE_MAX_ARGS];
    int    argc;
    char  *infile;
    char  *outfile;
    char  *errfile;
    bool   append_out;
    bool   background;
} forge_cmd_t;

/* ── Pipeline ────────────────────────────────────────────────────────────── */
typedef struct {
    forge_cmd_t commands[SHELL_MAX_PIPES];
    int         count;
    bool        background;
} forge_pipeline_t;

/* ── History ─────────────────────────────────────────────────────────────── */
typedef struct {
    char   entries[SHELL_MAX_HISTORY][FORGE_MAX_INPUT];
    int    count;
    int    head;
    int    cursor;
} forge_history_t;

/* ── Job Control ─────────────────────────────────────────────────────────── */
typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } job_status_t;

typedef struct {
    int          id;
    pid_t        pid;
    job_status_t status;
    char         cmd[FORGE_MAX_INPUT];
} forge_job_t;

/* ── Shell State ─────────────────────────────────────────────────────────── */
typedef struct forge_shell_s {
    forge_history_t history;
    forge_job_t     jobs[64];
    int             job_count;
    bool            interactive;
    char            prompt[SHELL_PROMPT_MAX];
    int             last_status;
} forge_shell_t;

/* Tell plugins.h these types are already fully defined */
#define FORGE_SHELL_TYPES_DEFINED 1

/* ── Public API ──────────────────────────────────────────────────────────── */
int   shell_init(forge_shell_t *sh);
void  shell_destroy(forge_shell_t *sh);
int   shell_run(forge_shell_t *sh);
int   shell_execute_line(forge_shell_t *sh, const char *line);
int   shell_tokenize(const char *input, token_t *tokens, int max_tokens);
int   shell_parse_pipeline(token_t *tokens, int ntok, forge_pipeline_t *pl);
void  shell_free_pipeline(forge_pipeline_t *pl);
int   shell_execute_pipeline(forge_shell_t *sh, forge_pipeline_t *pl);
int   shell_execute_cmd(forge_shell_t *sh, forge_cmd_t *cmd);

typedef int (*builtin_fn_t)(forge_shell_t *sh, forge_cmd_t *cmd);
typedef struct {
    const char  *name;
    builtin_fn_t fn;
    const char  *help;
} builtin_t;

int   shell_run_builtin(forge_shell_t *sh, forge_cmd_t *cmd);
const builtin_t *shell_get_builtins(int *count);

void  history_add(forge_history_t *h, const char *line);
const char *history_get(forge_history_t *h, int offset);
void  history_print(forge_history_t *h);
void  history_save(forge_history_t *h, const char *path);
void  history_load(forge_history_t *h, const char *path);

void  shell_build_prompt(forge_shell_t *sh);
void  shell_print_prompt(forge_shell_t *sh);
int   shell_readline(forge_shell_t *sh, char *buf, int bufsz);

#endif /* FORGE_SHELL_H */
