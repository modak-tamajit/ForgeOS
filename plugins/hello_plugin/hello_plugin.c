/*
 * ForgeOS Example Plugin - hello_plugin
 * plugins/hello_plugin/hello_plugin.c
 *
 * Demonstrates the plugin API by registering two commands:
 *   hello  - prints a greeting
 *   cowsay - ASCII art cow saying something
 *
 * Build:
 *   gcc -shared -fPIC -o plugins/hello_plugin.so \
 *       plugins/hello_plugin/hello_plugin.c \
 *       -I include
 */

#include "../../include/plugins.h"
#include "../../include/shell.h"

/* ── Command: hello ──────────────────────────────────────────────────────── */
static int cmd_hello(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    const char *name = cmd->argc > 1 ? cmd->argv[1] : "World";
    printf(C_BCYN "╔══════════════════════════╗\n" C_RESET);
    printf(C_BCYN "║" C_RESET C_BWHT "  Hello, %-16s" C_RESET C_BCYN "║\n" C_RESET, name);
    printf(C_BCYN "╚══════════════════════════╝\n" C_RESET);
    printf(C_DIM "  (from hello_plugin v1.0)\n" C_RESET);
    return 0;
}

/* ── Command: cowsay ─────────────────────────────────────────────────────── */
static int cmd_cowsay(forge_shell_t *sh, forge_cmd_t *cmd) {
    UNUSED(sh);
    /* Assemble message from args */
    char msg[256] = "";
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
        strncat(msg, cmd->argv[i], sizeof(msg) - strlen(msg) - 1);
    }
    if (!msg[0]) strncpy(msg, "Moo!", sizeof(msg) - 1);

    int len = (int)strlen(msg);
    /* Top border */
    printf(" ");
    for (int i = 0; i < len + 2; i++) putchar('-');
    printf("\n< %s >\n ", msg);
    for (int i = 0; i < len + 2; i++) putchar('-');
    printf("\n"
           "        \\   ^__^\n"
           "         \\  (oo)\\_______\n"
           "            (__)\\       )\\/\\\n"
           "                ||----w |\n"
           "                ||     ||\n\n");
    return 0;
}

/* ── Command Table ───────────────────────────────────────────────────────── */
static plugin_command_t s_commands[] = {
    { "hello",  "hello [NAME]          Greet someone",         cmd_hello  },
    { "cowsay", "cowsay [MSG...]       Print an ASCII cow",    cmd_cowsay },
};

/* ── Plugin Init (exported symbol) ──────────────────────────────────────── */
int forge_plugin_init(plugin_descriptor_t *desc) {
    desc->abi_version    = FORGE_PLUGIN_ABI_VERSION;
    strncpy(desc->name,        "hello_plugin",              sizeof(desc->name) - 1);
    strncpy(desc->version,     "1.0.0",                     sizeof(desc->version) - 1);
    strncpy(desc->author,      "ForgeOS Contributors",      sizeof(desc->author) - 1);
    strncpy(desc->description, "Example plugin: hello + cowsay", sizeof(desc->description) - 1);

    desc->hooks         = HOOK_SHELL_CMD | HOOK_STARTUP;
    desc->commands      = s_commands;
    desc->command_count = sizeof(s_commands) / sizeof(s_commands[0]);
    desc->on_startup    = NULL;
    desc->on_shutdown   = NULL;
    return FORGE_OK;
}
