// Built-in gameplay cheats and their version-specific saved state. RomFS mods are managed by mods.c.
#include "cheats.h"
#include "os.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const int rt_game_version;
int rt_patch_set(const char *name);
int rt_patch_enabled(int set);
void rt_patch_switch(int set, int on);

static char g_root[1024];

static const struct {
    const char *name;
    const char *patch_set;
    UiTextId label, desc;
} k_cheats[] = {
    {"Recomp-Infinite-Stamina", "infstamina", UI_MENU_MOD_STAMINA, UI_MENU_MOD_STAMINA_DESC},
    {"Recomp-Infinite-Horse-Stamina", "infhorse", UI_MENU_MOD_HORSE_STAMINA, UI_MENU_MOD_HORSE_STAMINA_DESC},
    {"Recomp-Infinite-Health", "infhealth", UI_MENU_MOD_HEALTH, UI_MENU_MOD_HEALTH_DESC},
    {"Recomp-Super-Jump", "superjump", UI_MENU_MOD_SUPER_JUMP, UI_MENU_MOD_SUPER_JUMP_DESC},
    {"Recomp-Free-Shop", "freeshop", UI_MENU_MOD_FREE_SHOP, UI_MENU_MOD_FREE_SHOP_DESC},
    {"Recomp-Infinite-Arrows", "infarrows", UI_MENU_MOD_ARROWS, UI_MENU_MOD_ARROWS_DESC},
    {"Recomp-Infinite-Durability", "infdur", UI_MENU_MOD_DURABILITY, UI_MENU_MOD_DURABILITY_DESC},
    {"Recomp-Infinite-Zonai", "infzonai", UI_MENU_MOD_ZONAI, UI_MENU_MOD_ZONAI_DESC},
    {"Recomp-Infinite-Food", "infood", UI_MENU_MOD_FOOD, UI_MENU_MOD_FOOD_DESC},
    {"Recomp-Pickup-8x", "pick8", UI_MENU_MOD_PICKUP_8X, UI_MENU_MOD_PICKUP_8X_DESC},
    {"Recomp-Items-999", "items999", UI_MENU_CHEAT_ITEMS_999, UI_MENU_CHEAT_ITEMS_999_DESC},
    {"Recomp-Infinite-Food-Buff", "infoodbuff", UI_MENU_MOD_FOOD_BUFF, UI_MENU_MOD_FOOD_BUFF_DESC},
    {"Recomp-Infinite-Battery", "infbat", UI_MENU_MOD_BATTERY, UI_MENU_MOD_BATTERY_DESC},
};

static int root_ready(void) {
    if (g_root[0]) return 1;
    char exe[1024];
    if (!plat_exe_path(exe, sizeof exe)) return 0;
    char *cut = strrchr(exe, '/'), *cut2 = strrchr(exe, '\\');
    if (cut2 > cut) cut = cut2;
    if (!cut) return 0;
    *cut = 0;
    os_host_path(exe, g_root, sizeof g_root);
    size_t n = strlen(g_root);
    if (n + 9 >= sizeof g_root) return 0;
#ifdef _WIN32
    snprintf(g_root + n, sizeof g_root - n, "\\cheats");
#else
    snprintf(g_root + n, sizeof g_root - n, "/cheats");
#endif
    plat_file_mkdir_p(g_root);
    return 1;
}

static void enabled_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/enabled-v%d.txt", g_root, rt_game_version);
}

static int read_enabled_file(const char *path, CheatEntry *out, int n) {
    uint64_t sz = 0;
    uint8_t *data = plat_file_read(path, &sz);
    if (!data) return 0;
    char *p = (char *)data;
    for (size_t i = 0; i <= sz; i++) {
        if (i == sz || data[i] == '\n' || data[i] == '\r') {
            if (i < sz) data[i] = 0;
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != '#')
                for (int k = 0; k < n; k++)
                    if (!strcmp(out[k].name, p)) { out[k].enabled = 1; break; }
            if (i < sz) p = (char *)data + i + 1;
        }
    }
    free(data);
    return 1;
}

int cheats_scan(CheatEntry *out, int max) {
    int n = 0;
    for (size_t i = 0; i < sizeof k_cheats / sizeof *k_cheats && n < max; i++) {
        int set = rt_patch_set(k_cheats[i].patch_set);
        if (set < 0) continue;
        CheatEntry *e = &out[n++];
        snprintf(e->name, sizeof e->name, "%s", k_cheats[i].name);
        e->enabled = rt_patch_enabled(set);
        e->patch_set = set;
        e->label = k_cheats[i].label;
        e->desc = k_cheats[i].desc;
    }
    if (!root_ready()) return n;
    char path[1100];
    enabled_path(path, sizeof path);
    if (!read_enabled_file(path, out, n)) {
        // Releases before the Cheats tab kept built-ins beside RomFS names. Read that file once as migration input;
        // the first change creates the versioned file and subsequent starts no longer consult the legacy list.
        snprintf(path, sizeof path, "%s/../mods/enabled.txt", g_root);
        read_enabled_file(path, out, n);
    }
    return n;
}

void cheats_apply(const CheatEntry *entries, int n) {
    if (!root_ready()) return;
    for (int i = 0; i < n; i++) rt_patch_switch(entries[i].patch_set, entries[i].enabled);
    char path[1100], body[4096];
    enabled_path(path, sizeof path);
    size_t pos = (size_t)snprintf(body, sizeof body, "# Enabled cheats for TotK %d (one name per line)\n", rt_game_version);
    for (int i = 0; i < n && pos + CHEATS_NAME_LEN + 2 < sizeof body; i++)
        if (entries[i].enabled) pos += (size_t)snprintf(body + pos, sizeof body - pos, "%s\n", entries[i].name);
    plat_file_write_atomic(path, body, pos);
}

void cheats_init(void) {
    CheatEntry entries[CHEATS_MAX];
    int n = cheats_scan(entries, CHEATS_MAX);
    for (int i = 0; i < n; i++) rt_patch_switch(entries[i].patch_set, entries[i].enabled);
}
