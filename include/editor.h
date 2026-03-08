/*
 * ForgeOS - Terminal Text Editor
 * include/editor.h
 *
 * A kilo-inspired modal text editor with syntax highlighting,
 * search, and raw terminal I/O — zero dependencies.
 */

#ifndef FORGE_EDITOR_H
#define FORGE_EDITOR_H

#include "forgeos.h"

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define EDITOR_TAB_STOP      4
#define EDITOR_QUIT_TIMES    2       /* Ctrl-Q presses to quit unsaved file  */
#define EDITOR_MAX_FIND_LEN  256
#define EDITOR_STATUS_LEN    256

/* ── Key Definitions ─────────────────────────────────────────────────────── */
#define CTRL_KEY(k)     ((k) & 0x1f)

typedef enum {
    EKEY_NONE       = 0,
    EKEY_ARROW_LEFT  = 1000,
    EKEY_ARROW_RIGHT,
    EKEY_ARROW_UP,
    EKEY_ARROW_DOWN,
    EKEY_PAGE_UP,
    EKEY_PAGE_DOWN,
    EKEY_HOME,
    EKEY_END,
    EKEY_DEL,
    EKEY_BACKSPACE  = 127,
} editor_key_t;

/* ── Syntax Highlighting ─────────────────────────────────────────────────── */
#define HL_NORMAL       0
#define HL_COMMENT      1
#define HL_ML_COMMENT   2
#define HL_KEYWORD1     3   /* control flow: if, while, for, return …     */
#define HL_KEYWORD2     4   /* types: int, char, void, struct …            */
#define HL_STRING       5
#define HL_NUMBER       6
#define HL_MATCH        7   /* search match highlight                      */
#define HL_PREPROCESSOR 8   /* #include, #define …                         */

#define HL_FLAG_NUMBERS   (1 << 0)
#define HL_FLAG_STRINGS   (1 << 1)

typedef struct {
    const char   *filetype;          /* display name e.g. "C"              */
    const char  **extensions;        /* NULL-terminated list               */
    const char  **keywords1;         /* NULL-terminated                    */
    const char  **keywords2;         /* NULL-terminated                    */
    const char   *singleline_comment;
    const char   *ml_comment_start;
    const char   *ml_comment_end;
    int           flags;
} editor_syntax_t;

/* ── Row ─────────────────────────────────────────────────────────────────── */
typedef struct {
    int    idx;             /* row index in file                            */
    char  *chars;           /* raw characters                               */
    int    size;
    char  *render;          /* rendered (tabs expanded)                     */
    int    rsize;
    uint8_t *hl;            /* highlight array (one byte per render char)   */
    bool    hl_open_comment; /* row ends inside a multi-line comment        */
} erow_t;

/* ── Editor State ────────────────────────────────────────────────────────── */
typedef struct {
    /* Cursor */
    int cx, cy;             /* cursor position in chars                     */
    int rx;                 /* cursor position in render                    */

    /* Viewport */
    int rowoff;             /* row scroll offset                            */
    int coloff;             /* col scroll offset                            */
    int screenrows;
    int screencols;

    /* File data */
    erow_t  *rows;
    int      numrows;
    bool     dirty;
    char     filename[FORGE_MAX_PATH];

    /* Status message */
    char     statusmsg[EDITOR_STATUS_LEN];
    time_t   statusmsg_time;

    /* Search state */
    char     last_search[EDITOR_MAX_FIND_LEN];
    int      search_last_match;   /* row index of last match, -1 = none    */
    int      search_direction;    /* 1 = forward, -1 = backward            */

    /* Syntax */
    editor_syntax_t *syntax;

    /* Raw mode saved state (pointer to g_forge.orig_termios) */
    struct termios *saved_termios;

    /* Undo buffer (simple last-action) */
    int    quit_times;
} editor_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
int   editor_init(editor_t *ed);
void  editor_destroy(editor_t *ed);
int   editor_open(editor_t *ed, const char *filename);
int   editor_save(editor_t *ed);
void  editor_run(editor_t *ed);

/* Row operations */
void  editor_insert_row(editor_t *ed, int at, const char *s, size_t len);
void  editor_delete_row(editor_t *ed, int at);
void  editor_row_insert_char(editor_t *ed, erow_t *row, int at, int c);
void  editor_row_delete_char(editor_t *ed, erow_t *row, int at);
void  editor_row_append_str(editor_t *ed, erow_t *row, const char *s, size_t len);
void  editor_update_row(editor_t *ed, erow_t *row);
int   editor_row_cx_to_rx(erow_t *row, int cx);
int   editor_row_rx_to_cx(erow_t *row, int rx);

/* Editing */
void  editor_insert_char(editor_t *ed, int c);
void  editor_insert_newline(editor_t *ed);
void  editor_delete_char(editor_t *ed);

/* I/O */
int   editor_read_key(void);
void  editor_process_keypress(editor_t *ed);
void  editor_refresh_screen(editor_t *ed);
void  editor_draw_rows(editor_t *ed, char **ab, int *ab_len);
void  editor_draw_status_bar(editor_t *ed, char **ab, int *ab_len);
void  editor_draw_message_bar(editor_t *ed, char **ab, int *ab_len);
void  editor_set_status_message(editor_t *ed, const char *fmt, ...);
void  editor_scroll(editor_t *ed);

/* Search */
void  editor_find(editor_t *ed);
void  editor_find_callback(editor_t *ed, char *query, int key);

/* Syntax */
void  editor_select_syntax(editor_t *ed);
void  editor_update_syntax(editor_t *ed, erow_t *row);
int   editor_syntax_to_color(int hl);

/* Prompt */
char *editor_prompt(editor_t *ed, const char *prompt,
                    void (*callback)(editor_t *, char *, int));

#endif /* FORGE_EDITOR_H */

/* Entry point for standalone editor invocation */
int editor_main(const char *filename);
