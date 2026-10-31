// User mods: built-in code patches and RomFS folders under <install>/mods/<name>/romfs/. Enabled names live in
// mods/enabled.txt; RomFS mods are staged into mods/.active/ before overlay_init (runtime/romfs.c).
#pragma once

#include <stddef.h>

#include "ui_text.h"

#define MODS_MAX 64
#define MODS_NAME_LEN 96
#define MODS_WALK_MAX_DEPTH 64

typedef enum { MOD_KIND_ROMFS, MOD_KIND_BUILTIN } ModKind;

typedef struct {
    char name[MODS_NAME_LEN];
    int enabled;
    ModKind kind;
    int patch_set;   // rt_patch_set index, -1 for RomFS
    UiTextId label;  // built-in row title; RomFS rows use name
    UiTextId desc;
} ModEntry;

int mods_folder(char *out, size_t cap);

// Built-in mods first, then RomFS folders; merges enabled.txt into ModEntry.enabled.
int mods_scan(ModEntry *out, int max);

// Writes enabled.txt, switches built-in patch sets, rebuilds mods/.active when RomFS is already loaded.
void mods_apply(const ModEntry *entries, int n);

// After rt_patches_init: apply enabled built-in mods from enabled.txt.
void mods_init(void);

void romfs_stage_user_mods(void);
