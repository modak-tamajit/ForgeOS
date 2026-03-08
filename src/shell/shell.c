/*
 * ForgeOS - Shell Core
 * src/shell/shell.c
 *
 * Tokeniser → parser → executor pipeline.
 * Handles pipes, redirection, background jobs, variable expansion.
 */

#include "../../include/shell.h"
#include "../../include/memory.h"
#include "../../include/plugins.h"

extern plugin_registry_t *g_plugin_registry; /* set in main.c */
plugin_registry_t *g_plugin_registry = NULL;

/* ── Init / Destroy ──────────────────────────────────────────────────────── */
int shell_init(forge_shell_t *sh) {
    memset(sh, 0, sizeof(*sh));
    sh->interactive  = isatty(STDIN_FILENO);
    sh->last_status  = 0;
    sh->job_count    = 0;
    shell_build_prompt(sh);

    /* Load history */
    const char *home = getenv("HOME");
    if (home) {
        char path[FORGE_MAX_PATH];
        snprintf(path, sizeof(path), "%s/.forgeos_history", home);
        history_load(&sh->history, path);
    }
    return FORGE_OK;
}

void shell_destroy(forge_shell_t *sh) {
    /* Save history */
    const char *home = getenv("HOME");
    if (home) {
        char path[FORGE_MAX_PATH];
        snprintf(path, sizeof(path), "%s/.forgeos_history", home);
        history_save(&sh->history, path);
    }
    for (int i = 0; i < sh->history.count; i++) {
        /* history strings are static arrays, nothing to free */
    }
    UNUSED(sh);
}

/* ── Prompt Builder ──────────────────────────────────────────────────────── */
void shell_build_prompt(forge_shell_t *sh) {
    char cwd[FORGE_MAX_PATH];
    if (!getcwd(cwd, sizeof(cwd))) strncpy(cwd, "~", sizeof(cwd));

    /* Shorten home directory to ~ */
    const char *home = getenv("HOME");
    char display_cwd[FORGE_MAX_PATH];
    if (home && strncmp(cwd, home, strlen(home)) == 0)
        snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
    else
        strncpy(display_cwd, cwd, sizeof(display_cwd));

    /* Status indicator */
    const char *status_col = sh->last_status == 0 ? C_BGRN : C_BRED;

    snprintf(sh->prompt, sizeof(sh->prompt),
             C_BBLU "%s" C_RESET "@" C_BCYN "forge" C_RESET ":"
             C_BYEL "%s" C_RESET " %s❯" C_RESET " ",
             g_forge.username, display_cwd, status_col);
}

void shell_print_prompt(forge_shell_t *sh) {
    shell_build_prompt(sh);
    printf("%s", sh->prompt);
    fflush(stdout);
}

/* ── Tokeniser ───────────────────────────────────────────────────────────── */
int shell_tokenize(const char *input, token_t *tokens, int max_tokens) {
    int ntok = 0;
    const char *p = input;

    while (*p && ntok < max_tokens - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        token_t *tok = &tokens[ntok];
        memset(tok, 0, sizeof(*tok));

        /* Two-char operators first */
        if (p[0] == '>' && p[1] == '>') { tok->type = TOK_REDIR_APPEND; p += 2; ntok++; continue; }
        if (p[0] == '2' && p[1] == '>') { tok->type = TOK_REDIR_ERR;    p += 2; ntok++; continue; }
        if (p[0] == '&' && p[1] == '&') { tok->type = TOK_AND;           p += 2; ntok++; continue; }
        if (p[0] == '|' && p[1] == '|') { tok->type = TOK_OR;            p += 2; ntok++; continue; }

        /* Single-char operators */
        if (*p == '|') { tok->type = TOK_PIPE;      p++; ntok++; continue; }
        if (*p == '<') { tok->type = TOK_REDIR_IN;  p++; ntok++; continue; }
        if (*p == '>') { tok->type = TOK_REDIR_OUT; p++; ntok++; continue; }
        if (*p == '&') { tok->type = TOK_BG;        p++; ntok++; continue; }
        if (*p == ';') { tok->type = TOK_SEMICOLON; p++; ntok++; continue; }

        /* Comment */
        if (*p == '#') break;

        /* Word (possibly quoted) */
        tok->type = TOK_WORD;
        int vi = 0;
        while (*p && *p != ' ' && *p != '\t' &&
               *p != '|' && *p != '<' && *p != '>' &&
               *p != '&' && *p != ';' && vi < (int)sizeof(tok->value) - 1) {
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                while (*p && *p != quote && vi < (int)sizeof(tok->value) - 1)
                    tok->value[vi++] = *p++;
                if (*p == quote) p++;
            } else if (*p == '\\' && *(p+1)) {
                p++;
                tok->value[vi++] = *p++;
            } else {
                tok->value[vi++] = *p++;
            }
        }
        tok->value[vi] = '\0';
        ntok++;
    }

    tokens[ntok].type = TOK_EOF;
    return ntok;
}

