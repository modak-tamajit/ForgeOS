/*
 * ForgeOS - Terminal Text Editor
 * src/editor/editor.c
 *
 * A kilo-inspired modal editor with:
 *   - Syntax highlighting (C/C++, Python, Shell, Makefile)
 *   - Incremental search (forward/backward)
 *   - File open/save with dirty tracking
 *   - Status bar and message bar
 *   - Tab expansion
 */

#include "../../include/editor.h"
#include "../../include/memory.h"

/* ── Syntax Definitions ──────────────────────────────────────────────────── */
static const char *c_extensions[]  = { ".c", ".h", ".cpp", ".cxx", ".cc", ".hpp", NULL };
static const char *c_keywords1[]   = {
    "if","else","while","for","do","switch","case","break","continue","return",
    "goto","default","sizeof","typedef","struct","union","enum","extern","static",
    "const","volatile","register","inline","restrict","auto","NULL","true","false", NULL };
static const char *c_keywords2[]   = {
    "int","long","short","char","float","double","void","unsigned","signed",
    "uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t",
    "int64_t","size_t","ssize_t","off_t","pid_t","bool","FILE","DIR", NULL };

static const char *py_extensions[] = { ".py", ".pyw", NULL };
static const char *py_keywords1[]  = {
    "and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","finally","for","from","global","if","import","in",
    "is","lambda","nonlocal","not","or","pass","raise","return","try","while",
    "with","yield","True","False","None", NULL };
static const char *py_keywords2[]  = {
    "int","str","float","bool","list","dict","tuple","set","bytes","type",
    "object","Exception","print","len","range","enumerate","zip", NULL };

static const char *sh_extensions[] = { ".sh", ".bash", ".zsh", NULL };
static const char *sh_keywords1[]  = {
    "if","then","else","elif","fi","for","while","do","done","case","esac",
    "in","break","continue","return","exit","local","export","declare",
    "function","select","until","shift","source",".", NULL };
static const char *sh_keywords2[]  = {
    "echo","printf","read","cd","pwd","ls","cp","mv","rm","mkdir","chmod",
    "grep","sed","awk","find","cat","head","tail","wc","sort","uniq","cut",
    "test","[","[[ ", NULL };

static editor_syntax_t s_syntaxes[] = {
    { "C/C++",  c_extensions,  c_keywords1,  c_keywords2,  "//", "/*", "*/", HL_FLAG_NUMBERS | HL_FLAG_STRINGS },
    { "Python", py_extensions, py_keywords1, py_keywords2, "#",  NULL,  NULL, HL_FLAG_NUMBERS | HL_FLAG_STRINGS },
    { "Shell",  sh_extensions, sh_keywords1, sh_keywords2, "#",  NULL,  NULL, HL_FLAG_NUMBERS | HL_FLAG_STRINGS },
};
#define SYNTAX_COUNT  ((int)(sizeof(s_syntaxes)/sizeof(s_syntaxes[0])))

/* ── Append Buffer ───────────────────────────────────────────────────────── */
static void ab_append(char **ab, int *len, const char *s, int slen) {
    *ab = realloc(*ab, *len + slen + 1);
    memcpy(*ab + *len, s, slen);
    *len += slen;
    (*ab)[*len] = '\0';
}
#define AB_APPEND(ab, len, s)  ab_append(ab, len, s, (int)strlen(s))

/* ── Init / Destroy ──────────────────────────────────────────────────────── */
int editor_init(editor_t *ed) {
    memset(ed, 0, sizeof(*ed));
    ed->cx = ed->cy = 0;
    ed->rx = 0;
    ed->rowoff = ed->coloff = 0;
    ed->numrows = 0;
    ed->rows    = NULL;
    ed->dirty   = false;
    ed->filename[0] = '\0';
    ed->syntax  = NULL;
    ed->search_last_match = -1;
    ed->search_direction  =  1;
    ed->quit_times = EDITOR_QUIT_TIMES;

    if (forge_get_terminal_size(&ed->screenrows, &ed->screencols) != FORGE_OK) {
        ed->screenrows = 24; ed->screencols = 80;
    }
    ed->screenrows -= 2; /* status bar + message bar */
    editor_set_status_message(ed, "HELP: Ctrl-S=Save  Ctrl-F=Find  Ctrl-Q=Quit");
    return FORGE_OK;
}

