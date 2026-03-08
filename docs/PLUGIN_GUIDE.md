# ForgeOS Plugin Development Guide

## Overview

ForgeOS supports runtime-loadable plugins as shared libraries (`.so` files).
A plugin can register new shell commands, run startup/shutdown hooks, and
inspect or modify the shell environment.

## Minimum Plugin Template

```c
/* my_plugin.c
 * Build: gcc -shared -fPIC -Iinclude my_plugin.c -o plugins/my_plugin.so
 */
#include "include/plugins.h"
#include "include/shell.h"

static int cmd_greet(forge_shell_t *sh, forge_cmd_t *cmd) {
    (void)sh;
    printf("Hello from my_plugin! Args: %d\n", cmd->argc);
    return 0;
}

static plugin_command_t s_cmds[] = {
    { "greet", "greet       Say hello from my_plugin", cmd_greet },
};

int forge_plugin_init(plugin_descriptor_t *d) {
    d->abi_version    = FORGE_PLUGIN_ABI_VERSION;
    strcpy(d->name,    "my_plugin");
    strcpy(d->version, "1.0.0");
    strcpy(d->author,  "Your Name");
    d->hooks         = HOOK_SHELL_CMD;
    d->commands      = s_cmds;
    d->command_count = 1;
    return 0;  /* FORGE_OK */
}
```

## Plugin Descriptor Fields

| Field | Type | Description |
|-------|------|-------------|
| `abi_version` | `int` | Must be `FORGE_PLUGIN_ABI_VERSION` (1) |
| `name` | `char[64]` | Plugin identifier |
| `version` | `char[32]` | Semver string |
| `author` | `char[64]` | Author name |
| `description` | `char[256]` | One-line description |
| `hooks` | `uint32_t` | OR of `plugin_hook_t` flags |
| `commands` | `plugin_command_t *` | Array of commands (can be NULL) |
| `command_count` | `int` | Length of commands array |
| `on_startup` | `plugin_hook_fn` | Called at ForgeOS startup (can be NULL) |
| `on_shutdown` | `plugin_hook_fn` | Called at ForgeOS shutdown (can be NULL) |

## Hook Types

```c
HOOK_SHELL_CMD    // Plugin provides shell commands
HOOK_STARTUP      // Plugin wants an on_startup() callback
HOOK_SHUTDOWN     // Plugin wants an on_shutdown() callback
HOOK_PROMPT       // Plugin can modify the shell prompt (future)
HOOK_EDITOR_CMD   // Plugin provides editor commands (future)
```

## Loading

Plugins are automatically loaded from the `plugins/` directory at startup.
Any `.so` file in that directory is opened with `dlopen()`.

You can also load manually by placing the `.so` in `plugins/` and restarting,
or by building against the plugin API and using `plugin_load()` programmatically.

## Example: Startup Hook

```c
static int my_startup(void *ctx) {
    (void)ctx;
    printf("[my_plugin] Loaded and ready!\n");
    return 0;
}

int forge_plugin_init(plugin_descriptor_t *d) {
    d->abi_version = FORGE_PLUGIN_ABI_VERSION;
    strcpy(d->name, "my_plugin");
    d->hooks       = HOOK_STARTUP | HOOK_SHUTDOWN;
    d->on_startup  = my_startup;
    d->on_shutdown = NULL;
    return 0;
}
```
