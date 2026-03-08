<div align="center">

```
  ███████╗ ██████╗ ██████╗  ██████╗ ███████╗ ██████╗ ███████╗
  ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝██╔═══██╗██╔════╝
  █████╗  ██║   ██║██████╔╝██║  ███╗█████╗  ██║   ██║███████╗
  ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  ██║   ██║╚════██║
  ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗╚██████╔╝███████║
  ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚══════╝
```

**A Terminal Operating Environment Built in C**

[![Build](https://img.shields.io/badge/build-passing-brightgreen?style=flat-square&logo=gnu)](#)
[![Language](https://img.shields.io/badge/language-C11-blue?style=flat-square&logo=c)](#)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](#)
[![Version](https://img.shields.io/badge/version-1.0.0--Ironclad-cyan?style=flat-square)](#)
[![Lines](https://img.shields.io/badge/~3500-lines_of_C-orange?style=flat-square)](#)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square)](#contributing)

*No libc allocator. No readline. No external dependencies.*

[Features](#-features) · [Architecture](#-architecture) · [Installation](#-installation) · [Usage](#-usage) · [Plugins](#-plugin-system) · [Contributing](#-contributing)

</div>

---

## 🔥 What is ForgeOS?

ForgeOS is a **systems programming showcase** — a self-contained terminal operating
environment written entirely in C11. It demonstrates mastery of:

- **Low-level memory management** — a custom free-list allocator with coalescing
- **Process control** — fork/exec/pipe/wait pipelines with job management
- **Terminal I/O** — raw-mode readline with ANSI sequences, no ncurses
- **Network programming** — multithreaded HTTP/1.1 server using POSIX sockets
- **Dynamic linking** — runtime plugin loading with `dlopen()`/`dlsym()`
- **Systems monitoring** — live `/proc` filesystem reader with TUI renderer

All six subsystems (shell, editor, HTTP server, process monitor, memory allocator,
plugin system) are implemented from scratch. Zero external dependencies beyond
POSIX and pthreads.

---

## ✨ Features

### 🐚 Unix Shell
| Capability | Details |
|-----------|---------|
| **Tokeniser** | Single-pass, handles `'quotes'`, `"double"`, `\escapes` |
| **Operators** | `\|` `>` `>>` `<` `2>` `&` `;` `&&` `\|\|` |
| **Pipelines** | Up to 16-stage pipelines with proper fd management |
| **Background** | `&` backgrounding, `jobs` listing, zombie reaping |
| **Built-ins** | `cd` `pwd` `echo` `export` `unset` `exit` `history` `jobs` `clear` `mem` `edit` `server` `top` `help` |
| **History** | 500-entry circular buffer, `↑↓` navigation, saved to `~/.forgeos_history` |
| **Readline** | Ctrl-A/E/K/U/W/L, cursor movement, Tab completion |

### 📝 Text Editor
| Capability | Details |
|-----------|---------|
| **File I/O** | Open existing files or new buffers, `Ctrl-S` save |
| **Navigation** | Arrow keys, `Page Up/Down`, `Home/End`, scroll viewport |
| **Editing** | Insert, delete, backspace, `Enter` for newlines |
| **Search** | Incremental find (Ctrl-F), forward/backward (arrow keys) |
| **Highlighting** | C/C++, Python, Shell — keywords, types, strings, numbers, comments, preprocessor |
| **Status Bar** | Filename, cursor position, filetype, dirty indicator |

### 🌐 HTTP/1.1 Server
| Capability | Details |
|-----------|---------|
| **Concurrency** | 8-thread pool with mutex-protected task queue |
| **Methods** | GET, HEAD |
| **File Serving** | Static files with `Content-Length` and MIME detection |
| **MIME Types** | 20+ types: html, css, js, json, png, jpg, svg, pdf, wasm… |
| **Directory** | Auto-generated HTML directory listings |
| **Keep-Alive** | HTTP/1.1 persistent connections |
| **Stats** | Request count, bytes transferred, uptime |

### 💾 Memory Allocator
| Capability | Details |
|-----------|---------|
| **Heap** | 16 MB static arena (`s_heap[]`) |
| **Algorithm** | First-fit free-list with immediate coalescing |
| **Layout** | Dual linked lists: physical order + free-only chain |
| **Thread Safety** | `pthread_mutex_t` guards all operations |
| **Debug** | `__FILE__`/`__LINE__` tracked per allocation |
| **Integrity** | Magic-number sentinel check on every block |
| **API** | `forge_malloc` `forge_free` `forge_calloc` `forge_realloc` |

### 📊 Process Monitor (`top`-like)
| Capability | Details |
|-----------|---------|
| **Data Source** | `/proc/[pid]/stat`, `/proc/stat`, `/proc/meminfo` |
| **CPU** | Per-process % computed from tick deltas |
| **Memory** | RSS and VSZ with percentage of total RAM |
| **Sort Modes** | CPU (`c`), Memory (`m`), PID (`p`), Name (`n`) |
| **TUI** | Coloured bars, column headers, scrollable process list |
| **Controls** | `q` quit · `↑↓` scroll · `c/m/p/n` sort |

### 🔌 Plugin System
| Capability | Details |
|-----------|---------|
| **Loading** | `dlopen()` shared libraries from `plugins/` directory |
| **ABI Guard** | Version number prevents loading incompatible plugins |
| **Commands** | Plugins register new shell commands transparently |
| **Hooks** | `on_startup` and `on_shutdown` lifecycle callbacks |
| **Example** | `hello_plugin.so` — `hello` and `cowsay` commands |

---

## 🏗 Architecture

```
forgeos/
├── src/
│   ├── main.c                  ← CLI dispatcher, global runtime, signal handlers
│   ├── shell/
│   │   ├── shell.c             ← Tokeniser, parser, pipeline executor, REPL
│   │   └── shell_builtins.c    ← 14 built-ins + readline implementation
│   ├── editor/
│   │   └── editor.c            ← Row model, viewport, syntax highlighting, search
│   ├── server/
│   │   └── server.c            ← Socket setup, thread pool, HTTP parsing, file serving
│   ├── process/
│   │   └── process.c           ← /proc reader, CPU delta, TUI renderer
│   ├── memory/
│   │   └── memory.c            ← Free-list allocator, coalescing, statistics
│   └── plugins/
│       └── plugins.c           ← dlopen registry, command lookup, hook dispatch
├── include/
│   ├── forgeos.h               ← Master header: types, colors, macros, runtime
│   ├── shell.h                 ← Shell types and API
│   ├── editor.h                ← Editor types and API
│   ├── server.h                ← HTTP server types and API
│   ├── process.h               ← Process monitor types and API
│   ├── memory.h                ← Allocator API and statistics types
│   └── plugins.h               ← Plugin ABI and registry API
├── plugins/
│   └── hello_plugin/
│       └── hello_plugin.c      ← Example plugin (hello + cowsay)
├── tests/
│   └── test_memory.c           ← 9 test suites, 84 assertions
├── docs/
│   ├── ARCHITECTURE.md         ← Detailed module and data-flow diagrams
│   └── PLUGIN_GUIDE.md         ← How to write your own plugin
├── www/
│   └── index.html              ← Demo page served by the HTTP server
└── Makefile                    ← Full build system
```

For detailed data-flow diagrams and module internals, see **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**.

---

## ⚙ Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install gcc make

# Fedora/RHEL
sudo dnf install gcc make

# macOS (requires GNU tools)
brew install gcc make
```

**Requirements**: GCC 9+ or Clang 10+, C11, POSIX.1-2008, pthreads, libdl

### Build

```bash
git clone https://github.com/yourusername/forgeos.git
cd forgeos
make                  # builds bin/forgeos
make test             # runs test suite (should show 84/84 passed)
make plugin           # builds plugins/hello_plugin.so
```

### Build Targets

| Target | Description |
|--------|-------------|
| `make` | Build `bin/forgeos` (optimised, `-O2`) |
| `make run` | Build + launch interactive shell |
| `make test` | Build + run memory allocator test suite |
| `make plugin` | Build the `hello_plugin.so` example |
| `make debug` | Build with `-O0 -g3 -fsanitize=address,undefined` |
| `make clean` | Remove `build/` and `bin/` |
| `make install` | Install to `PREFIX` (default `/usr/local`) |
| `make docs` | Generate Doxygen HTML (requires `doxygen`) |

---

## 🚀 Usage

### Interactive Shell

```bash
./bin/forgeos              # or: make run
```

```
  ███████╗ ██████╗ ██████╗  ...
  ForgeOS v1.0.0 (Ironclad)  |  MIT License
  Type help for available commands.

user@forge:~ ❯ echo "Hello from ForgeOS!"
Hello from ForgeOS!

user@forge:~ ❯ ls | grep .c | wc -l
12

user@forge:~ ❯ cat Makefile > /tmp/backup.mk && echo "backed up"
backed up

user@forge:~ ❯ sleep 10 &
[1] 12345

user@forge:~ ❯ help
```

### Text Editor

```bash
./bin/forgeos -e myfile.c     # open a file
./bin/forgeos -e              # new empty buffer
# Inside shell:
edit myfile.c
```

**Key bindings:**

| Key | Action |
|-----|--------|
| `Ctrl-S` | Save |
| `Ctrl-Q` | Quit (press twice if unsaved) |
| `Ctrl-F` | Find (incremental search) |
| `Arrow keys` | Move cursor |
| `Page Up/Down` | Scroll by screen |
| `Home` / `End` | Start / end of line |

### HTTP Server

```bash
./bin/forgeos -w 8080 ./www   # serve ./www on port 8080
# or from inside the shell:
server 8080 ./www
```

Then visit `http://localhost:8080` to see the ForgeOS demo page.

### Process Monitor

```bash
./bin/forgeos -p
# or from inside the shell:
top
```

**Controls:** `q` quit · `↑↓` scroll · `c` sort by CPU · `m` sort by MEM · `p` sort by PID · `n` sort by name

### Memory Allocator Demo

```bash
./bin/forgeos -m
```

```
┌─────────────── Memory Allocator Statistics ───────────────┐
  Heap capacity    :    16384 KB
  In use now       :     1216 bytes
  Peak usage       :     1216 bytes
  Utilization      :     0.01 %
  Alloc calls      :        3
  Free  calls      :        0
└────────────────────────────────────────────────────────────┘
```

---

## 🔌 Plugin System

### Using the example plugin

```bash
make plugin               # builds plugins/hello_plugin.so
make run

user@forge:~ ❯ hello World
╔══════════════════════════╗
║  Hello, World            ║
╚══════════════════════════╝

user@forge:~ ❯ cowsay ForgeOS rocks
 ---------------
< ForgeOS rocks >
 ---------------
        \   ^__^
         \  (oo)\_______
            (__)\       )\/\
                ||----w |
                ||     ||
```

### Writing your own plugin

See **[docs/PLUGIN_GUIDE.md](docs/PLUGIN_GUIDE.md)** for the full API reference and examples.

```c
// minimal_plugin.c — compile with:
// gcc -shared -fPIC -Iinclude minimal_plugin.c -o plugins/minimal.so

#include "include/plugins.h"
#include "include/shell.h"

static int cmd_hi(forge_shell_t *sh, forge_cmd_t *cmd) {
    (void)sh;
    printf("Hi from plugin! You passed %d args.\n", cmd->argc - 1);
    return 0;
}

static plugin_command_t cmds[] = {
    { "hi", "hi [args...]    Say hi", cmd_hi },
};

int forge_plugin_init(plugin_descriptor_t *d) {
    d->abi_version    = FORGE_PLUGIN_ABI_VERSION;
    strcpy(d->name,    "minimal");
    strcpy(d->version, "1.0.0");
    d->hooks         = HOOK_SHELL_CMD;
    d->commands      = cmds;
    d->command_count = 1;
    return 0;
}
```

---

## 🧪 Tests

```bash
make test
```

```
╔═══════════════════════════════════════╗
║  ForgeOS Memory Allocator Test Suite  ║
╚═══════════════════════════════════════╝

  [TEST] Basic malloc / free
    ✓ (p) != NULL
    ✓ (p2) != NULL
    ✓ (p3) == NULL
  [TEST] calloc zeroes memory        ✓
  [TEST] Write and read back         ✓
  [TEST] Multiple simultaneous allocs ✓
  [TEST] Realloc grow / shrink       ✓
  [TEST] Large allocation (1 MB)     ✓
  [TEST] Allocation statistics       ✓
  [TEST] Heap integrity check        ✓
  [TEST] Stress: 1000 random ops     ✓

  Tests run:    9
  Passed: 84
  All tests passed! ✓
```

---

## 🎓 Learning Goals

This project was built to demonstrate:

| Concept | Where |
|---------|-------|
| Memory allocator design | `src/memory/memory.c` |
| Fork/exec process model | `src/shell/shell.c` → `shell_execute_pipeline()` |
| POSIX pipe plumbing | `src/shell/shell.c` → multi-stage pipelines |
| Raw terminal I/O | `src/shell/shell_builtins.c` → `shell_readline()` |
| TCP socket server | `src/server/server.c` → `server_init()` |
| POSIX thread pools | `src/server/server.c` → `thread_pool_t` |
| `/proc` filesystem | `src/process/process.c` → `procmon_read_proc()` |
| Dynamic library loading | `src/plugins/plugins.c` → `plugin_load()` |
| ANSI terminal rendering | `src/editor/editor.c` / `src/process/process.c` |
| Modular C architecture | All headers in `include/` |

---

## 📽 Recording a Demo GIF

```bash
# Install asciinema
pip install asciinema

# Record
asciinema rec forgeos-demo.cast

# Inside the recording:
make run
help
echo "Hello" | tr a-z A-Z
ls -la | head
edit src/main.c
# Ctrl-F to search, Ctrl-Q to quit editor
server 8080 www &
mem
exit

# Convert to GIF (requires agg or svg-term)
asciinema-agg forgeos-demo.cast forgeos-demo.gif
```

Place `forgeos-demo.gif` in the repo root and add `![Demo](forgeos-demo.gif)` above the Features section.

---

## 🤝 Contributing

Contributions are welcome! Areas that would benefit most:

- [ ] `&&` / `||` conditional execution
- [ ] Shell variable assignment (`FOO=bar`)
- [ ] More editor features (undo, copy/paste)
- [ ] HTTPS support in the server (via mbedTLS)
- [ ] Plugin: syntax-checked JSON viewer
- [ ] Port to macOS (adjust `/proc` reader)
- [ ] Additional test suites (shell parser, server HTTP parsing)

Please open an issue before submitting large changes.

---

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

---

<div align="center">

**Built with 🔥 in C11 — No dependencies. No compromises.**

*If this project helped you learn systems programming, please ⭐ the repo.*

</div>