void editor_destroy(editor_t *ed) {
    for (int i = 0; i < ed->numrows; i++) {
        free(ed->rows[i].chars);
        free(ed->rows[i].render);
        free(ed->rows[i].hl);
    }
    free(ed->rows);
    ed->rows = NULL;
    ed->numrows = 0;
}

/* ── Row Operations ──────────────────────────────────────────────────────── */
void editor_update_row(editor_t *ed, erow_t *row) {
    /* Expand tabs in render buffer */
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (EDITOR_TAB_STOP - 1) + 1);
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % EDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editor_update_syntax(ed, row);
}

void editor_insert_row(editor_t *ed, int at, const char *s, size_t len) {
    if (at < 0 || at > ed->numrows) return;
    ed->rows = realloc(ed->rows, sizeof(erow_t) * (ed->numrows + 1));
    memmove(&ed->rows[at + 1], &ed->rows[at], sizeof(erow_t) * (ed->numrows - at));

    erow_t *row = &ed->rows[at];
    row->idx  = at;
    row->size = (int)len;
    row->chars = malloc(len + 1);
    memcpy(row->chars, s, len);
    row->chars[len] = '\0';
    row->render = NULL;
    row->rsize  = 0;
    row->hl     = NULL;
    row->hl_open_comment = false;

    /* Update indices of subsequent rows */
    for (int i = at + 1; i <= ed->numrows; i++) ed->rows[i].idx = i;

    editor_update_row(ed, row);
    ed->numrows++;
    ed->dirty = true;
}

void editor_delete_row(editor_t *ed, int at) {
    if (at < 0 || at >= ed->numrows) return;
    free(ed->rows[at].chars);
    free(ed->rows[at].render);
    free(ed->rows[at].hl);
    memmove(&ed->rows[at], &ed->rows[at + 1], sizeof(erow_t) * (ed->numrows - at - 1));
    for (int i = at; i < ed->numrows - 1; i++) ed->rows[i].idx = i;
    ed->numrows--;
    ed->dirty = true;
}

void editor_row_insert_char(editor_t *ed, erow_t *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = (char)c;
    row->size++;
    editor_update_row(ed, row);
    ed->dirty = true;
}

void editor_row_delete_char(editor_t *ed, erow_t *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_update_row(ed, row);
    ed->dirty = true;
}

void editor_row_append_str(editor_t *ed, erow_t *row, const char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';
    editor_update_row(ed, row);
    ed->dirty = true;
}

