/*
 * ForgeOS - Dynamic Plugin System
 * include/plugins.h
 */

#ifndef FORGE_PLUGINS_H
#define FORGE_PLUGINS_H

#include "forgeos.h"

/* ── Plugin ABI Version ──────────────────────────────────────────────────── */
#define FORGE_PLUGIN_ABI_VERSION  1

/* ── Hook Types ──────────────────────────────────────────────────────────── */
typedef enum {
    HOOK_SHELL_CMD    = 1 << 0,
    HOOK_STARTUP      = 1 << 1,
    HOOK_SHUTDOWN     = 1 << 2,
    HOOK_PROMPT       = 1 << 3,
    HOOK_EDITOR_CMD   = 1 << 4,
} plugin_hook_t;

/* ── Forward declarations ────────────────────────────────────────────────── */
/* Use tagged struct names so forward-decls work alongside full definitions */
#ifndef FORGE_SHELL_TYPES_DEFINED
typedef struct forge_shell_s forge_shell_t;
typedef struct forge_cmd_s   forge_cmd_t;
#endif
typedef struct plugin_handle plugin_handle_t;

/* ── Handlers ────────────────────────────────────────────────────────────── */
typedef int (*plugin_cmd_fn)(forge_shell_t *sh, forge_cmd_t *cmd);
typedef int (*plugin_hook_fn)(void *ctx);

/* ── Command Registration ────────────────────────────────────────────────── */
typedef struct {
    char          name[64];
    char          help[256];
    plugin_cmd_fn handler;
} plugin_command_t;

/* ── Plugin Descriptor ───────────────────────────────────────────────────── */
typedef struct {
    int               abi_version;
    char              name   [64];
    char              version[32];
    char              author [64];
    char              description[256];
    uint32_t          hooks;
    plugin_command_t *commands;
    int               command_count;
    plugin_hook_fn    on_startup;
    plugin_hook_fn    on_shutdown;
    void             *priv;
} plugin_descriptor_t;

/* ── Init symbol every plugin must export ────────────────────────────────── */
typedef int (*forge_plugin_init_fn)(plugin_descriptor_t *desc);
#define FORGE_PLUGIN_INIT_SYMBOL  "forge_plugin_init"

/* ── Loaded Plugin Handle ────────────────────────────────────────────────── */
struct plugin_handle {
    void               *dl_handle;
    plugin_descriptor_t desc;
    char                path[FORGE_MAX_PATH];
    bool                active;
};

/* ── Plugin Registry ─────────────────────────────────────────────────────── */
typedef struct {
    plugin_handle_t plugins[FORGE_MAX_PLUGINS];
    int             count;
    char            plugin_dir[FORGE_MAX_PATH];
} plugin_registry_t;

/* ── Public API ──────────────────────────────────────────────────────────── */
int   plugin_registry_init(plugin_registry_t *reg, const char *plugin_dir);
void  plugin_registry_destroy(plugin_registry_t *reg);
int   plugin_load(plugin_registry_t *reg, const char *path);
int   plugin_unload(plugin_registry_t *reg, const char *name);
int   plugin_load_dir(plugin_registry_t *reg);
plugin_command_t *plugin_find_command(plugin_registry_t *reg, const char *name);
void  plugin_run_startup_hooks(plugin_registry_t *reg);
void  plugin_run_shutdown_hooks(plugin_registry_t *reg);
void  plugin_list(plugin_registry_t *reg);

#endif /* FORGE_PLUGINS_H */