/* ── Pipeline Parser ─────────────────────────────────────────────────────── */
int shell_parse_pipeline(token_t *tokens, int ntok, forge_pipeline_t *pl) {
    memset(pl, 0, sizeof(*pl));
    int cmd_idx = 0;
    forge_cmd_t *cmd = &pl->commands[0];

    for (int i = 0; i <= ntok; i++) {
        token_t *tok = &tokens[i];

        if (tok->type == TOK_EOF || tok->type == TOK_SEMICOLON) {
            if (cmd->argc > 0) { cmd_idx++; }
            break;
        }

        if (tok->type == TOK_PIPE) {
            if (cmd->argc == 0) { forge_err("syntax: empty command before |"); return FORGE_ERR; }
            cmd_idx++;
            if (cmd_idx >= SHELL_MAX_PIPES) { forge_err("too many pipes"); return FORGE_ERR; }
            cmd = &pl->commands[cmd_idx];
            continue;
        }

        if (tok->type == TOK_BG) { pl->background = true; cmd->background = true; continue; }

        if (tok->type == TOK_REDIR_IN) {
            if (tokens[i+1].type == TOK_WORD) cmd->infile  = tokens[++i].value;
            continue;
        }
        if (tok->type == TOK_REDIR_OUT) {
            if (tokens[i+1].type == TOK_WORD) { cmd->outfile = tokens[++i].value; cmd->append_out = false; }
            continue;
        }
        if (tok->type == TOK_REDIR_APPEND) {
            if (tokens[i+1].type == TOK_WORD) { cmd->outfile = tokens[++i].value; cmd->append_out = true; }
            continue;
        }
        if (tok->type == TOK_REDIR_ERR) {
            if (tokens[i+1].type == TOK_WORD) cmd->errfile = tokens[++i].value;
            continue;
        }

        if (tok->type == TOK_WORD) {
            if (cmd->argc < FORGE_MAX_ARGS - 1) {
                cmd->argv[cmd->argc++] = tok->value;
            }
        }
    }

    pl->count = cmd_idx;
    return FORGE_OK;
}

/* ── Expand simple $VAR references ──────────────────────────────────────── */
static void expand_vars(forge_cmd_t *cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        if (cmd->argv[i][0] == '$') {
            const char *val = getenv(cmd->argv[i] + 1);
            if (val) cmd->argv[i] = (char *)val;
        }
    }
}