int editor_row_cx_to_rx(erow_t *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

int editor_row_rx_to_cx(erow_t *row, int rx) {
    int cur_rx = 0;
    for (int cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            cur_rx += (EDITOR_TAB_STOP - 1) - (cur_rx % EDITOR_TAB_STOP);
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return row->size;
}

/* ── Editing ─────────────────────────────────────────────────────────────── */
void editor_insert_char(editor_t *ed, int c) {
    if (ed->cy == ed->numrows) editor_insert_row(ed, ed->numrows, "", 0);
    editor_row_insert_char(ed, &ed->rows[ed->cy], ed->cx, c);
    ed->cx++;
}

void editor_insert_newline(editor_t *ed) {
    if (ed->cx == 0) {
        editor_insert_row(ed, ed->cy, "", 0);
    } else {
        erow_t *row = &ed->rows[ed->cy];
        editor_insert_row(ed, ed->cy + 1, &row->chars[ed->cx], row->size - ed->cx);
        row = &ed->rows[ed->cy]; /* refresh pointer after realloc */
        row->size = ed->cx;
        row->chars[row->size] = '\0';
        editor_update_row(ed, row);
    }
    ed->cy++;
    ed->cx = 0;
}

void editor_delete_char(editor_t *ed) {
    if (ed->cy == ed->numrows) return;
    if (ed->cx == 0 && ed->cy == 0) return;

    erow_t *row = &ed->rows[ed->cy];
    if (ed->cx > 0) {
        editor_row_delete_char(ed, row, ed->cx - 1);
        ed->cx--;
    } else {
        ed->cx = ed->rows[ed->cy - 1].size;
        editor_row_append_str(ed, &ed->rows[ed->cy - 1], row->chars, row->size);
        editor_delete_row(ed, ed->cy);
        ed->cy--;
    }
}

/* ── File I/O ────────────────────────────────────────────────────────────── */
int editor_open(editor_t *ed, const char *filename) {
    strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
    editor_select_syntax(ed);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* New file is OK */
        if (errno == ENOENT) {
            editor_set_status_message(ed, "New file: %s", filename);
            return FORGE_OK;
        }
        forge_err("editor_open: %s", strerror(errno));
        return FORGE_ERR_IO;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            linelen--;
        editor_insert_row(ed, ed->numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    ed->dirty = false;
    return FORGE_OK;
}

int editor_save(editor_t *ed) {
    if (ed->filename[0] == '\0') {
        char *name = editor_prompt(ed, "Save as: %s (ESC to cancel)", NULL);
        if (!name) { editor_set_status_message(ed, "Save aborted."); return FORGE_ERR; }
        strncpy(ed->filename, name, sizeof(ed->filename) - 1);
        free(name);
        editor_select_syntax(ed);
    }

    /* Build full file content */
    size_t total = 0;
    for (int i = 0; i < ed->numrows; i++)
        total += ed->rows[i].size + 1;

    char *buf = malloc(total + 1);
    if (!buf) return FORGE_ERR_NOMEM;

    char *p = buf;
    for (int i = 0; i < ed->numrows; i++) {
        memcpy(p, ed->rows[i].chars, ed->rows[i].size);
        p += ed->rows[i].size;
        *p++ = '\n';
    }
    *p = '\0';

    int fd = open(ed->filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        free(buf);
        editor_set_status_message(ed, "Can't save: %s", strerror(errno));
        return FORGE_ERR_IO;
    }
    ssize_t written = write(fd, buf, total);
    close(fd);
    free(buf);

    if (written != (ssize_t)total) {
        editor_set_status_message(ed, "Write error: %s", strerror(errno));
        return FORGE_ERR_IO;
    }

    ed->dirty = false;
    editor_set_status_message(ed, "%zu bytes written to %s", total, ed->filename);
    return FORGE_OK;
}

/* ── Syntax Highlighting ─────────────────────────────────────────────────── */
void editor_select_syntax(editor_t *ed) {
    ed->syntax = NULL;
    if (ed->filename[0] == '\0') return;
    for (int i = 0; i < SYNTAX_COUNT; i++) {
        editor_syntax_t *s = &s_syntaxes[i];
        for (int j = 0; s->extensions[j]; j++) {
            if (forge_str_ends_with(ed->filename, s->extensions[j])) {
                ed->syntax = s;
                /* Re-highlight all rows */
                for (int r = 0; r < ed->numrows; r++)
                    editor_update_syntax(ed, &ed->rows[r]);
                return;
            }
        }
    }
}

static bool is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}|&^!?:", c) != NULL;
}

void editor_update_syntax(editor_t *ed, erow_t *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (!ed->syntax) return;

    const char *scs = ed->syntax->singleline_comment;
    const char *mcs = ed->syntax->ml_comment_start;
    const char *mce = ed->syntax->ml_comment_end;
    int scs_len = scs ? (int)strlen(scs) : 0;
    int mcs_len = mcs ? (int)strlen(mcs) : 0;
    int mce_len = mce ? (int)strlen(mce) : 0;

    bool prev_sep  = true;
    bool in_string = false;
    char string_char = 0;
    bool in_comment = (row->idx > 0 && ed->rows[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        uint8_t prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        /* Single-line comment */
        if (!in_string && !in_comment && scs_len && !strncmp(&row->render[i], scs, scs_len)) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }

        /* Multi-line comment */
        if (!in_string && mcs_len) {
            if (in_comment) {
                row->hl[i] = HL_ML_COMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_ML_COMMENT, mce_len);
                    i += mce_len;
                    in_comment = false;
                } else { i++; }
                prev_sep = true;
                continue;
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_ML_COMMENT, mcs_len);
                i += mcs_len;
                in_comment = true;
                continue;
            }
        }

        /* Preprocessor (#include / #define) */
        if (i == 0 && c == '#') {
            memset(&row->hl[i], HL_PREPROCESSOR, row->rsize - i);
            break;
        }

        /* Strings */
        if (ed->syntax->flags & HL_FLAG_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) { row->hl[++i] = HL_STRING; }
                if (c == string_char) in_string = false;
                i++; prev_sep = false; continue;
            } else if (c == '"' || c == '\'') {
                in_string = true; string_char = c;
                row->hl[i] = HL_STRING;
                i++; prev_sep = false; continue;
            }
        }

        /* Numbers */
        if (ed->syntax->flags & HL_FLAG_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER) ||
                ((c == 'x' || c == 'X') && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++; prev_sep = false; continue;
            }
        }

        /* Keywords */
        if (prev_sep) {
            bool matched = false;
            for (int kp = 0; kp < 2 && !matched; kp++) {
                const char **kw = kp == 0 ? ed->syntax->keywords1 : ed->syntax->keywords2;
                uint8_t hl_type = kp == 0 ? HL_KEYWORD1 : HL_KEYWORD2;
                if (!kw) continue;
                for (int ki = 0; kw[ki]; ki++) {
                    int klen = (int)strlen(kw[ki]);
                    if (!strncmp(&row->render[i], kw[ki], klen) &&
                        is_separator(row->render[i + klen])) {
                        memset(&row->hl[i], hl_type, klen);
                        i += klen; matched = true; break;
                    }
                }
            }
            if (matched) { prev_sep = false; continue; }
        }

        prev_sep = is_separator(c);
        i++;
    }

    bool changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < ed->numrows)
        editor_update_syntax(ed, &ed->rows[row->idx + 1]);
}

