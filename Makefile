# ============================================================================
# ForgeOS — Makefile
# ============================================================================
# Targets:
#   make          → build forgeos binary
#   make run      → build and run interactive shell
#   make test     → build and run test suite
#   make plugin   → build example plugin shared library
#   make clean    → remove build artifacts
#   make install  → install to PREFIX (default /usr/local)
#   make docs     → generate HTML docs with doxygen (optional)
#   make help     → show this help
# ============================================================================

# ── Metadata ─────────────────────────────────────────────────────────────────
PROJECT   := forgeos
VERSION   := 1.0.0
PREFIX    ?= /usr/local

# ── Compiler & Flags ─────────────────────────────────────────────────────────
CC        := gcc
CFLAGS    := -std=c11 \
             -Wall -Wextra -Wpedantic \
             -Wformat=2 -Wno-unused-parameter \
             -Wshadow -Wwrite-strings \
             -Wno-format-truncation \
             -Wstrict-prototypes -Wold-style-definition \
             -O2 -g \
             -D_GNU_SOURCE \
             -DFORGEOS_VERSION=\"$(VERSION)\"

LDFLAGS   := -lpthread -ldl

# Debug build
DEBUG_FLAGS := -O0 -g3 -fsanitize=address,undefined -DDEBUG

# ── Directories ───────────────────────────────────────────────────────────────
SRCDIR    := src
INCDIR    := include
BUILDDIR  := build
BINDIR    := bin
TESTDIR   := tests
PLUGINDIR := plugins

# ── Source Files ──────────────────────────────────────────────────────────────
SRCS := $(SRCDIR)/main.c                     \
        $(SRCDIR)/shell/shell.c              \
        $(SRCDIR)/shell/shell_builtins.c     \
        $(SRCDIR)/editor/editor.c            \
        $(SRCDIR)/server/server.c            \
        $(SRCDIR)/process/process.c          \
        $(SRCDIR)/memory/memory.c            \
        $(SRCDIR)/plugins/plugins.c

OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

# ── Test Sources ──────────────────────────────────────────────────────────────
TEST_SRCS := $(TESTDIR)/test_memory.c         \
             $(SRCDIR)/memory/memory.c

TEST_OBJS := $(BUILDDIR)/tests/test_memory.o  \
             $(BUILDDIR)/memory/memory.o

# ── Targets ───────────────────────────────────────────────────────────────────

.PHONY: all run clean test plugin install uninstall docs help debug

all: banner $(BINDIR)/$(PROJECT)

banner:
	@echo ""
	@echo "  \033[1;36m███████╗ ██████╗ ██████╗  ██████╗ ███████╗\033[0m"
	@echo "  \033[1;36m██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝\033[0m"
	@echo "  \033[1;36m█████╗  ██║   ██║██████╔╝██║  ███╗█████╗  \033[0m"
	@echo "  \033[1;36m██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  \033[0m"
	@echo "  \033[1;36m██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗\033[0m"
	@echo "  \033[1;36m╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝\033[0m"
	@echo "  \033[2m  Building ForgeOS v$(VERSION)...\033[0m"
	@echo ""