/* ── Execute a Single Command ────────────────────────────────────────────── */
int shell_execute_cmd(forge_shell_t *sh, forge_cmd_t *cmd) {
    if (cmd->argc == 0) return 0;

    expand_vars(cmd);

    /* Try built-ins first */
    int bi = shell_run_builtin(sh, cmd);
    if (bi != FORGE_ERR_NOTFOUND) return bi;

    /* Try plugin commands */
    if (g_plugin_registry) {
        plugin_command_t *pcmd = plugin_find_command(g_plugin_registry, cmd->argv[0]);
        if (pcmd) return pcmd->handler(sh, cmd);
    }

    /* External command */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return FORGE_ERR; }

    if (pid == 0) {
        /* ── Child ── */

        /* stdin redirection */
        if (cmd->infile) {
            int fd = open(cmd->infile, O_RDONLY);
            if (fd < 0) { perror(cmd->infile); exit(1); }
            dup2(fd, STDIN_FILENO); close(fd);
        }
        /* stdout redirection */
        if (cmd->outfile) {
            int flags = O_WRONLY | O_CREAT | (cmd->append_out ? O_APPEND : O_TRUNC);
            int fd = open(cmd->outfile, flags, 0644);
            if (fd < 0) { perror(cmd->outfile); exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
        }
        /* stderr redirection */
        if (cmd->errfile) {
            int fd = open(cmd->errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(cmd->errfile); exit(1); }
            dup2(fd, STDERR_FILENO); close(fd);
        }

        cmd->argv[cmd->argc] = NULL;
        execvp(cmd->argv[0], cmd->argv);
        fprintf(stderr, "forge: command not found: %s\n", cmd->argv[0]);
        exit(127);
    }

    /* ── Parent ── */
    if (cmd->background) {
        printf("[%d] %d\n", sh->job_count + 1, pid);
        if (sh->job_count < 64) {
            forge_job_t *job = &sh->jobs[sh->job_count++];
            job->id = sh->job_count;
            job->pid = pid;
            job->status = JOB_RUNNING;
            strncpy(job->cmd, cmd->argv[0], sizeof(job->cmd) - 1);
        }
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    sh->last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return sh->last_status;
}

/* ── Execute Pipeline ────────────────────────────────────────────────────── */
int shell_execute_pipeline(forge_shell_t *sh, forge_pipeline_t *pl) {
    if (pl->count == 0) return 0;
    if (pl->count == 1) return shell_execute_cmd(sh, &pl->commands[0]);

    /* Multi-command pipeline */
    int pipes[SHELL_MAX_PIPES - 1][2];
    for (int i = 0; i < pl->count - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return FORGE_ERR; }
    }

    pid_t pids[SHELL_MAX_PIPES] = {0};

    for (int i = 0; i < pl->count; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return FORGE_ERR; }

        if (pids[i] == 0) {
            /* Connect stdin from previous pipe */
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            /* Connect stdout to next pipe */
            if (i < pl->count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            /* Close all pipe fds in child */
            for (int j = 0; j < pl->count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            forge_cmd_t *cmd = &pl->commands[i];
            cmd->argv[cmd->argc] = NULL;
            execvp(cmd->argv[0], cmd->argv);
            fprintf(stderr, "forge: %s: command not found\n", cmd->argv[0]);
            exit(127);
        }
    }

    /* Parent: close all pipe fds */
    for (int i = 0; i < pl->count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for all children */
    int last_status = 0;
    for (int i = 0; i < pl->count; i++) {
        int st;
        waitpid(pids[i], &st, 0);
        if (i == pl->count - 1)
            last_status = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }

    sh->last_status = last_status;
    return last_status;
}

/* ── Execute a Line ──────────────────────────────────────────────────────── */
int shell_execute_line(forge_shell_t *sh, const char *line) {
    char buf[FORGE_MAX_INPUT];
    strncpy(buf, line, sizeof(buf) - 1);
    char *trimmed = forge_trim(buf);
    if (!trimmed || *trimmed == '\0' || *trimmed == '#') return 0;

    history_add(&sh->history, trimmed);

    token_t tokens[FORGE_MAX_ARGS * 2];
    int ntok = shell_tokenize(trimmed, tokens, ARRAY_SIZE(tokens));

    forge_pipeline_t pl;
    if (shell_parse_pipeline(tokens, ntok, &pl) != FORGE_OK) return 1;

    return shell_execute_pipeline(sh, &pl);
}

/* ── Main REPL ───────────────────────────────────────────────────────────── */
int shell_run(forge_shell_t *sh) {
    char line[FORGE_MAX_INPUT];

    while (g_forge.running) {
        shell_print_prompt(sh);

        int rc = shell_readline(sh, line, sizeof(line));
        if (rc < 0) {
            if (feof(stdin)) {
                printf("\n");
                break;
            }
            continue;
        }

        int status = shell_execute_line(sh, line);
        if (status == FORGE_EXIT) break;
        sh->last_status = status;
    }

    return sh->last_status;
}