int editor_syntax_to_color(int hl) {
    switch (hl) {
        case HL_COMMENT:     return 36;   /* cyan    */
        case HL_ML_COMMENT:  return 36;
        case HL_PREPROCESSOR:return 35;   /* magenta */
        case HL_KEYWORD1:    return 33;   /* yellow  */
        case HL_KEYWORD2:    return 32;   /* green   */
        case HL_STRING:      return 31;   /* red     */
        case HL_NUMBER:      return 34;   /* blue    */
        case HL_MATCH:       return 34;
        default:             return 37;   /* white   */
    }
}

/* ── Scroll ──────────────────────────────────────────────────────────────── */
void editor_scroll(editor_t *ed) {
    ed->rx = 0;
    if (ed->cy < ed->numrows)
        ed->rx = editor_row_cx_to_rx(&ed->rows[ed->cy], ed->cx);

    if (ed->cy < ed->rowoff) ed->rowoff = ed->cy;
    if (ed->cy >= ed->rowoff + ed->screenrows) ed->rowoff = ed->cy - ed->screenrows + 1;
    if (ed->rx < ed->coloff) ed->coloff = ed->rx;
    if (ed->rx >= ed->coloff + ed->screencols) ed->coloff = ed->rx - ed->screencols + 1;
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */
void editor_draw_rows(editor_t *ed, char **ab, int *ab_len) {
    for (int y = 0; y < ed->screenrows; y++) {
        int filerow = y + ed->rowoff;

        if (filerow >= ed->numrows) {
            /* Welcome message on empty file */
            if (ed->numrows == 0 && y == ed->screenrows / 3) {
                char welcome[80];
                int wlen = snprintf(welcome, sizeof(welcome),
                    "ForgeOS Editor -- v%s", FORGEOS_VERSION_STR);
                if (wlen > ed->screencols) wlen = ed->screencols;
                int padding = (ed->screencols - wlen) / 2;
                if (padding) { AB_APPEND(ab, ab_len, "~"); padding--; }
                while (padding--) AB_APPEND(ab, ab_len, " ");
                ab_append(ab, ab_len, welcome, wlen);
            } else {
                AB_APPEND(ab, ab_len, "~");
            }
        } else {
            erow_t *row = &ed->rows[filerow];
            int len = row->rsize - ed->coloff;
            if (len < 0) len = 0;
            if (len > ed->screencols) len = ed->screencols;

            char *c = &row->render[ed->coloff];
            uint8_t *hl = row->hl ? &row->hl[ed->coloff] : NULL;
            int cur_color = -1;

            for (int j = 0; j < len; j++) {
                if (iscntrl((unsigned char)c[j])) {
                    char sym = (c[j] <= 26) ? ('@' + c[j]) : '?';
                    AB_APPEND(ab, ab_len, "\033[7m");
                    ab_append(ab, ab_len, &sym, 1);
                    AB_APPEND(ab, ab_len, "\033[m");
                    if (cur_color != -1) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "\033[%dm", cur_color);
                        AB_APPEND(ab, ab_len, buf);
                    }
                } else if (!hl || hl[j] == HL_NORMAL) {
                    if (cur_color != -1) { AB_APPEND(ab, ab_len, "\033[39m"); cur_color = -1; }
                    ab_append(ab, ab_len, &c[j], 1);
                } else {
                    int color = editor_syntax_to_color(hl[j]);
                    if (color != cur_color) {
                        cur_color = color;
                        char buf[16];
                        snprintf(buf, sizeof(buf), "\033[%dm", color);
                        AB_APPEND(ab, ab_len, buf);
                    }
                    ab_append(ab, ab_len, &c[j], 1);
                }
            }
            AB_APPEND(ab, ab_len, "\033[39m");
        }
        AB_APPEND(ab, ab_len, "\033[K\r\n");
    }
}

