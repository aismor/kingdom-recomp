// Built-in, version-specific gameplay cheats. Enabled names are stored outside the RomFS mods folder.
#pragma once

#include "ui_text.h"

#define CHEATS_MAX 32
#define CHEATS_NAME_LEN 96

typedef struct {
    char name[CHEATS_NAME_LEN];
    int enabled;
    int patch_set;
    UiTextId label, desc;
} CheatEntry;

// Lists only cheats whose patch set exists in the current game-version build.
int cheats_scan(CheatEntry *out, int max);

// Switches cheats immediately and writes cheats/enabled-v<version>.txt.
void cheats_apply(const CheatEntry *entries, int n);

// After rt_patches_init: applies the cheats saved for this game version.
void cheats_init(void);
