/*
 * ForgeOS - Main Entry Point
 * src/main.c
 */

#include "../include/forgeos.h"
#include "../include/memory.h"
#include "../include/shell.h"
#include "../include/editor.h"
#include "../include/server.h"
#include "../include/process.h"
#include "../include/plugins.h"

/* ── Global Runtime ──────────────────────────────────────────────────────── */
forge_runtime_t g_forge = {0};

/* ── Banner ──────────────────────────────────────────────────────────────── */
void forge_print_banner(void) {
    printf(C_BCYN
    "  ███████╗ ██████╗ ██████╗  ██████╗ ███████╗ ██████╗ ███████╗\n"
    "  ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝██╔═══██╗██╔════╝\n"
    "  █████╗  ██║   ██║██████╔╝██║  ███╗█████╗  ██║   ██║███████╗\n"
    "  ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  ██║   ██║╚════██║\n"
    "  ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗╚██████╔╝███████║\n"
    "  ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚══════╝\n"
    C_RESET);
    printf(C_DIM "  A Terminal Operating Environment Built in C\n");
    printf("  Version " C_BYEL "%s" C_RESET C_DIM " (%s)  |  MIT License\n",
           FORGEOS_VERSION_STR, FORGEOS_CODENAME);
    printf("  Type " C_BWHT "help" C_RESET C_DIM " for available commands.\n"
           C_RESET "\n");
}

/* ── Version String ──────────────────────────────────────────────────────── */
const char *forge_version(void) {
    static char v[64];
    snprintf(v, sizeof(v), "%s v%s (%s)",
             FORGEOS_NAME, FORGEOS_VERSION_STR, FORGEOS_CODENAME);
    return v;
}

/* ── Initialise global runtime ───────────────────────────────────────────── */
void forge_init(void) {
    g_forge.running = true;
    g_forge.debug_mode = false;
    g_forge.last_exit_code = 0;
    g_forge.raw_mode_active = false;

    if (getcwd(g_forge.cwd, sizeof(g_forge.cwd)) == NULL)
        strncpy(g_forge.cwd, "/", sizeof(g_forge.cwd));

    if (gethostname(g_forge.hostname, sizeof(g_forge.hostname)) != 0)
        strncpy(g_forge.hostname, "forgeos", sizeof(g_forge.hostname));

    struct passwd *pw = getpwuid(getuid());
    strncpy(g_forge.username,
            pw ? pw->pw_name : "user",
            sizeof(g_forge.username) - 1);

    /* Save original terminal settings */
    tcgetattr(STDIN_FILENO, &g_forge.orig_termios);

    forge_mem_init();
}

/* ── Shutdown ────────────────────────────────────────────────────────────── */
void forge_shutdown(void) {
    if (g_forge.raw_mode_active)
        forge_disable_raw_mode();

    forge_mem_print_stats();
    forge_mem_destroy();
    printf(C_DIM "\n[forge] Session ended. Goodbye.\n" C_RESET);
}

/* ── Terminal Raw Mode ───────────────────────────────────────────────────── */
void forge_enable_raw_mode(void) {
    if (g_forge.raw_mode_active) return;
    struct termios raw = g_forge.orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_forge.raw_mode_active = true;
}

void forge_disable_raw_mode(void) {
    if (!g_forge.raw_mode_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_forge.orig_termios);
    g_forge.raw_mode_active = false;
}

/* ── Terminal Size ───────────────────────────────────────────────────────── */
int forge_get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        *rows = 24; *cols = 80;
        return FORGE_ERR;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return FORGE_OK;
}

/* ── String Utilities ────────────────────────────────────────────────────── */
char *forge_trim(char *str) {
    if (!str) return str;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

char *forge_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = forge_malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

bool forge_str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t slen = strlen(str), sflen = strlen(suffix);
    if (sflen > slen) return false;
    return strcmp(str + slen - sflen, suffix) == 0;
}

void forge_sleep_ms(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ── Signal Handlers ─────────────────────────────────────────────────────── */
static void handle_sigint(int sig) {
    UNUSED(sig);
    /* Shell readline handles Ctrl-C itself; this is a safety net */
    write(STDOUT_FILENO, "\n", 1);
}

static void handle_sigchld(int sig) {
    UNUSED(sig);
    /* Reap zombies */
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* ── Usage / Help ────────────────────────────────────────────────────────── */
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -s, --shell       Start shell (default)\n");
    printf("  -e FILE           Open file in editor\n");
    printf("  -w [PORT] [ROOT]  Start HTTP server (default port 8080)\n");
    printf("  -p                Start process monitor\n");
    printf("  -m                Show memory allocator demo\n");
    printf("  -v, --version     Print version\n");
    printf("  -h, --help        Show this help\n\n");
    printf("Examples:\n");
    printf("  %s                # Interactive shell\n", prog);
    printf("  %s -e notes.txt   # Edit a file\n", prog);
    printf("  %s -w 9090 ./www  # Start HTTP server on port 9090\n", prog);
    printf("  %s -p             # Launch process monitor\n", prog);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    signal(SIGINT,  handle_sigint);
    signal(SIGCHLD, handle_sigchld);
    signal(SIGPIPE, SIG_IGN);

    forge_init();
    forge_print_banner();

    /* ── Parse CLI arguments ─────────────────────────────────────────── */
    if (argc >= 2) {
        /* Version */
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("%s\n", forge_version());
            forge_shutdown();
            return 0;
        }

        /* Help */
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            forge_shutdown();
            return 0;
        }

        /* Editor */
        if (strcmp(argv[1], "-e") == 0) {
            /* Lazy-import to avoid circular deps at top level */
            const char *file = (argc >= 3) ? argv[2] : NULL;
            int rc = editor_main(file);
            forge_shutdown();
            return rc;
        }

        /* HTTP Server */
        if (strcmp(argv[1], "-w") == 0) {
            int port = (argc >= 3) ? atoi(argv[2]) : SERVER_DEFAULT_PORT;
            const char *root = (argc >= 4) ? argv[3] : SERVER_DEFAULT_ROOT;
            int rc = server_main(port, root);
            forge_shutdown();
            return rc;
        }

        /* Process Monitor */
        if (strcmp(argv[1], "-p") == 0) {
            int rc = procmon_main();
            forge_shutdown();
            return rc;
        }

        /* Memory demo */
        if (strcmp(argv[1], "-m") == 0) {
            printf(C_BCYN "=== ForgeOS Memory Allocator Demo ===\n" C_RESET);
            void *a = forge_malloc(128);
            void *b = forge_calloc(4, 64);
            void *c = forge_malloc(1024);
            forge_mem_print_stats();
            forge_free(a);
            forge_free(b);
            forge_free(c);
            printf("\nAfter freeing:\n");
            forge_mem_print_stats();
            forge_shutdown();
            return 0;
        }

        if (strcmp(argv[1], "-s") != 0 && strcmp(argv[1], "--shell") != 0) {
            forge_err("Unknown option: %s", argv[1]);
            print_usage(argv[0]);
            forge_shutdown();
            return 1;
        }
    }

    /* ── Default: Interactive Shell ──────────────────────────────────── */
    forge_shell_t shell;
    shell_init(&shell);

    /* Load plugins from default dir */
    plugin_registry_t plugins;
    plugin_registry_init(&plugins, "./plugins");
    plugin_load_dir(&plugins);
    plugin_run_startup_hooks(&plugins);

    int rc = shell_run(&shell);

    plugin_run_shutdown_hooks(&plugins);
    plugin_registry_destroy(&plugins);
    shell_destroy(&shell);
    forge_shutdown();
    return rc;
}