void editor_draw_status_bar(editor_t *ed, char **ab, int *ab_len) {
    AB_APPEND(ab, ab_len, "\033[7m");   /* reverse video */
    char status[EDITOR_STATUS_LEN], rstatus[80];
    int len = snprintf(status, sizeof(status), " %.20s %s",
        ed->filename[0] ? ed->filename : "[No Name]",
        ed->dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d ",
        ed->syntax ? ed->syntax->filetype : "plain",
        ed->cy + 1, ed->numrows);
    if (len > ed->screencols) len = ed->screencols;
    ab_append(ab, ab_len, status, len);
    while (len < ed->screencols) {
        if (ed->screencols - len == rlen) {
            ab_append(ab, ab_len, rstatus, rlen); break;
        }
        AB_APPEND(ab, ab_len, " "); len++;
    }
    AB_APPEND(ab, ab_len, "\033[m\r\n");
}

void editor_draw_message_bar(editor_t *ed, char **ab, int *ab_len) {
    AB_APPEND(ab, ab_len, "\033[K");
    int msglen = (int)strlen(ed->statusmsg);
    if (msglen > ed->screencols) msglen = ed->screencols;
    if (msglen && time(NULL) - ed->statusmsg_time < 5)
        ab_append(ab, ab_len, ed->statusmsg, msglen);
}

void editor_refresh_screen(editor_t *ed) {
    editor_scroll(ed);

    char *ab = NULL; int ab_len = 0;

    AB_APPEND(&ab, &ab_len, T_CURSOR_HIDE T_CLEAR);

    editor_draw_rows(ed, &ab, &ab_len);
    editor_draw_status_bar(ed, &ab, &ab_len);
    editor_draw_message_bar(ed, &ab, &ab_len);

    /* Position cursor */
    char cursor_pos[32];
    snprintf(cursor_pos, sizeof(cursor_pos), "\033[%d;%dH",
             (ed->cy - ed->rowoff) + 1,
             (ed->rx - ed->coloff) + 1);
    AB_APPEND(&ab, &ab_len, cursor_pos);
    AB_APPEND(&ab, &ab_len, T_CURSOR_SHOW);

    write(STDOUT_FILENO, ab, ab_len);
    free(ab);
}