# ── Main Binary ───────────────────────────────────────────────────────────────
$(BINDIR)/$(PROJECT): $(OBJS) | $(BINDIR)
	@echo "  \033[1;32m[LINK]\033[0m  $@"
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  \033[1;32m✓ Build complete:\033[0m bin/$(PROJECT)"
	@echo "  Run with: \033[1;33mmake run\033[0m"
	@echo ""

# ── Object Files ──────────────────────────────────────────────────────────────
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  \033[1;34m[CC]\033[0m    $<"
	@$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# ── Directories ───────────────────────────────────────────────────────────────
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)/shell $(BUILDDIR)/editor $(BUILDDIR)/server \
	          $(BUILDDIR)/process $(BUILDDIR)/memory $(BUILDDIR)/plugins \
	          $(BUILDDIR)/tests

$(BINDIR):
	@mkdir -p $(BINDIR)

# ── Run ───────────────────────────────────────────────────────────────────────
run: all
	@echo "  \033[1;35m[RUN]\033[0m   Launching ForgeOS shell...\033[0m"
	@echo ""
	@$(BINDIR)/$(PROJECT)

# ── Debug Build ───────────────────────────────────────────────────────────────
debug: CFLAGS += $(DEBUG_FLAGS)
debug: all
	@echo "  \033[1;33m[DEBUG]\033[0m Build complete (ASan + UBSan enabled)"

# ── Test Suite ────────────────────────────────────────────────────────────────
$(BINDIR)/test_memory: $(TEST_OBJS) | $(BINDIR)
	@echo "  \033[1;32m[LINK]\033[0m  $@"
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/tests/test_memory.o: $(TESTDIR)/test_memory.c | $(BUILDDIR)
	@mkdir -p $(BUILDDIR)/tests
	@echo "  \033[1;34m[CC]\033[0m    $<"
	@$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(BUILDDIR)/memory/memory.o: $(SRCDIR)/memory/memory.c | $(BUILDDIR)
	@mkdir -p $(BUILDDIR)/memory
	@echo "  \033[1;34m[CC]\033[0m    $<"
	@$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

test: $(BINDIR)/test_memory
	@echo ""
	@echo "  \033[1;35m[TEST]\033[0m  Running memory allocator tests..."
	@echo ""
	@$(BINDIR)/test_memory
	@echo ""
	@echo "  \033[1;32m✓ Tests complete\033[0m"

# ── Plugin ────────────────────────────────────────────────────────────────────
plugin:
	@echo "  \033[1;34m[CC]\033[0m    Building hello_plugin.so"
	@$(CC) -shared -fPIC -O2 -g -I$(INCDIR) \
	       $(PLUGINDIR)/hello_plugin/hello_plugin.c \
	       -o $(PLUGINDIR)/hello_plugin.so
	@echo "  \033[1;32m✓ Plugin:\033[0m plugins/hello_plugin.so"

# ── Install ───────────────────────────────────────────────────────────────────
install: all
	@echo "  [INSTALL] $(PREFIX)/bin/$(PROJECT)"
	@install -d $(PREFIX)/bin
	@install -m 755 $(BINDIR)/$(PROJECT) $(PREFIX)/bin/$(PROJECT)
	@echo "  \033[1;32m✓ Installed to $(PREFIX)/bin/$(PROJECT)\033[0m"

uninstall:
	@rm -f $(PREFIX)/bin/$(PROJECT)
	@echo "  [UNINSTALL] Removed $(PREFIX)/bin/$(PROJECT)"

# ── Docs ─────────────────────────────────────────────────────────────────────
docs:
	@if command -v doxygen >/dev/null 2>&1; then \
	    doxygen docs/Doxyfile; \
	    echo "  \033[1;32m✓ Docs generated in docs/html/\033[0m"; \
	else \
	    echo "  \033[1;33m[WARN]\033[0m doxygen not found. Install with: apt install doxygen"; \
	fi

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	@echo "  [CLEAN]  Removing build artifacts..."
	@rm -rf $(BUILDDIR) $(BINDIR)
	@echo "  \033[1;32m✓ Clean complete\033[0m"

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  \033[1;36mForgeOS v$(VERSION) — Build System\033[0m"
	@echo ""
	@echo "  Targets:"
	@echo "    \033[1;33mmake\033[0m           Build the forgeos binary"
	@echo "    \033[1;33mmake run\033[0m        Build and launch the interactive shell"
	@echo "    \033[1;33mmake test\033[0m       Build and run the test suite"
	@echo "    \033[1;33mmake plugin\033[0m     Build the example hello_plugin.so"
	@echo "    \033[1;33mmake debug\033[0m      Build with AddressSanitizer + UBSan"
	@echo "    \033[1;33mmake clean\033[0m      Remove all build artifacts"
	@echo "    \033[1;33mmake install\033[0m    Install to PREFIX (default /usr/local)"
	@echo "    \033[1;33mmake docs\033[0m       Generate Doxygen HTML documentation"
	@echo ""
	@echo "  Options:"
	@echo "    \033[1;33mCC=clang\033[0m        Use Clang instead of GCC"
	@echo "    \033[1;33mPREFIX=/usr\033[0m     Change install prefix"
	@echo ""
