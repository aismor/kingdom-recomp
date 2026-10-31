// User mod folders, enabled.txt, and built-in patch mods; RomFS staging is in romfs.c.
#include "mods.h"
#include "host.h"
#include "os.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

static char g_root[1024];

static const struct {
    const char *name;
    const char *patch_set;
    UiTextId label, desc;
} k_builtins[] = {
    {"Recomp-Infinite-Stamina", "infstamina", UI_MENU_MOD_STAMINA, UI_MENU_MOD_STAMINA_DESC},
};

static int root_ready(void) {
    if (g_root[0]) return 1;
    char exe[1024];
    if (!plat_exe_path(exe, sizeof exe)) return 0;
    char *cut = strrchr(exe, '/');
    char *cut2 = strrchr(exe, '\\');
    if (cut2 > cut) cut = cut2;
    if (!cut) return 0;
    *cut = 0;
    os_host_path(exe, g_root, sizeof g_root);
    size_t n = strlen(g_root);
    if (n + 6 >= sizeof g_root) return 0;
    snprintf(g_root + n, sizeof g_root - n, "/mods");
    plat_file_mkdir_p(g_root);
    return 1;
}

int mods_folder(char *out, size_t cap) {
    if (!root_ready() || snprintf(out, cap, "%s", g_root) >= (int)cap) return 0;
    return (int)strlen(out);
}

static int is_dir(const char *path) {
    char host[4096];
    os_host_path(path, host, sizeof host);
#ifdef _WIN32
    wchar_t w[4096];
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (MultiByteToWideChar(CP_UTF8, 0, host, -1, w, 4096) <= 0) return 0;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &d)) return 0;
    return (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(host, &st)) return 0;
    return S_ISDIR(st.st_mode);
#endif
}

static int reserved_name(const char *name) {
    return !strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".active") || !strcmp(name, "enabled.txt");
}

static int has_romfs(const char *mod_dir) {
    char p[1200];
    if (snprintf(p, sizeof p, "%s/romfs", mod_dir) >= (int)sizeof p) return 0;
    return is_dir(p);
}

static void read_enabled(const char *path, ModEntry *out, int n) {
    uint64_t sz = 0;
    uint8_t *data = plat_file_read(path, &sz);
    if (!data) return;
    char *p = (char *)data;
    for (size_t i = 0; i <= sz; i++) {
        if (i == sz || data[i] == '\n' || data[i] == '\r') {
            if (i < sz) data[i] = 0;
            char *line = p;
            while (*line == ' ' || *line == '\t') line++;
            if (*line && *line != '#') {
                for (int k = 0; k < n; k++)
                    if (!strcmp(out[k].name, line)) {
                        out[k].enabled = 1;
                        break;
                    }
            }
            if (i < sz) p = (char *)data + i + 1;
        }
    }
    free(data);
}

typedef struct {
    ModEntry *out;
    int *n;
    int max;
} ScanCtx;

static int list_cb(const char *name, void *ctx) {
    ScanCtx *sc = ctx;
    if (reserved_name(name)) return 1;
    if (*sc->n >= sc->max) return 0;
    char dir[1200];
    if (snprintf(dir, sizeof dir, "%s/%s", g_root, name) >= (int)sizeof dir) return 1;
    if (!is_dir(dir) || !has_romfs(dir)) return 1;
    ModEntry *e = &sc->out[*sc->n];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->enabled = 0;
    e->kind = MOD_KIND_ROMFS;
    e->patch_set = -1;
    e->label = (UiTextId)0;
    e->desc = UI_MENU_MODS_ROW_DESC;
    (*sc->n)++;
    return 1;
}

static void sort_entries(ModEntry *out, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(out[i].name, out[j].name) > 0) {
                ModEntry t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
}

int mods_scan(ModEntry *out, int max) {
    if (max <= 0) return 0;
    int n = 0;
    for (size_t b = 0; b < sizeof k_builtins / sizeof *k_builtins && n < max; b++) {
        ModEntry *e = &out[n++];
        snprintf(e->name, sizeof e->name, "%s", k_builtins[b].name);
        e->enabled = 0;
        e->kind = MOD_KIND_BUILTIN;
        e->patch_set = rt_patch_set(k_builtins[b].patch_set);
        e->label = k_builtins[b].label;
        e->desc = k_builtins[b].desc;
    }
    if (!root_ready()) return n;
    int n_builtin = n;
    ScanCtx sc = {out, &n, max};
    plat_file_list(g_root, "", list_cb, &sc);
    sort_entries(out + n_builtin, n - n_builtin);
    char enpath[1100];
    snprintf(enpath, sizeof enpath, "%s/enabled.txt", g_root);
    read_enabled(enpath, out, n);
    return n;
}

void mods_apply(const ModEntry *entries, int n) {
    if (!root_ready()) return;
    for (int i = 0; i < n; i++)
        if (entries[i].kind == MOD_KIND_BUILTIN && entries[i].patch_set >= 0)
            rt_patch_switch(entries[i].patch_set, entries[i].enabled);
    char enpath[1100];
    snprintf(enpath, sizeof enpath, "%s/enabled.txt", g_root);
    char body[8192];
    size_t pos = 0;
    pos += (size_t)snprintf(body + pos, sizeof body - pos, "# Enabled mods (one name per line)\n");
    for (int i = 0; i < n && pos + MODS_NAME_LEN + 2 < sizeof body; i++)
        if (entries[i].enabled)
            pos += (size_t)snprintf(body + pos, sizeof body - pos, "%s\n", entries[i].name);
    plat_file_write_atomic(enpath, body, pos);
    romfs_stage_user_mods();
}

void mods_init(void) {
    ModEntry entries[MODS_MAX];
    int n = mods_scan(entries, MODS_MAX);
    for (int i = 0; i < n; i++)
        if (entries[i].kind == MOD_KIND_BUILTIN && entries[i].patch_set >= 0)
            rt_patch_switch(entries[i].patch_set, entries[i].enabled);
}