void editor_set_status_message(editor_t *ed, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->statusmsg, sizeof(ed->statusmsg), fmt, ap);
    va_end(ap);
    ed->statusmsg_time = time(NULL);
}

/* ── Key Reading ─────────────────────────────────────────────────────────── */
int editor_read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) return -1;
    }
    if (c != '\033') return c;

    char seq[4] = {0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            char q; if (read(STDIN_FILENO, &q, 1) != 1) return '\033';
            if (q == '~') switch (seq[1]) {
                case '1': case '7': return EKEY_HOME;
                case '4': case '8': return EKEY_END;
                case '3': return EKEY_DEL;
                case '5': return EKEY_PAGE_UP;
                case '6': return EKEY_PAGE_DOWN;
            }
        } else switch (seq[1]) {
            case 'A': return EKEY_ARROW_UP;
            case 'B': return EKEY_ARROW_DOWN;
            case 'C': return EKEY_ARROW_RIGHT;
            case 'D': return EKEY_ARROW_LEFT;
            case 'H': return EKEY_HOME;
            case 'F': return EKEY_END;
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return EKEY_HOME;
            case 'F': return EKEY_END;
        }
    }
    return '\033';
}

/* ── Search ──────────────────────────────────────────────────────────────── */
void editor_find_callback(editor_t *ed, char *query, int key) {
    /* Restore saved highlight on previous match */
    static int saved_hl_line = -1;
    static uint8_t *saved_hl = NULL;
    if (saved_hl) {
        if (saved_hl_line < ed->numrows)
            memcpy(ed->rows[saved_hl_line].hl, saved_hl, ed->rows[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\033') {
        ed->search_last_match = -1;
        ed->search_direction  =  1;
        return;
    }
    if (key == EKEY_ARROW_RIGHT || key == EKEY_ARROW_DOWN) ed->search_direction =  1;
    else if (key == EKEY_ARROW_LEFT || key == EKEY_ARROW_UP) ed->search_direction = -1;
    else { ed->search_last_match = -1; ed->search_direction = 1; }

    if (ed->search_last_match == -1) ed->search_direction = 1;
    int current = ed->search_last_match;

    for (int i = 0; i < ed->numrows; i++) {
        current += ed->search_direction;
        if (current == -1)            current = ed->numrows - 1;
        if (current == ed->numrows)   current = 0;

        erow_t *row = &ed->rows[current];
        char *match = strstr(row->render, query);
        if (match) {
            ed->search_last_match = current;
            ed->cy      = current;
            ed->cx      = editor_row_rx_to_cx(row, (int)(match - row->render));
            ed->rowoff  = ed->numrows;

            /* Highlight match */
            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editor_find(editor_t *ed) {
    int saved_cx = ed->cx, saved_cy = ed->cy;
    int saved_rowoff = ed->rowoff, saved_coloff = ed->coloff;

    char *query = editor_prompt(ed, "Search: %s (ESC/Arrows/Enter)",
                                editor_find_callback);
    if (query) {
        free(query);
    } else {
        ed->cx = saved_cx; ed->cy = saved_cy;
        ed->rowoff = saved_rowoff; ed->coloff = saved_coloff;
    }
}

/* ── Prompt ──────────────────────────────────────────────────────────────── */
char *editor_prompt(editor_t *ed, const char *prompt,
                    void (*callback)(editor_t *, char *, int)) {
    size_t bufsz = 128;
    char *buf = malloc(bufsz);
    int buflen = 0;
    buf[0] = '\0';

    while (1) {
        editor_set_status_message(ed, prompt, buf);
        editor_refresh_screen(ed);

        int c = editor_read_key();

        if (c == EKEY_DEL || c == CTRL_KEY('h') || c == EKEY_BACKSPACE) {
            if (buflen > 0) buf[--buflen] = '\0';
        } else if (c == '\033') {
            editor_set_status_message(ed, "");
            if (callback) callback(ed, buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen) {
                editor_set_status_message(ed, "");
                if (callback) callback(ed, buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == (int)bufsz - 1) {
                bufsz *= 2;
                buf = realloc(buf, bufsz);
            }
            buf[buflen++] = (char)c;
            buf[buflen]   = '\0';
        }
        if (callback) callback(ed, buf, c);
    }
}

/* ── Keypress Handler ────────────────────────────────────────────────────── */
void editor_process_keypress(editor_t *ed) {
    int c = editor_read_key();

    switch (c) {
        case '\r': editor_insert_newline(ed); break;

        case CTRL_KEY('q'):
            if (ed->dirty && ed->quit_times > 0) {
                ed->quit_times--;
                editor_set_status_message(ed,
                    "File has unsaved changes. Ctrl-Q again to quit (%d)", ed->quit_times);
                return;
            }
            g_forge.running = false;
            printf(T_CLEAR);
            break;

        case CTRL_KEY('s'): editor_save(ed); break;
        case CTRL_KEY('f'): editor_find(ed); break;

        case CTRL_KEY('h'):
        case EKEY_BACKSPACE: editor_delete_char(ed); break;

        case EKEY_DEL:
            if (ed->cx < (ed->cy < ed->numrows ? ed->rows[ed->cy].size : 0)) {
                ed->cx++;
                editor_delete_char(ed);
            }
            break;

        case EKEY_PAGE_UP:
        case EKEY_PAGE_DOWN: {
            int times = ed->screenrows;
            while (times--) {
                if (c == EKEY_PAGE_UP)
                    ed->cy = ed->cy > 0 ? ed->cy - 1 : 0;
                else
                    ed->cy = ed->cy < ed->numrows ? ed->cy + 1 : ed->numrows;
            }
            break;
        }

        case EKEY_HOME: ed->cx = 0; break;
        case EKEY_END:
            if (ed->cy < ed->numrows) ed->cx = ed->rows[ed->cy].size;
            break;

        case EKEY_ARROW_UP:
        case EKEY_ARROW_DOWN:
        case EKEY_ARROW_LEFT:
        case EKEY_ARROW_RIGHT: {
            if (c == EKEY_ARROW_LEFT) {
                if (ed->cx > 0) ed->cx--;
                else if (ed->cy > 0) { ed->cy--; ed->cx = ed->rows[ed->cy].size; }
            } else if (c == EKEY_ARROW_RIGHT) {
                if (ed->cy < ed->numrows && ed->cx < ed->rows[ed->cy].size) ed->cx++;
                else if (ed->cy < ed->numrows) { ed->cy++; ed->cx = 0; }
            } else if (c == EKEY_ARROW_UP) {
                if (ed->cy > 0) ed->cy--;
            } else if (c == EKEY_ARROW_DOWN) {
                if (ed->cy < ed->numrows) ed->cy++;
            }
            /* Clamp cx */
            int rowlen = (ed->cy < ed->numrows) ? ed->rows[ed->cy].size : 0;
            if (ed->cx > rowlen) ed->cx = rowlen;
            break;
        }

        case CTRL_KEY('l'): break;   /* just refresh */
        case '\033':         break;

        default:
            if (!iscntrl(c)) editor_insert_char(ed, c);
            break;
    }

    ed->quit_times = EDITOR_QUIT_TIMES;
}

/* ── Main Loop ───────────────────────────────────────────────────────────── */
void editor_run(editor_t *ed) {
    forge_enable_raw_mode();
    g_forge.running = true;

    editor_set_status_message(ed,
        "Ctrl-S=Save  Ctrl-F=Find  Ctrl-Q=Quit  Arrows=Move  PgUp/PgDn=Scroll");

    while (g_forge.running) {
        editor_refresh_screen(ed);
        editor_process_keypress(ed);
    }

    forge_disable_raw_mode();
    printf(T_CLEAR);
    g_forge.running = true; /* restore for shell */
}

/* ── Entry Point ─────────────────────────────────────────────────────────── */
int editor_main(const char *filename) {
    editor_t ed;
    editor_init(&ed);

    if (filename) {
        if (editor_open(&ed, filename) != FORGE_OK) {
            forge_err("Cannot open file: %s", filename);
            editor_destroy(&ed);
            return 1;
        }
    }

    editor_run(&ed);
    editor_destroy(&ed);
    return 0;
}
