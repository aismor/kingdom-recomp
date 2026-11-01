// User RomFS folders under <install>/mods/<name>/romfs/. Enabled names live in mods/enabled.txt; enabled files are
// staged into mods/.active/ before overlay_init (runtime/romfs.c). Built-in cheats are managed separately by cheats.h.
#pragma once

#include <stddef.h>

#include "ui_text.h"

#define MODS_MAX 64
#define MODS_NAME_LEN 96
#define MODS_WALK_MAX_DEPTH 64

typedef struct {
    char name[MODS_NAME_LEN];
    int enabled;
    UiTextId desc;
} ModEntry;

int mods_folder(char *out, size_t cap);

// Lists RomFS folders and merges enabled.txt into ModEntry.enabled.
int mods_scan(ModEntry *out, int max);

// Writes enabled.txt and rebuilds mods/.active when RomFS is already loaded.
void mods_apply(const ModEntry *entries, int n);

void romfs_stage_user_mods(void);
