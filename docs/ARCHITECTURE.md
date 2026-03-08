# ForgeOS — System Architecture

## High-Level Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                            ForgeOS                                   │
│                                                                      │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│   │  Shell   │  │  Editor  │  │  Server  │  │ Process Monitor  │   │
│   │          │  │          │  │          │  │                  │   │
│   │ Tokenize │  │ Viewport │  │Thread    │  │ /proc reader     │   │
│   │ Parse    │  │ Rows     │  │Pool (8)  │  │ CPU/MEM delta    │   │
│   │ Execute  │  │ Syntax HL│  │HTTP/1.1  │  │ TUI renderer     │   │
│   │ Readline │  │ Search   │  │Static FS │  │ Keyboard ctrl    │   │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └────────┬─────────┘   │
│        │              │              │                 │              │
│   ┌────┴──────────────┴──────────────┴─────────────────┴──────────┐  │
│   │                     Plugin Registry                            │  │
│   │            dlopen() / forge_plugin_init()                      │  │
│   └────────────────────────────────────────────────────────────────┘  │
│                              │                                        │
│   ┌───────────────────────────────────────────────────────────────┐   │
│   │              forge_malloc / forge_free / forge_calloc          │   │
│   │                  16 MB Static Heap (s_heap[])                  │   │
│   │              Free-list  ←→  Physical Block Chain              │   │
│   └───────────────────────────────────────────────────────────────┘   │
│                              │                                        │
│   ┌───────────────────────────────────────────────────────────────┐   │
│   │             forge_runtime_t (g_forge)                          │   │
│   │   cwd | hostname | username | termios | running | exit_code   │   │
│   └───────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                         Linux Kernel / POSIX
                    fork/exec | pipe | socket | /proc
```

## Module Breakdown

### `src/memory/memory.c` — Custom Allocator
- **Heap**: 16 MB static array (`s_heap[]`)
- **Block layout**: `[header | payload]` where header = `mem_block_t`
- **Free list**: doubly-linked list of free blocks (first-fit)
- **Physical list**: second doubly-linked list ordered by address (enables O(1) coalescing)
- **Operations**: `split_block()` on alloc, `coalesce()` on free
- **Thread safety**: global `pthread_mutex_t s_lock`
- **Debug macros**: `forge_malloc(n)` → `forge_malloc_impl(n, __FILE__, __LINE__)`

### `src/shell/shell.c` — Core Shell
- **Tokeniser** (`shell_tokenize`): single-pass, handles quotes, backslash, all operators
- **Parser** (`shell_parse_pipeline`): builds `forge_pipeline_t` with up to 16 stages
- **Executor** (`shell_execute_pipeline`): creates `pipe(2)` pairs, `fork(2)` per stage, `waitpid` all
- **Variable expansion**: `$VAR` via `getenv()` before exec
- **History**: circular buffer with persistence to `~/.forgeos_history`

### `src/shell/shell_builtins.c` — Built-ins & Readline
- **Built-ins**: `cd`, `pwd`, `echo`, `export`, `unset`, `exit`, `history`, `jobs`, `clear`, `mem`, `edit`, `server`, `top`, `help`
- **Readline**: raw-mode terminal with arrow history, Ctrl-A/E/K/U/W/L, Tab completion

### `src/editor/editor.c` — Text Editor
- Inspired by [kilo](https://github.com/antirez/kilo)
- **Row model**: each row has `chars` (raw) and `render` (tab-expanded) arrays
- **Highlighting**: per-character `hl[]` byte array; updated incrementally
- **Languages**: C/C++, Python, Shell, Makefile
- **Search**: incremental with forward/backward and highlight restoration

### `src/server/server.c` — HTTP Server
- **Thread pool**: `SERVER_THREAD_COUNT` (8) worker threads sharing a task queue
- **Accept loop**: dedicated thread pushes `server_task_t` onto queue
- **HTTP parsing**: line-by-line using `sscanf` and string scanning
- **File serving**: `sendfile`-style read/write loop with `stat(2)` for size
- **Directory listing**: auto-generated HTML when path is a directory

### `src/process/process.c` — Process Monitor
- **Data source**: `/proc/[pid]/stat`, `/proc/stat`, `/proc/meminfo`, `/proc/uptime`
- **CPU delta**: compares `(utime + stime)` between samples with total CPU time delta
- **TUI**: ANSI escape sequences, redraws every `PROC_UPDATE_MS` ms
- **Controls**: `q` quit, `c/m/p/n` sort modes, `↑↓` scroll, `f` filter

### `src/plugins/plugins.c` — Plugin System
- Scans `plugin_dir` for `*.so` files
- `dlopen()` + `dlsym("forge_plugin_init")` → `plugin_descriptor_t`
- ABI version gate prevents loading incompatible plugins
- Commands registered into registry; shell queries registry before `execvp`

## Data Flow: Shell Command Execution

```
User input
    │
    ▼
shell_readline()   ← raw mode, history browsing, tab complete
    │
    ▼
shell_tokenize()   ← produces token_t[] stream
    │
    ▼
shell_parse_pipeline() ← produces forge_pipeline_t
    │
    ├── single cmd ──→ shell_run_builtin()  or  plugin_find_command()  or  execvp()
    │
    └── pipeline ────→ for each stage: fork() → dup2(pipes) → execvp()
                       parent waits for all pids
```

## Memory Allocator: Block Layout

```
Heap: s_heap[0 .. FORGE_HEAP_SIZE-1]
      ┌────────────────┬──────────────────────────────────────┐
      │  mem_block_t   │          payload bytes               │
      │  (header)      │                                      │
      │  magic         │  ← returned to caller                │
      │  size          │                                      │
      │  is_free       │                                      │
      │  phys_next ────┼──→ next block (may be free or alloc) │
      │  phys_prev     │                                      │
      │  free_next ────┼──→ next FREE block (free list only)  │
      │  free_prev     │                                      │
      └────────────────┴──────────────────────────────────────┘
                       ▲
                       │  sizeof(mem_block_t)
                forge_malloc returns this pointer
```

## Thread Model: HTTP Server

```
Main thread
    │
    └──→ accept_thread ──→  accept(listen_fd)
                                │
                                ▼
                          task_queue (mutex-protected)
                                │
                    ┌───────────┼───────────┐
                    ▼           ▼           ▼
                worker_0    worker_1  ... worker_7
                    │
                    ▼
               handle_client()
                    │
                    ├── parse HTTP request
                    ├── resolve path (root_dir + req->path)
                    ├── stat() file
                    ├── send headers
                    └── send file body (read/write loop)
```
