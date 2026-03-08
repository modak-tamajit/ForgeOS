/*
 * ForgeOS - Plugin System
 * src/plugins/plugins.c
 *
 * Loads .so shared libraries at runtime, validates ABI version,
 * registers commands and lifecycle hooks.
 */

#include "../../include/plugins.h"
#include "../../include/memory.h"

/* ── Init / Destroy ──────────────────────────────────────────────────────── */
int plugin_registry_init(plugin_registry_t *reg, const char *plugin_dir) {
    memset(reg, 0, sizeof(*reg));
    strncpy(reg->plugin_dir,
            plugin_dir ? plugin_dir : "./plugins",
            sizeof(reg->plugin_dir) - 1);
    forge_log("Plugin dir: %s", reg->plugin_dir);
    return FORGE_OK;
}

void plugin_registry_destroy(plugin_registry_t *reg) {
    for (int i = 0; i < reg->count; i++) {
        plugin_handle_t *h = &reg->plugins[i];
        if (h->active && h->dl_handle) {
            if (h->desc.on_shutdown) h->desc.on_shutdown(NULL);
            dlclose(h->dl_handle);
            h->active = false;
        }
    }
    reg->count = 0;
}

/* ── Load a Single Plugin ────────────────────────────────────────────────── */
int plugin_load(plugin_registry_t *reg, const char *path) {
    if (reg->count >= FORGE_MAX_PLUGINS) {
        forge_err("%s", "Plugin registry full");
        return FORGE_ERR;
    }

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        forge_err("dlopen(%s): %s", path, dlerror());
        return FORGE_ERR;
    }

    /* ISO C forbids casting between function and object pointers;
     * POSIX guarantees dlsym works for function pointers on conforming systems. */
    void *sym = dlsym(handle, FORGE_PLUGIN_INIT_SYMBOL);
    forge_plugin_init_fn init_fn;
    memcpy(&init_fn, &sym, sizeof(init_fn));
    if (!init_fn) {
        forge_err("Plugin %s missing symbol '%s': %s", path, FORGE_PLUGIN_INIT_SYMBOL, dlerror());
        dlclose(handle);
        return FORGE_ERR;
    }

    plugin_handle_t *h = &reg->plugins[reg->count];
    memset(h, 0, sizeof(*h));

    if (init_fn(&h->desc) != FORGE_OK) {
        forge_err("Plugin %s init_fn failed", path);
        dlclose(handle);
        return FORGE_ERR;
    }

    if (h->desc.abi_version != FORGE_PLUGIN_ABI_VERSION) {
        forge_err("Plugin %s ABI mismatch: expected %d, got %d",
                  path, FORGE_PLUGIN_ABI_VERSION, h->desc.abi_version);
        dlclose(handle);
        return FORGE_ERR;
    }

    h->dl_handle = handle;
    h->active    = true;
    strncpy(h->path, path, sizeof(h->path) - 1);
    reg->count++;

    forge_ok("Loaded plugin: %s v%s by %s (%d commands)",
             h->desc.name, h->desc.version, h->desc.author, h->desc.command_count);
    return FORGE_OK;
}

/* ── Unload by Name ──────────────────────────────────────────────────────── */
int plugin_unload(plugin_registry_t *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->plugins[i].desc.name, name) == 0) {
            plugin_handle_t *h = &reg->plugins[i];
            if (h->desc.on_shutdown) h->desc.on_shutdown(NULL);
            dlclose(h->dl_handle);
            /* Compact array */
            memmove(&reg->plugins[i], &reg->plugins[i+1],
                    sizeof(plugin_handle_t) * (reg->count - i - 1));
            reg->count--;
            forge_ok("Unloaded plugin: %s", name);
            return FORGE_OK;
        }
    }
    forge_err("Plugin not found: %s", name);
    return FORGE_ERR_NOTFOUND;
}

/* ── Scan Plugin Directory ───────────────────────────────────────────────── */
int plugin_load_dir(plugin_registry_t *reg) {
    DIR *dir = opendir(reg->plugin_dir);
    if (!dir) {
        /* Not an error if plugin dir doesn't exist */
        return FORGE_OK;
    }

    int loaded = 0;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (!forge_str_ends_with(de->d_name, ".so")) continue;
        char path[FORGE_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", reg->plugin_dir, de->d_name);
        if (plugin_load(reg, path) == FORGE_OK) loaded++;
    }
    closedir(dir);

    if (loaded > 0)
        forge_log("Loaded %d plugin(s) from %s", loaded, reg->plugin_dir);
    return FORGE_OK;
}

/* ── Look Up Command ─────────────────────────────────────────────────────── */
plugin_command_t *plugin_find_command(plugin_registry_t *reg, const char *name) {
    for (int i = 0; i < reg->count; i++) {
        plugin_handle_t *h = &reg->plugins[i];
        if (!h->active) continue;
        for (int j = 0; j < h->desc.command_count; j++) {
            if (strcmp(h->desc.commands[j].name, name) == 0)
                return &h->desc.commands[j];
        }
    }
    return NULL;
}

/* ── Lifecycle Hooks ─────────────────────────────────────────────────────── */
void plugin_run_startup_hooks(plugin_registry_t *reg) {
    for (int i = 0; i < reg->count; i++) {
        plugin_handle_t *h = &reg->plugins[i];
        if (h->active && (h->desc.hooks & HOOK_STARTUP) && h->desc.on_startup)
            h->desc.on_startup(NULL);
    }
}

void plugin_run_shutdown_hooks(plugin_registry_t *reg) {
    for (int i = 0; i < reg->count; i++) {
        plugin_handle_t *h = &reg->plugins[i];
        if (h->active && (h->desc.hooks & HOOK_SHUTDOWN) && h->desc.on_shutdown)
            h->desc.on_shutdown(NULL);
    }
}

/* ── List Plugins ────────────────────────────────────────────────────────── */
void plugin_list(plugin_registry_t *reg) {
    if (reg->count == 0) {
        printf(C_DIM "  No plugins loaded.\n" C_RESET);
        return;
    }
    printf(C_BCYN "\n  Loaded Plugins (%d)\n\n" C_RESET, reg->count);
    for (int i = 0; i < reg->count; i++) {
        plugin_handle_t *h = &reg->plugins[i];
        printf("  " C_BWHT "%-20s" C_RESET " v%-8s  %s\n",
               h->desc.name, h->desc.version, h->desc.description);
        for (int j = 0; j < h->desc.command_count; j++)
            printf("    " C_BCYN "%-16s" C_RESET " %s\n",
                   h->desc.commands[j].name, h->desc.commands[j].help);
    }
    printf("\n");
}
