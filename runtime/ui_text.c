// Catalog of the text the host shows the player (ui_text.h). One array per catalog language, indexed by UiTextId: a new
// string gets an id in ui_text.h and an entry in every language that has one (a missing entry falls back to English).
// The software keyboard has every language; the menu has English, Russian and Portuguese (Brazil).
#include <stdatomic.h>
#include <string.h>

#include "cfg.h"
#include "ov.h"
#include "platform/platform.h"
#include "ui_text.h"

typedef struct { const char *lang; const char *const *text; } Row;

// The language names are the same in every row.
#define LANG_NAMES \
    [UI_MENU_LANGNAME_EN] = "English", [UI_MENU_LANGNAME_JA] = "日本語", [UI_MENU_LANGNAME_FR] = "Français", \
    [UI_MENU_LANGNAME_DE] = "Deutsch", [UI_MENU_LANGNAME_IT] = "Italiano",     [UI_MENU_LANGNAME_ES] = "Español", \
    [UI_MENU_LANGNAME_NL] = "Nederlands", [UI_MENU_LANGNAME_RU] = "Русский", \
    [UI_MENU_LANGNAME_ZH_HANS] = "简体中文", [UI_MENU_LANGNAME_ZH_HANT] = "繁體中文", [UI_MENU_LANGNAME_KO] = "한국어", \
    [UI_MENU_LANGNAME_PT] = "Português (Brasil)"

// SWKBD: TITLE, PROMPT, DEFAULT_NAME (at most 9 UTF-16 units, no '@' '%' '\'), OK, CANCEL, PAD_HINT.
#define SWKBD(title, prompt, name, ok, cancel, hint) \
    [UI_SWKBD_TITLE] = title, [UI_SWKBD_PROMPT] = prompt, [UI_SWKBD_DEFAULT_NAME] = name, [UI_SWKBD_OK] = ok, \
    [UI_SWKBD_CANCEL] = cancel, [UI_SWKBD_PAD_HINT] = hint, LANG_NAMES

static const char *const text_en[UI_TEXT_COUNT] = {
    SWKBD("Text entry", "Enter a name", "Horse", "OK", "Cancel", "Controller: A = OK, B = Cancel"),

    [UI_MENU_TITLE] = "Settings",
    [UI_MENU_TAB_SCREEN] = "Screen", [UI_MENU_TAB_GAME] = "Game", [UI_MENU_TAB_MODS] = "Mods",
    [UI_MENU_TAB_SOUND] = "Sound", [UI_MENU_TAB_CONTROLS] = "Controls", [UI_MENU_TAB_MENU] = "Menu", [UI_MENU_TAB_ADVANCED] = "Advanced",
    [UI_MENU_CONTINUE] = "Continue", [UI_MENU_QUIT] = "Quit game",
    [UI_MENU_ON] = "On", [UI_MENU_OFF] = "Off", [UI_MENU_AUTO] = "Automatic", [UI_MENU_DEFAULT_MARK] = "default",
    [UI_MENU_AFTER_RESTART] = "after restart", [UI_MENU_SET_BY_ENV] = "set by environment variable",
    [UI_MENU_SAVE_FAILED] = "could not save settings",
    [UI_MENU_VERSION] = "Kingdom Recomp %s, game %s",

    [UI_MENU_FULLSCREEN] = "Fullscreen", [UI_MENU_FULLSCREEN_DESC] = "Fills the screen with the game; F11 toggles it.",
    [UI_MENU_WINDOW_SIZE] = "Window size", [UI_MENU_WINDOW_SIZE_DESC] = "Size of the game window when not fullscreen.",
    [UI_MENU_WINDOW_SIZE_CUSTOM] = "Custom",
    [UI_MENU_PRESENT] = "Presentation", [UI_MENU_PRESENT_DESC] = "How finished frames reach the screen.",
    [UI_MENU_PRESENT_MAILBOX] = "Mailbox", [UI_MENU_PRESENT_VSYNC] = "VSync", [UI_MENU_PRESENT_IMMEDIATE] = "Immediate",
    [UI_MENU_PRESENT_IN_USE] = "in use: %s",
    [UI_MENU_GPU] = "Graphics card", [UI_MENU_GPU_DESC] = "The Vulkan device that draws the game.",
    [UI_MENU_GPU_AUTO] = "Automatic",
    [UI_MENU_PERF_OVERLAY] = "Performance overlay",
    [UI_MENU_PERF_OVERLAY_DESC] = "Frame rate in the top right corner. Full adds frame times with a graph, CPU, GPU and "
                                  "memory load, temperatures and the render thread's waits. F10 cycles the modes.",
    [UI_MENU_PERF_OFF] = "Off", [UI_MENU_PERF_FPS] = "FPS", [UI_MENU_PERF_FULL] = "Full",
    // printf format of the overlay line in FPS mode: the frame rate
    [UI_PERF_LINE_FPS] = "%.0f fps",
    [UI_MENU_GRAPHICS] = "Picture",
    [UI_MENU_OUTPUT_RES] = "Game resolution",
    [UI_MENU_OUTPUT_RES_DESC] = "The size the game draws its picture and interface in. Above 1920 x 1080 the game needs "
                                "more video memory and GPU time.",
    [UI_MENU_RENDER_SCALE] = "3D render scale",
    [UI_MENU_RENDER_SCALE_DESC] = "Size of the 3D scene relative to the game resolution; the game's FSR scales it to "
                                  "that size. Above 100 % smooths jagged edges (supersampling) at a high GPU cost.",
    [UI_MENU_RENDER_SCALE_GAME] = "Game (83 %)",
    [UI_MENU_UPSCALER] = "Scaling to the window",
    [UI_MENU_UPSCALER_DESC] = "How the picture is scaled when the window is larger than the game resolution. FSR keeps "
                              "edges and detail sharper than bilinear.",
    [UI_MENU_UPSCALER_FSR] = "AMD FSR 1", [UI_MENU_UPSCALER_BILINEAR] = "Bilinear",
    [UI_MENU_SHARPNESS] = "FSR sharpness",
    [UI_MENU_SHARPNESS_DESC] = "Sharpening after FSR scaling; 0 turns it off.",

    [UI_MENU_FPS_LIMIT] = "Frame rate limit",
    [UI_MENU_FPS_LIMIT_DESC] = "The most frames per second the game draws. Above 60 is experimental and needs Game speed "
                               "follows the clock.",
    [UI_MENU_FPS_30] = "30 FPS", [UI_MENU_FPS_60] = "60 FPS", [UI_MENU_FPS_120] = "120 FPS", [UI_MENU_FPS_144] = "144 FPS",
    // the monitor's refresh rate; printf format with the rate in Hz when it is known
    [UI_MENU_FPS_DISPLAY] = "Monitor rate", [UI_MENU_FPS_DISPLAY_HZ] = "Monitor rate (%.0f Hz)",
    [UI_MENU_REALTIME] = "Game speed follows the clock",
    [UI_MENU_REALTIME_DESC] = "Steps the game by real time, so a slow frame does not slow the game down.",
    [UI_MENU_CLOTH_REALTIME] = "Cloth on the clock",
    [UI_MENU_CLOTH_REALTIME_DESC] = "Steps cloth and hair by real time as well.",
    [UI_MENU_HIGHFPS_FIX] = "High frame rate physics fix",
    [UI_MENU_HIGHFPS_FIX_DESC] = "Corrects physics that misbehave above 30 frames per second.",
    [UI_MENU_GAME_LANG] = "Game language", [UI_MENU_GAME_LANG_DESC] = "Language of the game's text and voices.",
    [UI_MENU_GAME_LANG_AUTO] = "Automatic",
    [UI_MENU_OPERATION_MODE] = "Console mode",
    [UI_MENU_OPERATION_MODE_DESC] = "Whether the game thinks it runs docked or handheld.",
    [UI_MENU_MODS_INTRO] =
        "Built-in mods apply immediately. Drop RomFS mod folders into the mods folder (each with a romfs/ tree, as in "
        "Eden); enable them here and restart to apply.",
    [UI_MENU_MODS_EMPTY] = "No RomFS mods found. Built-in mods still appear when listed above.",
    [UI_MENU_MODS_ROW_DESC] = "RomFS files from this folder replace game data on the next start.",
    [UI_MENU_MODS_OPEN] = "Open mods folder",
    [UI_MENU_MODS_OPEN_DESC] = "Opens the folder where mod directories go.",
    [UI_MENU_MOD_STAMINA] = "Infinite stamina (sprint / climb)",
    [UI_MENU_MOD_STAMINA_DESC] = "Stamina no longer drains while running or climbing. Applies immediately.",
    [UI_MENU_MOD_HORSE_STAMINA] = "Infinite horse stamina",
    [UI_MENU_MOD_HORSE_STAMINA_DESC] = "Your horse can gallop without tiring. Applies immediately.",
    [UI_MENU_MOD_HEALTH] = "Infinite health",
    [UI_MENU_MOD_HEALTH_DESC] = "Link takes no damage from hits. Applies immediately.",
    [UI_MENU_MOD_SUPER_JUMP] = "Super jump",
    [UI_MENU_MOD_SUPER_JUMP_DESC] = "Higher jump height. Applies immediately.",
    [UI_MENU_MOD_FREE_SHOP] = "Free shopping (Rupees)",
    [UI_MENU_MOD_FREE_SHOP_DESC] = "Shop purchases do not spend Rupees. Applies immediately.",
    [UI_MENU_MOD_ARROWS] = "Infinite arrows",
    [UI_MENU_MOD_ARROWS_DESC] = "Arrows are not consumed when shot. Applies immediately.",
    [UI_MENU_MOD_DURABILITY] = "Infinite weapon durability",
    [UI_MENU_MOD_DURABILITY_DESC] = "Weapons, shields and bows do not break. Applies immediately.",
    [UI_MENU_MOD_ZONAI] = "Infinite Zonai devices",
    [UI_MENU_MOD_ZONAI_DESC] = "Zonai items from the bag are not consumed. Applies immediately.",
    [UI_MENU_MOD_FOOD] = "Infinite food items",
    [UI_MENU_MOD_FOOD_DESC] = "Food items in the inventory are not consumed. Applies immediately.",
    [UI_MENU_MOD_PICKUP_8X] = "Item pickup x8",
    [UI_MENU_MOD_PICKUP_8X_DESC] = "Pick up eight times as many items at once. Applies immediately.",
    [UI_MENU_MOD_FOOD_BUFF] = "Infinite food buff timer",
    [UI_MENU_MOD_FOOD_BUFF_DESC] = "Timed food bonuses stay active. Applies immediately.",
    [UI_MENU_MOD_BATTERY] = "Infinite battery",
    [UI_MENU_MOD_BATTERY_DESC] = "Battery energy does not drain. Applies immediately.",
    [UI_MENU_MODE_DOCKED] = "Docked", [UI_MENU_MODE_HANDHELD] = "Handheld",

    [UI_MENU_VOLUME] = "Volume", [UI_MENU_VOLUME_DESC] = "Master volume of the game.",

    [UI_MENU_MOTION] = "Motion controls", [UI_MENU_MOTION_DESC] = "Aiming with the controller's gyroscope.",
    [UI_MENU_RUMBLE_STRENGTH] = "Vibration strength",
    [UI_MENU_RUMBLE_STRENGTH_DESC] = "How strong the controller vibrates.",
    [UI_MENU_RUMBLE_CURVE] = "Vibration curve",
    [UI_MENU_RUMBLE_CURVE_DESC] = "Lower values make weak vibration stronger.",
    [UI_MENU_RUMBLE_TRIGGERS] = "Trigger vibration (Xbox)",
    [UI_MENU_RUMBLE_TRIGGERS_DESC] = "Vibrates the triggers of an Xbox controller too.",
    [UI_MENU_PAD_LAYOUT] = "Button layout",
    [UI_MENU_PAD_LAYOUT_DESC] = "Which button is A. Nintendo: the right one, as on a Switch. Xbox: the bottom one, as "
                                "labelled on an Xbox controller (cross on a PlayStation controller). Automatic picks "
                                "Nintendo for controllers with Nintendo labels and Xbox for the others.",
    [UI_MENU_PAD_LAYOUT_AUTO] = "Automatic", [UI_MENU_PAD_LAYOUT_NINTENDO] = "Nintendo (A right)",
    [UI_MENU_PAD_LAYOUT_XBOX] = "Xbox (A bottom)", [UI_MENU_PAD_LAYOUT_IN_USE] = "in use: %s",
    [UI_MENU_MOUSE_SENS] = "Mouse camera sensitivity",
    [UI_MENU_MOUSE_SENS_DESC] = "How fast the mouse turns the camera.",

    [UI_BINDS_HELP] = "Select a cell and press A or Enter, then the key, mouse button or controller button. Delete or "
                      "X clears a cell. A key in use moves here; a controller button swaps with the one it replaces.",
    [UI_BINDS_COL_KEY1] = "Key 1", [UI_BINDS_COL_KEY2] = "Key 2", [UI_BINDS_COL_PAD] = "Controller",
    [UI_BINDS_GROUP_BUTTONS] = "Buttons", [UI_BINDS_GROUP_MOVE] = "Movement", [UI_BINDS_GROUP_CAMERA] = "Camera",
    [UI_BINDS_PRESS_KEY] = "Press a key or mouse button for %s",
    [UI_BINDS_PRESS_PAD] = "Press a controller button for %s",
    [UI_BINDS_CANCEL_HINT] = "Esc cancels, or wait %d s",
    [UI_BINDS_TAKEN] = "%s was taken from %s", [UI_BINDS_SWAPPED] = "%s swapped with %s",
    [UI_BINDS_RESET] = "Reset all buttons",
    [UI_BINDS_RESET_DESC] = "Back to the default keys and controller buttons. The button layout above applies again: remapped controller buttons stay where you put them until this reset.",
    [UI_BIND_A] = "A", [UI_BIND_B] = "B", [UI_BIND_X] = "X", [UI_BIND_Y] = "Y",
    [UI_BIND_LCLICK] = "Left stick click", [UI_BIND_RCLICK] = "Right stick click", [UI_BIND_L] = "L",
    [UI_BIND_R] = "R", [UI_BIND_ZL] = "ZL", [UI_BIND_ZR] = "ZR", [UI_BIND_PLUS] = "Plus", [UI_BIND_MINUS] = "Minus",
    [UI_BIND_LEFT] = "D-pad left", [UI_BIND_UP] = "D-pad up", [UI_BIND_RIGHT] = "D-pad right",
    [UI_BIND_DOWN] = "D-pad down",
    [UI_BIND_LSTICK_LEFT] = "Move left", [UI_BIND_LSTICK_UP] = "Move forward", [UI_BIND_LSTICK_RIGHT] = "Move right",
    [UI_BIND_LSTICK_DOWN] = "Move back",
    [UI_BIND_RSTICK_LEFT] = "Camera left", [UI_BIND_RSTICK_UP] = "Camera up", [UI_BIND_RSTICK_RIGHT] = "Camera right",
    [UI_BIND_RSTICK_DOWN] = "Camera down",
    [UI_BIND_DESC_A] = "Interact, pick up, talk.", [UI_BIND_DESC_B] = "Run, cancel, go back.",
    [UI_BIND_DESC_X] = "Jump.", [UI_BIND_DESC_Y] = "Attack.", [UI_BIND_DESC_LCLICK] = "Crouch.",
    [UI_BIND_DESC_RCLICK] = "First-person view.", [UI_BIND_DESC_L] = "Abilities.",
    [UI_BIND_DESC_R] = "Throw a weapon.", [UI_BIND_DESC_ZL] = "Shield, lock on.", [UI_BIND_DESC_ZR] = "Bow.",
    [UI_BIND_DESC_PLUS] = "Menu, inventory.", [UI_BIND_DESC_MINUS] = "Map.",
    [UI_BIND_DESC_DPAD] = "Quick-select.",
    [UI_BIND_DESC_MOVE] = "Moves Link like the left stick. The controller's left stick always works too.",
    [UI_BIND_DESC_CAMERA] = "Turns the camera like the right stick. The mouse and the controller's right stick always "
                            "work too.",
    [UI_KEY_SPACE] = "Space", [UI_KEY_TAB] = "Tab", [UI_KEY_LSHIFT] = "Left Shift", [UI_KEY_LCTRL] = "Left Ctrl",
    [UI_KEY_UP] = "Up arrow", [UI_KEY_DOWN] = "Down arrow", [UI_KEY_LEFT] = "Left arrow",
    [UI_KEY_RIGHT] = "Right arrow",
    [UI_KEY_MOUSE_L] = "Left mouse", [UI_KEY_MOUSE_R] = "Right mouse", [UI_KEY_MOUSE_M] = "Middle mouse",
    [UI_KEY_MOUSE_X1] = "Mouse 4", [UI_KEY_MOUSE_X2] = "Mouse 5",
    [UI_KEY_ENTER] = "Enter", [UI_KEY_BACKSPACE] = "Backspace", [UI_KEY_RSHIFT] = "Right Shift",
    [UI_KEY_RCTRL] = "Right Ctrl", [UI_KEY_LALT] = "Alt", [UI_KEY_CAPSLOCK] = "Caps Lock", [UI_KEY_INSERT] = "Insert",
    [UI_KEY_DELETE] = "Delete", [UI_KEY_HOME] = "Home", [UI_KEY_END] = "End", [UI_KEY_PAGEUP] = "Page Up",
    [UI_KEY_PAGEDOWN] = "Page Down", [UI_KEY_NUM_FMT] = "Num %s",
    [UI_PAD_SOUTH] = "Bottom button", [UI_PAD_EAST] = "Right button", [UI_PAD_WEST] = "Left button",
    [UI_PAD_NORTH] = "Top button", [UI_PAD_LSTICK] = "Left stick", [UI_PAD_RSTICK] = "Right stick",
    [UI_PAD_L] = "Left bumper", [UI_PAD_R] = "Right bumper", [UI_PAD_START] = "Start", [UI_PAD_BACK] = "Back",
    [UI_PAD_DPAD_LEFT] = "D-pad left", [UI_PAD_DPAD_UP] = "D-pad up", [UI_PAD_DPAD_RIGHT] = "D-pad right",
    [UI_PAD_DPAD_DOWN] = "D-pad down", [UI_PAD_ZL] = "Left trigger", [UI_PAD_ZR] = "Right trigger",
    [UI_PAD_CROSS] = "Cross", [UI_PAD_CIRCLE] = "Circle", [UI_PAD_SQUARE] = "Square", [UI_PAD_TRIANGLE] = "Triangle",

    [UI_MENU_MENU_LANG] = "Menu language", [UI_MENU_MENU_LANG_DESC] = "Language of this menu and the overlay.",
    [UI_MENU_MENU_LANG_AUTO] = "Automatic (from Windows)",

    [UI_MENU_ADV_WARNING] = "These settings are for testing and troubleshooting. Change them only when you know why.",
    [UI_MENU_GROUP_SCHED] = "Scheduling", [UI_MENU_GROUP_SHADERS] = "Shaders and pipelines",
    [UI_MENU_GROUP_TEXMEM] = "Textures and memory", [UI_MENU_GROUP_FIXES] = "Render fixes",
    [UI_MENU_GROUP_VULKAN] = "Vulkan", [UI_MENU_GROUP_DIAG] = "Diagnostics",
    [UI_MENU_ASYNC_PREEMPT] = "Virtual core preemption",
    [UI_MENU_ASYNC_PREEMPT_DESC] = "Lets a game thread interrupt another one on the same virtual core.",
    [UI_MENU_PREEMPT_OFF] = "Off", [UI_MENU_PREEMPT_ON] = "On", [UI_MENU_PREEMPT_INHERIT] = "Inheritance only",
    [UI_MENU_REPLAY_ASYNC] = "GPU frame prep on its own thread",
    [UI_MENU_REPLAY_ASYNC_DESC] = "Prepares frames for the GPU on a separate thread instead of the game thread. Adds up to one frame of latency.",
    [UI_MENU_FREE_CORES] = "Virtual core gate",
    [UI_MENU_FREE_CORES_DESC] = "Lets only one thread run per virtual core, as on the console.",
    [UI_MENU_TIMER_1MS] = "1 ms Windows timer",
    [UI_MENU_TIMER_1MS_DESC] = "Raises the system timer resolution to 1 ms for steadier frame pacing.",
    [UI_MENU_PIPE_WAIT] = "Waiting draw handling",
    [UI_MENU_PIPE_WAIT_DESC] = "Default keeps the current scheduling with a reserved worker for waiting draws; Old (draw) uses the previous scheduling.",
    [UI_MENU_PIPE_WAIT_DEFAULT] = "Default", [UI_MENU_PIPE_WAIT_DRAW] = "Old (draw)",
    [UI_MENU_PIPE_SKIP] = "Draws skipped while a pipeline builds",
    [UI_MENU_PIPE_SKIP_DESC] = "Which draws are dropped instead of waiting for their pipeline.",
    [UI_MENU_PIPE_SKIP_STRICT] = "Strict", [UI_MENU_PIPE_SKIP_ANY] = "Any",
    [UI_MENU_PIPE_SKIP_FULLSCREEN] = "Fullscreen passes",
    [UI_MENU_LIB_PREBUILD] = "Stage library prebuild",
    [UI_MENU_LIB_PREBUILD_DESC] = "Builds shader stage libraries ahead of time in the background.",
    [UI_MENU_LIB_PREBUILD_THREADS] = "Stage library prebuild threads",
    [UI_MENU_LIB_PREBUILD_THREADS_DESC] = "Threads used for the prebuild; Automatic picks a number from your CPU.",
    [UI_MENU_PIPE_PREBUILD] = "Pipeline prebuild",
    [UI_MENU_PIPE_PREBUILD_DESC] = "Builds known pipelines ahead of time.",
    [UI_MENU_PREWARM_XLATE] = "Translator prewarm",
    [UI_MENU_PREWARM_XLATE_DESC] = "Threads that translate known shaders at start; Automatic takes half the CPU threads, at most 16.",
    [UI_MENU_GPL] = "Graphics pipeline library",
    [UI_MENU_GPL_DESC] = "Uses the Vulkan pipeline library extension; turn off if your driver misbehaves with it.",
    [UI_MENU_ASTC] = "ASTC texture decoding", [UI_MENU_ASTC_DESC] = "Where compressed ASTC textures are decoded.",
    [UI_MENU_ASTC_AUTO] = "Automatic", [UI_MENU_ASTC_GPU] = "GPU shader", [UI_MENU_ASTC_NATIVE] = "Native",
    [UI_MENU_ASTC_CPU] = "CPU",
    [UI_MENU_MEMORY] = "GPU memory mode", [UI_MENU_MEMORY_DESC] = "How game memory is shared with the GPU.",
    [UI_MENU_MEMORY_AUTO] = "Automatic", [UI_MENU_MEMORY_IMPORT] = "Import", [UI_MENU_MEMORY_MIRROR] = "Mirror",
    [UI_MENU_MEMORY_VERIFY] = "Verify",
    [UI_MENU_TEX_ALIAS] = "Texture aliasing", [UI_MENU_TEX_ALIAS_DESC] = "A new texture created over a live texture's memory starts with that content instead of empty.",
    [UI_MENU_LOD_GUARD] = "LOD guard", [UI_MENU_LOD_GUARD_DESC] = "Prevents objects flashing in the wrong place or a flat far field for a frame, by waiting for the game to finish rewriting level-of-detail data before the GPU reads it.",
    [UI_MENU_FIX_C0] = "Constant buffer fix",
    [UI_MENU_FIX_C0_DESC] = "Fills the driver-reserved constant bank c0 (storage sizes, dispatch size, point size, base instance) instead of reading zeros.",
    [UI_MENU_FIX_ZERO] = "Zero new GPU memory",
    [UI_MENU_FIX_ZERO_DESC] = "New GPU memory pools start zero-filled, as Switch memory does.",
    [UI_MENU_BIND_SKIP] = "Skip repeated binds",
    [UI_MENU_BIND_SKIP_DESC] = "Optimisation: skips bindings that did not change.",
    [UI_MENU_SHADER_MEMO] = "Shader memo", [UI_MENU_SHADER_MEMO_DESC] = "Optimisation: remembers shader lookups.",
    [UI_MENU_PIPE_MEMO] = "Pipeline memo", [UI_MENU_PIPE_MEMO_DESC] = "Optimisation: reuses the previous draw's pipeline when its state repeats.",
    [UI_MENU_TEX_MEMO] = "Texture binding memo", [UI_MENU_TEX_MEMO_DESC] = "Optimisation: remembers texture descriptors between draws.",
    [UI_MENU_DESC_TEMPLATE] = "Descriptor templates",
    [UI_MENU_DESC_TEMPLATE_DESC] = "Optimisation: updates descriptors in bulk.",
    [UI_MENU_PUSH_SKIP] = "Skip repeated push constants",
    [UI_MENU_PUSH_SKIP_DESC] = "Optimisation: skips push constants that did not change.",
    [UI_MENU_SPLIT_DRAWS] = "Early GPU submission",
    [UI_MENU_SPLIT_DRAWS_DESC] = "Submits GPU work in parts every N draws between passes so the GPU starts earlier; 0 turns it off.",
    [UI_MENU_VK_VALIDATION] = "Validation layer",
    [UI_MENU_VK_VALIDATION_DESC] = "Checks every Vulkan call and slows the game down a lot.",
    [UI_MENU_VK_VALIDATION_MISSING] = "The validation layer is not installed.",
    [UI_MENU_VK_DYN_VERTEX] = "Dynamic vertex state",
    [UI_MENU_VK_DYN_VERTEX_DESC] = "Forces dynamic vertex input on or off; the default depends on the GPU.",
    [UI_MENU_VK_SUBGROUP_PIN] = "Pin subgroup size", [UI_MENU_VK_SUBGROUP_PIN_DESC] = "Fixes the shader subgroup size.",
    [UI_MENU_TRACE] = "Performance log", [UI_MENU_TRACE_DESC] = "Writes frame timings to a file.",
    [UI_MENU_FLIGHT] = "Flight recorder",
    [UI_MENU_FLIGHT_DESC] = "Keeps a short history and saves it when a frame freezes.",
    [UI_MENU_FLIGHT_SPIKE_MS] = "Freeze threshold (ms)",
    [UI_MENU_FLIGHT_SPIKE_MS_DESC] = "Frame time that counts as a freeze.",
    [UI_MENU_INPUT_RECORD] = "Input recording",
    [UI_MENU_INPUT_RECORD_DESC] = "Writes your inputs to a file for bug reports.",
    [UI_MENU_OPEN_LOG] = "Open log folder", [UI_MENU_OPEN_LOG_DESC] = "Shows the folder with the log files.",
    [UI_MENU_COPY_INFO] = "Copy system info",
    [UI_MENU_COPY_INFO_DESC] = "Copies GPU, driver, CPU, RAM and changed settings to the clipboard.",
    [UI_MENU_COPY_INFO_DONE] = "Copied", [UI_MENU_ACTION_FAILED] = "did not work",
    [UI_MENU_RESET] = "Reset all settings to defaults", [UI_MENU_RESET_DESC] = "Applies to every tab.",
    [UI_MENU_RESET_CONFIRM] = "Reset every setting to its default?", [UI_MENU_RESET_YES] = "Reset",
    [UI_MENU_RESET_NO] = "Cancel",

    [UI_MENU_OV_FPS] = "%.0f FPS", [UI_MENU_OV_FRAME_MS] = "%.1f ms", [UI_MENU_OV_GAME_PCT] = "game %.0f%%",
    [UI_MENU_OV_MAX_MS] = "max %.1f ms",
    [UI_MENU_OV_GPU] = "GPU", [UI_MENU_OV_CPU] = "CPU", [UI_MENU_OV_RAM] = "RAM", [UI_MENU_OV_VRAM] = "VRAM",
    [UI_MENU_OV_TEMP] = "Temperature", [UI_MENU_OV_POWER] = "Power", [UI_MENU_OV_CLOCK] = "Clock",
    [UI_MENU_OV_NONE] = "-",
    [UI_MENU_OV_REPLAY] = "GPU frame prep", [UI_MENU_OV_GPU_TIME] = "GPU busy per frame",
    [UI_MENU_OV_PIPE_WAIT] = "Pipeline waits",
    [UI_MENU_OV_SHADER_WAIT] = "Shader waits", [UI_MENU_OV_TEX_UPLOADS] = "Texture uploads",
    [UI_MENU_OV_FMT_PCT] = "%.0f%%", [UI_MENU_OV_FMT_TEMP] = "%.0f °C", [UI_MENU_OV_FMT_POWER] = "%.0f W",
    [UI_MENU_OV_FMT_MHZ] = "%.0f MHz", [UI_MENU_OV_FMT_GB] = "%.1f GB", [UI_MENU_OV_FMT_GB_OF] = "/ %.1f GB",
    [UI_MENU_OV_GAME_GB] = "game %.1f GB", [UI_MENU_OV_FMT_MS] = "%.2f ms", [UI_MENU_OV_PER_FRAME] = "%.1f per frame",
    [UI_MENU_OV_WINDOW] = "Window", [UI_MENU_OV_OUTPUT] = "Game image", [UI_MENU_OV_SCENE] = "3D scene",
    [UI_MENU_OV_SCALING] = "Scaling", [UI_MENU_OV_LIMIT] = "Frame limit", [UI_MENU_OV_PRESENT] = "Presentation",
    [UI_MENU_OV_FMT_SIZE] = "%ux%u", [UI_MENU_OV_FMT_SCENE] = "%ux%u, %.0f%%",
    [UI_MENU_OV_FMT_FSR] = "AMD FSR 1, sharpness %d%%", [UI_MENU_OV_SCALE_NONE] = "none (1:1)",
    [UI_MENU_OV_SCALE_BILINEAR] = "bilinear",
};

static const char *const text_ru[UI_TEXT_COUNT] = {
    SWKBD("Ввод текста", "Введите имя", "Лошадь", "ОК", "Отмена", "Контроллер: A — ОК, B — отмена"),

    [UI_MENU_TITLE] = "Настройки",
    [UI_MENU_TAB_SCREEN] = "Экран", [UI_MENU_TAB_GAME] = "Игра", [UI_MENU_TAB_MODS] = "Моды",
    [UI_MENU_TAB_SOUND] = "Звук", [UI_MENU_TAB_CONTROLS] = "Управление", [UI_MENU_TAB_MENU] = "Меню", [UI_MENU_TAB_ADVANCED] = "Дополнительно",
    [UI_MENU_CONTINUE] = "Продолжить", [UI_MENU_QUIT] = "Выйти из игры",
    [UI_MENU_ON] = "Вкл.", [UI_MENU_OFF] = "Выкл.", [UI_MENU_AUTO] = "Автоматически",
    [UI_MENU_DEFAULT_MARK] = "по умолчанию",
    [UI_MENU_AFTER_RESTART] = "после перезапуска", [UI_MENU_SET_BY_ENV] = "задано переменной окружения",
    [UI_MENU_SAVE_FAILED] = "не удалось сохранить настройки",
    [UI_MENU_VERSION] = "Kingdom Recomp %s, игра %s",

    [UI_MENU_FULLSCREEN] = "Полный экран", [UI_MENU_FULLSCREEN_DESC] = "Игра на весь экран; F11 переключает режим.",
    [UI_MENU_WINDOW_SIZE] = "Размер окна", [UI_MENU_WINDOW_SIZE_DESC] = "Размер окна игры вне полноэкранного режима.",
    [UI_MENU_WINDOW_SIZE_CUSTOM] = "Другой",
    [UI_MENU_PRESENT] = "Вывод кадров", [UI_MENU_PRESENT_DESC] = "Как готовые кадры попадают на экран.",
    [UI_MENU_PRESENT_MAILBOX] = "Mailbox", [UI_MENU_PRESENT_VSYNC] = "Вертикальная синхронизация",
    [UI_MENU_PRESENT_IMMEDIATE] = "Без ожидания",
    [UI_MENU_PRESENT_IN_USE] = "сейчас: %s",
    [UI_MENU_GPU] = "Видеокарта", [UI_MENU_GPU_DESC] = "Устройство Vulkan, которое рисует игру.",
    [UI_MENU_GPU_AUTO] = "Автоматически",
    [UI_MENU_PERF_OVERLAY] = "Индикатор производительности",
    [UI_MENU_PERF_OVERLAY_DESC] = "Частота кадров в правом верхнем углу. «Полный» добавляет время кадров с графиком, "
                                  "загрузку ЦП, GPU и памяти, температуры и ожидания потока отрисовки. F10 переключает "
                                  "режимы.",
    [UI_MENU_PERF_OFF] = "Выкл.", [UI_MENU_PERF_FPS] = "FPS", [UI_MENU_PERF_FULL] = "Полный",
    [UI_PERF_LINE_FPS] = "%.0f к/с",
    [UI_MENU_GRAPHICS] = "Изображение",
    [UI_MENU_OUTPUT_RES] = "Разрешение игры",
    [UI_MENU_OUTPUT_RES_DESC] = "Размер, в котором игра рисует картинку и интерфейс. Выше 1920 × 1080 игре нужно больше "
                                "видеопамяти и времени GPU.",
    [UI_MENU_RENDER_SCALE] = "Масштаб 3D-отрисовки",
    [UI_MENU_RENDER_SCALE_DESC] = "Размер 3D-сцены относительно разрешения игры; FSR игры доводит её до этого размера. "
                                  "Выше 100 % сглаживает лесенки на краях (суперсэмплинг) ценой большой нагрузки на GPU.",
    [UI_MENU_RENDER_SCALE_GAME] = "Как в игре (83 %)",
    [UI_MENU_UPSCALER] = "Масштабирование под окно",
    [UI_MENU_UPSCALER_DESC] = "Как картинка растягивается, когда окно больше разрешения игры. FSR сохраняет края и "
                              "детали чётче билинейного.",
    [UI_MENU_UPSCALER_FSR] = "AMD FSR 1", [UI_MENU_UPSCALER_BILINEAR] = "Билинейное",
    [UI_MENU_SHARPNESS] = "Резкость FSR",
    [UI_MENU_SHARPNESS_DESC] = "Повышение резкости после масштабирования FSR; 0 выключает его.",

    [UI_MENU_FPS_LIMIT] = "Ограничение частоты кадров",
    [UI_MENU_FPS_LIMIT_DESC] = "Наибольшее число кадров в секунду. Выше 60 — экспериментальный режим, нужна «Скорость "
                               "игры по часам».",
    [UI_MENU_FPS_30] = "30 FPS", [UI_MENU_FPS_60] = "60 FPS", [UI_MENU_FPS_120] = "120 FPS", [UI_MENU_FPS_144] = "144 FPS",
    [UI_MENU_FPS_DISPLAY] = "Как у монитора", [UI_MENU_FPS_DISPLAY_HZ] = "Как у монитора (%.0f Гц)",
    [UI_MENU_REALTIME] = "Скорость игры по часам",
    [UI_MENU_REALTIME_DESC] = "Игра идёт по реальному времени, медленный кадр её не замедляет.",
    [UI_MENU_CLOTH_REALTIME] = "Ткань по часам",
    [UI_MENU_CLOTH_REALTIME_DESC] = "Ткань и волосы тоже идут по реальному времени.",
    [UI_MENU_HIGHFPS_FIX] = "Исправление физики на высоком FPS",
    [UI_MENU_HIGHFPS_FIX_DESC] = "Исправляет физику, которая ломается при частоте выше 30 кадров в секунду.",
    [UI_MENU_GAME_LANG] = "Язык игры", [UI_MENU_GAME_LANG_DESC] = "Язык текста и озвучки игры.",
    [UI_MENU_GAME_LANG_AUTO] = "Автоматически",
    [UI_MENU_OPERATION_MODE] = "Режим консоли",
    [UI_MENU_OPERATION_MODE_DESC] = "Игра считает, что консоль в док-станции или в портативном режиме.",
    [UI_MENU_MODS_INTRO] =
        "Встроенные моды включаются сразу. Папки RomFS-модов кладите в mods/ (в каждой — romfs/, как в Eden); после "
        "включения перезапустите игру.",
    [UI_MENU_MODS_EMPTY] = "RomFS-модов нет. Встроенные моды всё равно отображаются выше.",
    [UI_MENU_MODS_ROW_DESC] = "Файлы RomFS из этой папки заменят данные игры при следующем запуске.",
    [UI_MENU_MODS_OPEN] = "Открыть папку mods",
    [UI_MENU_MODS_OPEN_DESC] = "Открывает папку, куда кладут каталоги модов.",
    [UI_MENU_MOD_STAMINA] = "Бесконечная выносливость (бег / лазание)",
    [UI_MENU_MOD_STAMINA_DESC] = "Выносливость не тратится при беге и лазании. Включается сразу.",
    [UI_MENU_MOD_HORSE_STAMINA] = "Бесконечная выносливость лошади",
    [UI_MENU_MOD_HORSE_STAMINA_DESC] = "Лошадь может скакать без усталости. Включается сразу.",
    [UI_MENU_MOD_HEALTH] = "Бесконечное здоровье",
    [UI_MENU_MOD_HEALTH_DESC] = "Линк не получает урона. Включается сразу.",
    [UI_MENU_MOD_SUPER_JUMP] = "Супер-прыжок",
    [UI_MENU_MOD_SUPER_JUMP_DESC] = "Выше прыжок. Включается сразу.",
    [UI_MENU_MOD_FREE_SHOP] = "Бесплатные покупки (рупии)",
    [UI_MENU_MOD_FREE_SHOP_DESC] = "Покупки не тратят рупии. Включается сразу.",
    [UI_MENU_MOD_ARROWS] = "Бесконечные стрелы",
    [UI_MENU_MOD_ARROWS_DESC] = "Стрелы не расходуются. Включается сразу.",
    [UI_MENU_MOD_DURABILITY] = "Бесконечная прочность оружия",
    [UI_MENU_MOD_DURABILITY_DESC] = "Оружие, щиты и луки не ломаются. Включается сразу.",
    [UI_MENU_MOD_ZONAI] = "Бесконечные зонаи-устройства",
    [UI_MENU_MOD_ZONAI_DESC] = "Предметы Zonai из сумки не тратятся. Включается сразу.",
    [UI_MENU_MOD_FOOD] = "Бесконечная еда",
    [UI_MENU_MOD_FOOD_DESC] = "Еда в инвентаре не расходуется. Включается сразу.",
    [UI_MENU_MOD_PICKUP_8X] = "Подбор предметов x8",
    [UI_MENU_MOD_PICKUP_8X_DESC] = "Подбираете в 8 раз больше за раз. Включается сразу.",
    [UI_MENU_MOD_FOOD_BUFF] = "Бесконечный бафф еды",
    [UI_MENU_MOD_FOOD_BUFF_DESC] = "Временные бонусы от еды не истекают. Включается сразу.",
    [UI_MENU_MOD_BATTERY] = "Бесконечная батарея",
    [UI_MENU_MOD_BATTERY_DESC] = "Заряд батареи не тратится. Включается сразу.",
    [UI_MENU_MODE_DOCKED] = "В док-станции", [UI_MENU_MODE_HANDHELD] = "Портативный",

    [UI_MENU_VOLUME] = "Громкость", [UI_MENU_VOLUME_DESC] = "Общая громкость игры.",

    [UI_MENU_MOTION] = "Управление движением", [UI_MENU_MOTION_DESC] = "Прицеливание гироскопом контроллера.",
    [UI_MENU_RUMBLE_STRENGTH] = "Сила вибрации",
    [UI_MENU_RUMBLE_STRENGTH_DESC] = "Насколько сильно вибрирует контроллер.",
    [UI_MENU_RUMBLE_CURVE] = "Кривая вибрации",
    [UI_MENU_RUMBLE_CURVE_DESC] = "Чем ниже значение, тем сильнее слабая вибрация.",
    [UI_MENU_RUMBLE_TRIGGERS] = "Вибрация триггеров (Xbox)",
    [UI_MENU_RUMBLE_TRIGGERS_DESC] = "Вибрация и в триггерах контроллера Xbox.",
    [UI_MENU_PAD_LAYOUT] = "Раскладка кнопок",
    [UI_MENU_PAD_LAYOUT_DESC] = "Какая кнопка работает как A. Nintendo: правая, как на Switch. Xbox: нижняя, как "
                                "подписано на контроллере Xbox (крест на контроллере PlayStation). Автоматически: "
                                "Nintendo для контроллеров с подписями Nintendo, Xbox для остальных.",
    [UI_MENU_PAD_LAYOUT_AUTO] = "Автоматически", [UI_MENU_PAD_LAYOUT_NINTENDO] = "Nintendo (A справа)",
    [UI_MENU_PAD_LAYOUT_XBOX] = "Xbox (A снизу)", [UI_MENU_PAD_LAYOUT_IN_USE] = "сейчас: %s",
    [UI_MENU_MOUSE_SENS] = "Чувствительность мыши",
    [UI_MENU_MOUSE_SENS_DESC] = "Как быстро мышь поворачивает камеру.",

    [UI_BINDS_HELP] = "Выберите ячейку и нажмите A или Enter, затем клавишу, кнопку мыши или кнопку контроллера. "
                      "Delete или X очищает ячейку. Занятая клавиша переходит сюда; кнопка контроллера меняется "
                      "местами с той, которую заменяет.",
    [UI_BINDS_COL_KEY1] = "Клавиша 1", [UI_BINDS_COL_KEY2] = "Клавиша 2", [UI_BINDS_COL_PAD] = "Контроллер",
    [UI_BINDS_GROUP_BUTTONS] = "Кнопки", [UI_BINDS_GROUP_MOVE] = "Движение", [UI_BINDS_GROUP_CAMERA] = "Камера",
    [UI_BINDS_PRESS_KEY] = "Нажмите клавишу или кнопку мыши для «%s»",
    [UI_BINDS_PRESS_PAD] = "Нажмите кнопку контроллера для «%s»",
    [UI_BINDS_CANCEL_HINT] = "Esc отменяет, или подождите %d с",
    [UI_BINDS_TAKEN] = "%s: убрано с «%s»", [UI_BINDS_SWAPPED] = "«%s» и «%s» поменялись местами",
    [UI_BINDS_RESET] = "Сбросить все кнопки",
    [UI_BINDS_RESET_DESC] = "Вернуть клавиши и кнопки контроллера по умолчанию. Снова действует раскладка кнопок выше: "
                            "переназначенные кнопки контроллера остаются на своих местах до "
                            "этого сброса.",
    [UI_BIND_A] = "A", [UI_BIND_B] = "B", [UI_BIND_X] = "X", [UI_BIND_Y] = "Y",
    [UI_BIND_LCLICK] = "Нажатие левого стика", [UI_BIND_RCLICK] = "Нажатие правого стика", [UI_BIND_L] = "L",
    [UI_BIND_R] = "R", [UI_BIND_ZL] = "ZL", [UI_BIND_ZR] = "ZR", [UI_BIND_PLUS] = "Плюс", [UI_BIND_MINUS] = "Минус",
    [UI_BIND_LEFT] = "Крестовина влево", [UI_BIND_UP] = "Крестовина вверх", [UI_BIND_RIGHT] = "Крестовина вправо",
    [UI_BIND_DOWN] = "Крестовина вниз",
    [UI_BIND_LSTICK_LEFT] = "Влево", [UI_BIND_LSTICK_UP] = "Вперёд", [UI_BIND_LSTICK_RIGHT] = "Вправо",
    [UI_BIND_LSTICK_DOWN] = "Назад",
    [UI_BIND_RSTICK_LEFT] = "Камера влево", [UI_BIND_RSTICK_UP] = "Камера вверх",
    [UI_BIND_RSTICK_RIGHT] = "Камера вправо", [UI_BIND_RSTICK_DOWN] = "Камера вниз",
    [UI_BIND_DESC_A] = "Взаимодействие, подобрать, поговорить.", [UI_BIND_DESC_B] = "Бег, отмена, назад.",
    [UI_BIND_DESC_X] = "Прыжок.", [UI_BIND_DESC_Y] = "Атака.", [UI_BIND_DESC_LCLICK] = "Присесть.",
    [UI_BIND_DESC_RCLICK] = "Вид от первого лица.", [UI_BIND_DESC_L] = "Способности.",
    [UI_BIND_DESC_R] = "Бросок оружия.", [UI_BIND_DESC_ZL] = "Щит, захват цели.", [UI_BIND_DESC_ZR] = "Лук.",
    [UI_BIND_DESC_PLUS] = "Меню, инвентарь.", [UI_BIND_DESC_MINUS] = "Карта.",
    [UI_BIND_DESC_DPAD] = "Быстрый выбор.",
    [UI_BIND_DESC_MOVE] = "Двигает Линка, как левый стик. Левый стик контроллера тоже всегда работает.",
    [UI_BIND_DESC_CAMERA] = "Поворачивает камеру, как правый стик. Мышь и правый стик контроллера тоже всегда "
                            "работают.",
    [UI_KEY_SPACE] = "Пробел", [UI_KEY_TAB] = "Tab", [UI_KEY_LSHIFT] = "Левый Shift", [UI_KEY_LCTRL] = "Левый Ctrl",
    [UI_KEY_UP] = "Стрелка вверх", [UI_KEY_DOWN] = "Стрелка вниз", [UI_KEY_LEFT] = "Стрелка влево",
    [UI_KEY_RIGHT] = "Стрелка вправо",
    [UI_KEY_MOUSE_L] = "ЛКМ", [UI_KEY_MOUSE_R] = "ПКМ", [UI_KEY_MOUSE_M] = "СКМ", [UI_KEY_MOUSE_X1] = "Мышь 4",
    [UI_KEY_MOUSE_X2] = "Мышь 5",
    [UI_KEY_ENTER] = "Enter", [UI_KEY_BACKSPACE] = "Backspace", [UI_KEY_RSHIFT] = "Правый Shift",
    [UI_KEY_RCTRL] = "Правый Ctrl", [UI_KEY_LALT] = "Alt", [UI_KEY_CAPSLOCK] = "Caps Lock", [UI_KEY_INSERT] = "Insert",
    [UI_KEY_DELETE] = "Delete", [UI_KEY_HOME] = "Home", [UI_KEY_END] = "End", [UI_KEY_PAGEUP] = "Page Up",
    [UI_KEY_PAGEDOWN] = "Page Down", [UI_KEY_NUM_FMT] = "Num %s",
    [UI_PAD_SOUTH] = "Нижняя кнопка", [UI_PAD_EAST] = "Правая кнопка", [UI_PAD_WEST] = "Левая кнопка",
    [UI_PAD_NORTH] = "Верхняя кнопка", [UI_PAD_LSTICK] = "Левый стик", [UI_PAD_RSTICK] = "Правый стик",
    [UI_PAD_L] = "Левый бампер", [UI_PAD_R] = "Правый бампер", [UI_PAD_START] = "Start", [UI_PAD_BACK] = "Back",
    [UI_PAD_DPAD_LEFT] = "Крестовина влево", [UI_PAD_DPAD_UP] = "Крестовина вверх",
    [UI_PAD_DPAD_RIGHT] = "Крестовина вправо", [UI_PAD_DPAD_DOWN] = "Крестовина вниз", [UI_PAD_ZL] = "Левый курок",
    [UI_PAD_ZR] = "Правый курок",
    [UI_PAD_CROSS] = "Крест", [UI_PAD_CIRCLE] = "Круг", [UI_PAD_SQUARE] = "Квадрат", [UI_PAD_TRIANGLE] = "Треугольник",

    [UI_MENU_MENU_LANG] = "Язык меню", [UI_MENU_MENU_LANG_DESC] = "Язык этого меню и индикатора.",
    [UI_MENU_MENU_LANG_AUTO] = "Автоматически (из Windows)",

    [UI_MENU_ADV_WARNING] = "Эти настройки нужны для тестов и поиска неполадок. Меняйте их, только если знаете зачем.",
    [UI_MENU_GROUP_SCHED] = "Планирование", [UI_MENU_GROUP_SHADERS] = "Шейдеры и конвейеры",
    [UI_MENU_GROUP_TEXMEM] = "Текстуры и память", [UI_MENU_GROUP_FIXES] = "Исправления графики",
    [UI_MENU_GROUP_VULKAN] = "Vulkan", [UI_MENU_GROUP_DIAG] = "Диагностика",
    [UI_MENU_ASYNC_PREEMPT] = "Вытеснение на виртуальных ядрах",
    [UI_MENU_ASYNC_PREEMPT_DESC] = "Позволяет потоку игры прервать другой поток на том же виртуальном ядре.",
    [UI_MENU_PREEMPT_OFF] = "Выкл.", [UI_MENU_PREEMPT_ON] = "Вкл.", [UI_MENU_PREEMPT_INHERIT] = "Только наследование",
    [UI_MENU_REPLAY_ASYNC] = "Подготовка кадра для GPU в отдельном потоке",
    [UI_MENU_REPLAY_ASYNC_DESC] = "Кадр для GPU готовится в отдельном потоке, а не в потоке игры. Добавляет до одного кадра задержки.",
    [UI_MENU_FREE_CORES] = "Ограничение виртуальных ядер",
    [UI_MENU_FREE_CORES_DESC] = "На каждом виртуальном ядре работает один поток, как на консоли.",
    [UI_MENU_TIMER_1MS] = "Таймер Windows 1 мс",
    [UI_MENU_TIMER_1MS_DESC] = "Повышает точность системного таймера до 1 мс для ровного темпа кадров.",
    [UI_MENU_PIPE_WAIT] = "Обработка ожидающих отрисовок",
    [UI_MENU_PIPE_WAIT_DESC] = "«По умолчанию» — текущее планирование с отдельным потоком для ожидающих отрисовок; «Старый способ» — прежнее планирование.",
    [UI_MENU_PIPE_WAIT_DEFAULT] = "По умолчанию", [UI_MENU_PIPE_WAIT_DRAW] = "Старый способ",
    [UI_MENU_PIPE_SKIP] = "Пропуск отрисовок при сборке конвейера",
    [UI_MENU_PIPE_SKIP_DESC] = "Какие отрисовки отбрасываются, а не ждут свой конвейер.",
    [UI_MENU_PIPE_SKIP_STRICT] = "Строго", [UI_MENU_PIPE_SKIP_ANY] = "Любые",
    [UI_MENU_PIPE_SKIP_FULLSCREEN] = "Полноэкранные проходы",
    [UI_MENU_LIB_PREBUILD] = "Предсборка библиотек стадий",
    [UI_MENU_LIB_PREBUILD_DESC] = "Заранее собирает библиотеки стадий шейдеров в фоне.",
    [UI_MENU_LIB_PREBUILD_THREADS] = "Потоки предсборки библиотек",
    [UI_MENU_LIB_PREBUILD_THREADS_DESC] = "Сколько потоков занято предсборкой; «Автоматически» — по числу ядер процессора.",
    [UI_MENU_PIPE_PREBUILD] = "Предсборка конвейеров",
    [UI_MENU_PIPE_PREBUILD_DESC] = "Заранее собирает известные конвейеры.",
    [UI_MENU_PREWARM_XLATE] = "Прогрев транслятора",
    [UI_MENU_PREWARM_XLATE_DESC] = "Потоки, которые при запуске переводят известные шейдеры; «Автоматически» — половина потоков процессора, не больше 16.",
    [UI_MENU_GPL] = "Библиотека графических конвейеров",
    [UI_MENU_GPL_DESC] = "Использует расширение Vulkan для библиотек конвейеров; выключите, если драйвер с ним сбоит.",
    [UI_MENU_ASTC] = "Распаковка текстур ASTC", [UI_MENU_ASTC_DESC] = "Где распаковываются сжатые текстуры ASTC.",
    [UI_MENU_ASTC_AUTO] = "Автоматически", [UI_MENU_ASTC_GPU] = "Шейдер GPU", [UI_MENU_ASTC_NATIVE] = "Аппаратно",
    [UI_MENU_ASTC_CPU] = "Процессор",
    [UI_MENU_MEMORY] = "Режим памяти GPU", [UI_MENU_MEMORY_DESC] = "Как память игры передаётся видеокарте.",
    [UI_MENU_MEMORY_AUTO] = "Автоматически", [UI_MENU_MEMORY_IMPORT] = "Импорт", [UI_MENU_MEMORY_MIRROR] = "Зеркало",
    [UI_MENU_MEMORY_VERIFY] = "Проверка",
    [UI_MENU_TEX_ALIAS] = "Наследование содержимого текстур", [UI_MENU_TEX_ALIAS_DESC] = "Новая текстура поверх памяти живой текстуры получает её содержимое, а не пустую.",
    [UI_MENU_LOD_GUARD] = "Защита LOD", [UI_MENU_LOD_GUARD_DESC] = "Не даёт объектам мелькать не на своём месте, а дальнему плану — на кадр становиться плоским: ждёт, пока игра допишет данные уровней детализации, и только потом их читает GPU.",
    [UI_MENU_FIX_C0] = "Исправление константного буфера",
    [UI_MENU_FIX_C0_DESC] = "Заполняет зарезервированный драйвером банк констант c0 (размеры хранилищ, размер запуска, размер точки, базовый экземпляр) вместо чтения нулей.",
    [UI_MENU_FIX_ZERO] = "Обнулять новую память GPU",
    [UI_MENU_FIX_ZERO_DESC] = "Новые пулы памяти GPU заполняются нулями, как память Switch.",
    [UI_MENU_BIND_SKIP] = "Пропуск повторных привязок",
    [UI_MENU_BIND_SKIP_DESC] = "Оптимизация: пропускает неизменившиеся привязки.",
    [UI_MENU_SHADER_MEMO] = "Память шейдеров", [UI_MENU_SHADER_MEMO_DESC] = "Оптимизация: запоминает поиск шейдеров.",
    [UI_MENU_PIPE_MEMO] = "Память конвейера", [UI_MENU_PIPE_MEMO_DESC] = "Оптимизация: повторяет конвейер прошлой отрисовки, если её состояние не изменилось.",
    [UI_MENU_TEX_MEMO] = "Память привязок текстур", [UI_MENU_TEX_MEMO_DESC] = "Оптимизация: запоминает дескрипторы текстур между отрисовками.",
    [UI_MENU_DESC_TEMPLATE] = "Шаблоны дескрипторов",
    [UI_MENU_DESC_TEMPLATE_DESC] = "Оптимизация: обновляет дескрипторы пачками.",
    [UI_MENU_PUSH_SKIP] = "Пропуск повторных push-констант",
    [UI_MENU_PUSH_SKIP_DESC] = "Оптимизация: пропускает неизменившиеся push-константы.",
    [UI_MENU_SPLIT_DRAWS] = "Ранняя отправка работы GPU",
    [UI_MENU_SPLIT_DRAWS_DESC] = "Отправляет работу GPU частями через каждые N отрисовок между проходами, чтобы GPU начинал раньше; 0 — выключено.",
    [UI_MENU_VK_VALIDATION] = "Слой проверки",
    [UI_MENU_VK_VALIDATION_DESC] = "Проверяет каждый вызов Vulkan и сильно замедляет игру.",
    [UI_MENU_VK_VALIDATION_MISSING] = "Слой проверки не установлен.",
    [UI_MENU_VK_DYN_VERTEX] = "Динамическое состояние вершин",
    [UI_MENU_VK_DYN_VERTEX_DESC] = "Принудительно включает или выключает динамический ввод вершин; по умолчанию зависит от GPU.",
    [UI_MENU_VK_SUBGROUP_PIN] = "Фиксировать размер подгруппы",
    [UI_MENU_VK_SUBGROUP_PIN_DESC] = "Фиксирует размер подгруппы шейдера.",
    [UI_MENU_TRACE] = "Журнал производительности", [UI_MENU_TRACE_DESC] = "Пишет время кадров в файл.",
    [UI_MENU_FLIGHT] = "Запись последних кадров (для отчёта о фризе)",
    [UI_MENU_FLIGHT_DESC] = "Хранит короткую историю и сохраняет её, когда кадр подвисает.",
    [UI_MENU_FLIGHT_SPIKE_MS] = "Порог подвисания (мс)",
    [UI_MENU_FLIGHT_SPIKE_MS_DESC] = "Время кадра, которое считается подвисанием.",
    [UI_MENU_INPUT_RECORD] = "Запись ввода",
    [UI_MENU_INPUT_RECORD_DESC] = "Пишет ваш ввод в файл для отчёта об ошибке.",
    [UI_MENU_OPEN_LOG] = "Открыть папку журналов", [UI_MENU_OPEN_LOG_DESC] = "Показывает папку с файлами журнала.",
    [UI_MENU_COPY_INFO] = "Копировать сведения о системе",
    [UI_MENU_COPY_INFO_DESC] = "Копирует в буфер обмена видеокарту, драйвер, процессор, память и изменённые настройки.",
    [UI_MENU_COPY_INFO_DONE] = "Скопировано", [UI_MENU_ACTION_FAILED] = "не получилось",
    [UI_MENU_RESET] = "Сбросить все настройки", [UI_MENU_RESET_DESC] = "Действует на все вкладки.",
    [UI_MENU_RESET_CONFIRM] = "Сбросить все настройки к значениям по умолчанию?", [UI_MENU_RESET_YES] = "Сбросить",
    [UI_MENU_RESET_NO] = "Отмена",

    [UI_MENU_OV_FPS] = "%.0f FPS", [UI_MENU_OV_FRAME_MS] = "%.1f мс", [UI_MENU_OV_GAME_PCT] = "игра %.0f%%",
    [UI_MENU_OV_MAX_MS] = "макс. %.1f мс",
    [UI_MENU_OV_GPU] = "GPU", [UI_MENU_OV_CPU] = "ЦП", [UI_MENU_OV_RAM] = "ОЗУ", [UI_MENU_OV_VRAM] = "Видеопамять",
    [UI_MENU_OV_TEMP] = "Температура", [UI_MENU_OV_POWER] = "Мощность", [UI_MENU_OV_CLOCK] = "Частота",
    [UI_MENU_OV_NONE] = "-",
    [UI_MENU_OV_REPLAY] = "Подготовка кадра для GPU", [UI_MENU_OV_GPU_TIME] = "Занятость GPU за кадр",
    [UI_MENU_OV_PIPE_WAIT] = "Ожидания конвейеров",
    [UI_MENU_OV_SHADER_WAIT] = "Ожидания шейдеров", [UI_MENU_OV_TEX_UPLOADS] = "Загрузки текстур",
    [UI_MENU_OV_FMT_PCT] = "%.0f%%", [UI_MENU_OV_FMT_TEMP] = "%.0f °C", [UI_MENU_OV_FMT_POWER] = "%.0f Вт",
    [UI_MENU_OV_FMT_MHZ] = "%.0f МГц", [UI_MENU_OV_FMT_GB] = "%.1f ГБ", [UI_MENU_OV_FMT_GB_OF] = "/ %.1f ГБ",
    [UI_MENU_OV_GAME_GB] = "игра %.1f ГБ", [UI_MENU_OV_FMT_MS] = "%.2f мс", [UI_MENU_OV_PER_FRAME] = "%.1f за кадр",
    [UI_MENU_OV_WINDOW] = "Окно", [UI_MENU_OV_OUTPUT] = "Изображение игры", [UI_MENU_OV_SCENE] = "3D-сцена",
    [UI_MENU_OV_SCALING] = "Масштабирование", [UI_MENU_OV_LIMIT] = "Лимит кадров", [UI_MENU_OV_PRESENT] = "Вывод кадров",
    [UI_MENU_OV_FMT_SIZE] = "%ux%u", [UI_MENU_OV_FMT_SCENE] = "%ux%u, %.0f%%",
    [UI_MENU_OV_FMT_FSR] = "AMD FSR 1, резкость %d%%", [UI_MENU_OV_SCALE_NONE] = "нет (1:1)",
    [UI_MENU_OV_SCALE_BILINEAR] = "билинейное",
};

static const char *const text_pt[UI_TEXT_COUNT] = {
    SWKBD("Inserir texto", "Digite um nome", "Cavalo", "OK", "Cancelar", "Controle: A = OK, B = Cancelar"),

    [UI_MENU_TITLE] = "Configurações",
    [UI_MENU_TAB_SCREEN] = "Tela", [UI_MENU_TAB_GAME] = "Jogo", [UI_MENU_TAB_MODS] = "Mods",
    [UI_MENU_TAB_SOUND] = "Som", [UI_MENU_TAB_CONTROLS] = "Controles", [UI_MENU_TAB_MENU] = "Menu", [UI_MENU_TAB_ADVANCED] = "Avançado",
    [UI_MENU_CONTINUE] = "Continuar", [UI_MENU_QUIT] = "Sair do jogo",
    [UI_MENU_ON] = "Ligado", [UI_MENU_OFF] = "Desligado", [UI_MENU_AUTO] = "Automático",
    [UI_MENU_DEFAULT_MARK] = "padrão",
    [UI_MENU_AFTER_RESTART] = "após reiniciar", [UI_MENU_SET_BY_ENV] = "definido por variável de ambiente",
    [UI_MENU_SAVE_FAILED] = "não foi possível salvar as configurações",
    [UI_MENU_VERSION] = "Kingdom Recomp %s, jogo %s",

    [UI_MENU_FULLSCREEN] = "Tela cheia", [UI_MENU_FULLSCREEN_DESC] = "Preenche a tela com o jogo; F11 alterna.",
    [UI_MENU_WINDOW_SIZE] = "Tamanho da janela",
    [UI_MENU_WINDOW_SIZE_DESC] = "Tamanho da janela do jogo fora da tela cheia.",
    [UI_MENU_WINDOW_SIZE_CUSTOM] = "Personalizado",
    [UI_MENU_PRESENT] = "Apresentação", [UI_MENU_PRESENT_DESC] = "Como os quadros prontos chegam à tela.",
    [UI_MENU_PRESENT_MAILBOX] = "Mailbox", [UI_MENU_PRESENT_VSYNC] = "VSync", [UI_MENU_PRESENT_IMMEDIATE] = "Imediato",
    [UI_MENU_PRESENT_IN_USE] = "em uso: %s",
    [UI_MENU_GPU] = "Placa de vídeo", [UI_MENU_GPU_DESC] = "O dispositivo Vulkan que desenha o jogo.",
    [UI_MENU_GPU_AUTO] = "Automático",
    [UI_MENU_PERF_OVERLAY] = "Indicador de desempenho",
    [UI_MENU_PERF_OVERLAY_DESC] = "Taxa de quadros no canto superior direito. Completo adiciona tempos de quadro com "
                                  "gráfico, carga de CPU, GPU e memória, temperaturas e esperas da thread de renderização. "
                                  "F10 alterna os modos.",
    [UI_MENU_PERF_OFF] = "Desligado", [UI_MENU_PERF_FPS] = "FPS", [UI_MENU_PERF_FULL] = "Completo",
    [UI_PERF_LINE_FPS] = "%.0f fps",
    [UI_MENU_GRAPHICS] = "Imagem",
    [UI_MENU_OUTPUT_RES] = "Resolução do jogo",
    [UI_MENU_OUTPUT_RES_DESC] = "Tamanho em que o jogo desenha a imagem e a interface. Acima de 1920 × 1080 o jogo "
                                "precisa de mais memória de vídeo e tempo de GPU.",
    [UI_MENU_RENDER_SCALE] = "Escala de renderização 3D",
    [UI_MENU_RENDER_SCALE_DESC] = "Tamanho da cena 3D em relação à resolução do jogo; o FSR do jogo escala até esse "
                                  "tamanho. Acima de 100 % suaviza serrilhados (supersampling) com alto custo de GPU.",
    [UI_MENU_RENDER_SCALE_GAME] = "Do jogo (83 %)",
    [UI_MENU_UPSCALER] = "Escala para a janela",
    [UI_MENU_UPSCALER_DESC] = "Como a imagem é ampliada quando a janela é maior que a resolução do jogo. FSR mantém "
                              "bordas e detalhes mais nítidos que bilinear.",
    [UI_MENU_UPSCALER_FSR] = "AMD FSR 1", [UI_MENU_UPSCALER_BILINEAR] = "Bilinear",
    [UI_MENU_SHARPNESS] = "Nitidez FSR",
    [UI_MENU_SHARPNESS_DESC] = "Nitidez após a escala FSR; 0 desliga.",

    [UI_MENU_FPS_LIMIT] = "Limite de taxa de quadros",
    [UI_MENU_FPS_LIMIT_DESC] = "O máximo de quadros por segundo que o jogo desenha. Acima de 60 é experimental e "
                               "precisa de Velocidade do jogo segue o relógio.",
    [UI_MENU_FPS_30] = "30 FPS", [UI_MENU_FPS_60] = "60 FPS", [UI_MENU_FPS_120] = "120 FPS", [UI_MENU_FPS_144] = "144 FPS",
    [UI_MENU_FPS_DISPLAY] = "Taxa do monitor", [UI_MENU_FPS_DISPLAY_HZ] = "Taxa do monitor (%.0f Hz)",
    [UI_MENU_REALTIME] = "Velocidade do jogo segue o relógio",
    [UI_MENU_REALTIME_DESC] = "Avança o jogo pelo tempo real, para um quadro lento não deixar o jogo mais lento.",
    [UI_MENU_CLOTH_REALTIME] = "Tecido no relógio",
    [UI_MENU_CLOTH_REALTIME_DESC] = "Tecido e cabelo também avançam pelo tempo real.",
    [UI_MENU_HIGHFPS_FIX] = "Correção de física em alta taxa de quadros",
    [UI_MENU_HIGHFPS_FIX_DESC] = "Corrige a física que falha acima de 30 quadros por segundo.",
    [UI_MENU_GAME_LANG] = "Idioma do jogo", [UI_MENU_GAME_LANG_DESC] = "Idioma do texto e das legendas do jogo.",
    [UI_MENU_GAME_LANG_AUTO] = "Automático",
    [UI_MENU_OPERATION_MODE] = "Modo do console",
    [UI_MENU_OPERATION_MODE_DESC] = "Se o jogo acha que roda na base ou no modo portátil.",
    [UI_MENU_MODS_INTRO] =
        "Mods integrados aplicam na hora. Coloque mods RomFS na pasta mods (cada um com romfs/, como no Eden); ative "
        "aqui e reinicie o jogo para aplicar.",
    [UI_MENU_MODS_EMPTY] = "Nenhum mod RomFS encontrado. Mods integrados aparecem acima quando existirem.",
    [UI_MENU_MODS_ROW_DESC] = "Arquivos RomFS desta pasta substituem dados do jogo na próxima inicialização.",
    [UI_MENU_MODS_OPEN] = "Abrir pasta de mods",
    [UI_MENU_MODS_OPEN_DESC] = "Abre a pasta onde ficam os diretórios dos mods.",
    [UI_MENU_MOD_STAMINA] = "Vigor infinito (correr / escalar)",
    [UI_MENU_MOD_STAMINA_DESC] = "O vigor não gasta ao correr ou escalar. Aplica na hora.",
    [UI_MENU_MOD_HORSE_STAMINA] = "Vigor infinito do cavalo",
    [UI_MENU_MOD_HORSE_STAMINA_DESC] = "O cavalo galopa sem cansar. Aplica na hora.",
    [UI_MENU_MOD_HEALTH] = "Vida infinita",
    [UI_MENU_MOD_HEALTH_DESC] = "Link não recebe dano. Aplica na hora.",
    [UI_MENU_MOD_SUPER_JUMP] = "Super pulo",
    [UI_MENU_MOD_SUPER_JUMP_DESC] = "Pulo mais alto. Aplica na hora.",
    [UI_MENU_MOD_FREE_SHOP] = "Compras grátis (Rupees)",
    [UI_MENU_MOD_FREE_SHOP_DESC] = "Compras em lojas não gastam Rupees. Aplica na hora.",
    [UI_MENU_MOD_ARROWS] = "Flechas infinitas",
    [UI_MENU_MOD_ARROWS_DESC] = "Flechas não são consumidas ao atirar. Aplica na hora.",
    [UI_MENU_MOD_DURABILITY] = "Durabilidade infinita",
    [UI_MENU_MOD_DURABILITY_DESC] = "Armas, escudos e arcos não quebram. Aplica na hora.",
    [UI_MENU_MOD_ZONAI] = "Dispositivos Zonai infinitos",
    [UI_MENU_MOD_ZONAI_DESC] = "Itens Zonai da bolsa não são consumidos. Aplica na hora.",
    [UI_MENU_MOD_FOOD] = "Comida infinita",
    [UI_MENU_MOD_FOOD_DESC] = "Itens de comida no inventário não são consumidos. Aplica na hora.",
    [UI_MENU_MOD_PICKUP_8X] = "Coleta x8",
    [UI_MENU_MOD_PICKUP_8X_DESC] = "Pega oito vezes mais itens de uma vez. Aplica na hora.",
    [UI_MENU_MOD_FOOD_BUFF] = "Bônus de comida infinito",
    [UI_MENU_MOD_FOOD_BUFF_DESC] = "Bônus temporários de comida não expiram. Aplica na hora.",
    [UI_MENU_MOD_BATTERY] = "Bateria infinita",
    [UI_MENU_MOD_BATTERY_DESC] = "A bateria não descarrega. Aplica na hora.",
    [UI_MENU_MODE_DOCKED] = "Na base", [UI_MENU_MODE_HANDHELD] = "Portátil",

    [UI_MENU_VOLUME] = "Volume", [UI_MENU_VOLUME_DESC] = "Volume geral do jogo.",

    [UI_MENU_MOTION] = "Controles de movimento", [UI_MENU_MOTION_DESC] = "Mira com o giroscópio do controle.",
    [UI_MENU_RUMBLE_STRENGTH] = "Intensidade da vibração",
    [UI_MENU_RUMBLE_STRENGTH_DESC] = "Quão forte o controle vibra.",
    [UI_MENU_RUMBLE_CURVE] = "Curva de vibração",
    [UI_MENU_RUMBLE_CURVE_DESC] = "Valores menores deixam a vibração fraca mais forte.",
    [UI_MENU_RUMBLE_TRIGGERS] = "Vibração dos gatilhos (Xbox)",
    [UI_MENU_RUMBLE_TRIGGERS_DESC] = "Vibra também os gatilhos de um controle Xbox.",
    [UI_MENU_PAD_LAYOUT] = "Layout dos botões",
    [UI_MENU_PAD_LAYOUT_DESC] = "Qual botão é A. Nintendo: o da direita, como no Switch. Xbox: o de baixo, como "
                                "rotulado no controle Xbox (cruz no PlayStation). Automático escolhe Nintendo para "
                                "controles com rótulos Nintendo e Xbox para os demais.",
    [UI_MENU_PAD_LAYOUT_AUTO] = "Automático", [UI_MENU_PAD_LAYOUT_NINTENDO] = "Nintendo (A à direita)",
    [UI_MENU_PAD_LAYOUT_XBOX] = "Xbox (A embaixo)", [UI_MENU_PAD_LAYOUT_IN_USE] = "em uso: %s",
    [UI_MENU_MOUSE_SENS] = "Sensibilidade da câmera no mouse",
    [UI_MENU_MOUSE_SENS_DESC] = "Quão rápido o mouse gira a câmera.",

    [UI_BINDS_HELP] = "Selecione uma célula e pressione A ou Enter, depois a tecla, botão do mouse ou botão do "
                      "controle. Delete ou X limpa a célula. Uma tecla em uso vem para cá; um botão do controle troca "
                      "de lugar com o que substituir.",
    [UI_BINDS_COL_KEY1] = "Tecla 1", [UI_BINDS_COL_KEY2] = "Tecla 2", [UI_BINDS_COL_PAD] = "Controle",
    [UI_BINDS_GROUP_BUTTONS] = "Botões", [UI_BINDS_GROUP_MOVE] = "Movimento", [UI_BINDS_GROUP_CAMERA] = "Câmera",
    [UI_BINDS_PRESS_KEY] = "Pressione uma tecla ou botão do mouse para %s",
    [UI_BINDS_PRESS_PAD] = "Pressione um botão do controle para %s",
    [UI_BINDS_CANCEL_HINT] = "Esc cancela, ou aguarde %d s",
    [UI_BINDS_TAKEN] = "%s foi retirado de %s", [UI_BINDS_SWAPPED] = "%s trocado com %s",
    [UI_BINDS_RESET] = "Redefinir todos os botões",
    [UI_BINDS_RESET_DESC] = "Volta às teclas e botões padrão do controle. O layout de botões acima vale de novo: "
                            "botões remapeados do controle ficam onde você os colocou até esta redefinição.",
    [UI_BIND_A] = "A", [UI_BIND_B] = "B", [UI_BIND_X] = "X", [UI_BIND_Y] = "Y",
    [UI_BIND_LCLICK] = "Clique do analógico esquerdo", [UI_BIND_RCLICK] = "Clique do analógico direito", [UI_BIND_L] = "L",
    [UI_BIND_R] = "R", [UI_BIND_ZL] = "ZL", [UI_BIND_ZR] = "ZR", [UI_BIND_PLUS] = "Mais", [UI_BIND_MINUS] = "Menos",
    [UI_BIND_LEFT] = "D-pad esquerda", [UI_BIND_UP] = "D-pad cima", [UI_BIND_RIGHT] = "D-pad direita",
    [UI_BIND_DOWN] = "D-pad baixo",
    [UI_BIND_LSTICK_LEFT] = "Mover esquerda", [UI_BIND_LSTICK_UP] = "Mover para frente", [UI_BIND_LSTICK_RIGHT] = "Mover direita",
    [UI_BIND_LSTICK_DOWN] = "Mover para trás",
    [UI_BIND_RSTICK_LEFT] = "Câmera esquerda", [UI_BIND_RSTICK_UP] = "Câmera cima", [UI_BIND_RSTICK_RIGHT] = "Câmera direita",
    [UI_BIND_RSTICK_DOWN] = "Câmera baixo",
    [UI_BIND_DESC_A] = "Interagir, pegar, conversar.", [UI_BIND_DESC_B] = "Correr, cancelar, voltar.",
    [UI_BIND_DESC_X] = "Pular.", [UI_BIND_DESC_Y] = "Atacar.", [UI_BIND_DESC_LCLICK] = "Agachar.",
    [UI_BIND_DESC_RCLICK] = "Visão em primeira pessoa.", [UI_BIND_DESC_L] = "Habilidades.",
    [UI_BIND_DESC_R] = "Arremessar arma.", [UI_BIND_DESC_ZL] = "Escudo, mirar.", [UI_BIND_DESC_ZR] = "Arco.",
    [UI_BIND_DESC_PLUS] = "Menu, inventário.", [UI_BIND_DESC_MINUS] = "Mapa.",
    [UI_BIND_DESC_DPAD] = "Seleção rápida.",
    [UI_BIND_DESC_MOVE] = "Move o Link como o analógico esquerdo. O analógico esquerdo do controle também funciona sempre.",
    [UI_BIND_DESC_CAMERA] = "Gira a câmera como o analógico direito. O mouse e o analógico direito do controle também "
                            "funcionam sempre.",
    [UI_KEY_SPACE] = "Espaço", [UI_KEY_TAB] = "Tab", [UI_KEY_LSHIFT] = "Shift esquerdo", [UI_KEY_LCTRL] = "Ctrl esquerdo",
    [UI_KEY_UP] = "Seta para cima", [UI_KEY_DOWN] = "Seta para baixo", [UI_KEY_LEFT] = "Seta para esquerda",
    [UI_KEY_RIGHT] = "Seta para direita",
    [UI_KEY_MOUSE_L] = "Botão esquerdo", [UI_KEY_MOUSE_R] = "Botão direito", [UI_KEY_MOUSE_M] = "Botão do meio",
    [UI_KEY_MOUSE_X1] = "Mouse 4", [UI_KEY_MOUSE_X2] = "Mouse 5",
    [UI_KEY_ENTER] = "Enter", [UI_KEY_BACKSPACE] = "Backspace", [UI_KEY_RSHIFT] = "Shift direito",
    [UI_KEY_RCTRL] = "Ctrl direito", [UI_KEY_LALT] = "Alt", [UI_KEY_CAPSLOCK] = "Caps Lock", [UI_KEY_INSERT] = "Insert",
    [UI_KEY_DELETE] = "Delete", [UI_KEY_HOME] = "Home", [UI_KEY_END] = "End", [UI_KEY_PAGEUP] = "Page Up",
    [UI_KEY_PAGEDOWN] = "Page Down", [UI_KEY_NUM_FMT] = "Num %s",
    [UI_PAD_SOUTH] = "Botão de baixo", [UI_PAD_EAST] = "Botão da direita", [UI_PAD_WEST] = "Botão da esquerda",
    [UI_PAD_NORTH] = "Botão de cima", [UI_PAD_LSTICK] = "Analógico esquerdo", [UI_PAD_RSTICK] = "Analógico direito",
    [UI_PAD_L] = "LB", [UI_PAD_R] = "RB", [UI_PAD_START] = "Start", [UI_PAD_BACK] = "Back",
    [UI_PAD_DPAD_LEFT] = "D-pad esquerda", [UI_PAD_DPAD_UP] = "D-pad cima", [UI_PAD_DPAD_RIGHT] = "D-pad direita",
    [UI_PAD_DPAD_DOWN] = "D-pad baixo", [UI_PAD_ZL] = "Gatilho esquerdo", [UI_PAD_ZR] = "Gatilho direito",
    [UI_PAD_CROSS] = "Cruz", [UI_PAD_CIRCLE] = "Círculo", [UI_PAD_SQUARE] = "Quadrado", [UI_PAD_TRIANGLE] = "Triângulo",

    [UI_MENU_MENU_LANG] = "Idioma do menu", [UI_MENU_MENU_LANG_DESC] = "Idioma deste menu e do indicador.",
    [UI_MENU_MENU_LANG_AUTO] = "Automático (do Windows)",

    [UI_MENU_ADV_WARNING] = "Estas configurações são para testes e diagnóstico. Altere apenas se souber por quê.",
    [UI_MENU_GROUP_SCHED] = "Agendamento", [UI_MENU_GROUP_SHADERS] = "Shaders e pipelines",
    [UI_MENU_GROUP_TEXMEM] = "Texturas e memória", [UI_MENU_GROUP_FIXES] = "Correções de renderização",
    [UI_MENU_GROUP_VULKAN] = "Vulkan", [UI_MENU_GROUP_DIAG] = "Diagnóstico",
    [UI_MENU_ASYNC_PREEMPT] = "Preempção de núcleo virtual",
    [UI_MENU_ASYNC_PREEMPT_DESC] = "Permite que uma thread do jogo interrompa outra no mesmo núcleo virtual.",
    [UI_MENU_PREEMPT_OFF] = "Desligado", [UI_MENU_PREEMPT_ON] = "Ligado", [UI_MENU_PREEMPT_INHERIT] = "Só herança",
    [UI_MENU_REPLAY_ASYNC] = "Preparação de quadro para GPU em thread própria",
    [UI_MENU_REPLAY_ASYNC_DESC] = "Prepara quadros para a GPU em thread separada em vez da thread do jogo. Adiciona até "
                                  "um quadro de latência.",
    [UI_MENU_FREE_CORES] = "Portão de núcleo virtual",
    [UI_MENU_FREE_CORES_DESC] = "Deixa só uma thread rodar por núcleo virtual, como no console.",
    [UI_MENU_TIMER_1MS] = "Timer Windows de 1 ms",
    [UI_MENU_TIMER_1MS_DESC] = "Aumenta a resolução do timer do sistema para 1 ms para ritmo de quadros mais estável.",
    [UI_MENU_PIPE_WAIT] = "Tratamento de desenhos em espera",
    [UI_MENU_PIPE_WAIT_DESC] = "Padrão mantém o agendamento atual com worker reservado para desenhos em espera; Antigo "
                              "(draw) usa o agendamento anterior.",
    [UI_MENU_PIPE_WAIT_DEFAULT] = "Padrão", [UI_MENU_PIPE_WAIT_DRAW] = "Antigo (draw)",
    [UI_MENU_PIPE_SKIP] = "Desenhos ignorados enquanto um pipeline compila",
    [UI_MENU_PIPE_SKIP_DESC] = "Quais desenhos são descartados em vez de esperar seu pipeline.",
    [UI_MENU_PIPE_SKIP_STRICT] = "Rigoroso", [UI_MENU_PIPE_SKIP_ANY] = "Qualquer",
    [UI_MENU_PIPE_SKIP_FULLSCREEN] = "Passes em tela cheia",
    [UI_MENU_LIB_PREBUILD] = "Pré-compilação de biblioteca de estágios",
    [UI_MENU_LIB_PREBUILD_DESC] = "Compila bibliotecas de estágios de shader antecipadamente em segundo plano.",
    [UI_MENU_LIB_PREBUILD_THREADS] = "Threads de pré-compilação de biblioteca",
    [UI_MENU_LIB_PREBUILD_THREADS_DESC] = "Threads usadas na pré-compilação; Automático escolhe um número pela CPU.",
    [UI_MENU_PIPE_PREBUILD] = "Pré-compilação de pipeline",
    [UI_MENU_PIPE_PREBUILD_DESC] = "Compila pipelines conhecidos antecipadamente.",
    [UI_MENU_PREWARM_XLATE] = "Aquecimento do tradutor",
    [UI_MENU_PREWARM_XLATE_DESC] = "Threads que traduzem shaders conhecidos na inicialização; Automático usa metade "
                                  "das threads da CPU, no máximo 16.",
    [UI_MENU_GPL] = "Biblioteca de pipeline gráfico",
    [UI_MENU_GPL_DESC] = "Usa a extensão de biblioteca de pipeline Vulkan; desligue se o driver falhar com ela.",
    [UI_MENU_ASTC] = "Decodificação de textura ASTC", [UI_MENU_ASTC_DESC] = "Onde texturas ASTC comprimidas são decodificadas.",
    [UI_MENU_ASTC_AUTO] = "Automático", [UI_MENU_ASTC_GPU] = "Shader GPU", [UI_MENU_ASTC_NATIVE] = "Nativo",
    [UI_MENU_ASTC_CPU] = "CPU",
    [UI_MENU_MEMORY] = "Modo de memória GPU", [UI_MENU_MEMORY_DESC] = "Como a memória do jogo é compartilhada com a GPU.",
    [UI_MENU_MEMORY_AUTO] = "Automático", [UI_MENU_MEMORY_IMPORT] = "Import", [UI_MENU_MEMORY_MIRROR] = "Espelho",
    [UI_MENU_MEMORY_VERIFY] = "Verificar",
    [UI_MENU_TEX_ALIAS] = "Alias de textura",
    [UI_MENU_TEX_ALIAS_DESC] = "Uma textura nova criada sobre a memória de uma textura ativa começa com esse conteúdo "
                               "em vez de vazio.",
    [UI_MENU_LOD_GUARD] = "Proteção LOD",
    [UI_MENU_LOD_GUARD_DESC] = "Evita objetos piscando no lugar errado ou um horizonte plano por um quadro, esperando o "
                               "jogo terminar de reescrever dados de nível de detalhe antes da GPU ler.",
    [UI_MENU_FIX_C0] = "Correção de buffer de constantes",
    [UI_MENU_FIX_C0_DESC] = "Preenche o banco de constantes c0 reservado ao driver (tamanhos de storage, tamanho de "
                           "dispatch, tamanho de ponto, instância base) em vez de ler zeros.",
    [UI_MENU_FIX_ZERO] = "Zerar memória GPU nova",
    [UI_MENU_FIX_ZERO_DESC] = "Pools novos de memória GPU começam zerados, como a memória do Switch.",
    [UI_MENU_BIND_SKIP] = "Ignorar binds repetidos",
    [UI_MENU_BIND_SKIP_DESC] = "Otimização: ignora binds que não mudaram.",
    [UI_MENU_SHADER_MEMO] = "Memo de shader", [UI_MENU_SHADER_MEMO_DESC] = "Otimização: lembra buscas de shader.",
    [UI_MENU_PIPE_MEMO] = "Memo de pipeline", [UI_MENU_PIPE_MEMO_DESC] = "Otimização: reutiliza o pipeline do desenho "
                                                                          "anterior quando o estado se repete.",
    [UI_MENU_TEX_MEMO] = "Memo de bind de textura",
    [UI_MENU_TEX_MEMO_DESC] = "Otimização: lembra descritores de textura entre desenhos.",
    [UI_MENU_DESC_TEMPLATE] = "Modelos de descritor",
    [UI_MENU_DESC_TEMPLATE_DESC] = "Otimização: atualiza descritores em lote.",
    [UI_MENU_PUSH_SKIP] = "Ignorar push constants repetidas",
    [UI_MENU_PUSH_SKIP_DESC] = "Otimização: ignora push constants que não mudaram.",
    [UI_MENU_SPLIT_DRAWS] = "Envio antecipado à GPU",
    [UI_MENU_SPLIT_DRAWS_DESC] = "Envia trabalho à GPU em partes a cada N desenhos entre passes para a GPU começar "
                                "antes; 0 desliga.",
    [UI_MENU_VK_VALIDATION] = "Camada de validação",
    [UI_MENU_VK_VALIDATION_DESC] = "Verifica cada chamada Vulkan e deixa o jogo bem mais lento.",
    [UI_MENU_VK_VALIDATION_MISSING] = "A camada de validação não está instalada.",
    [UI_MENU_VK_DYN_VERTEX] = "Estado de vértice dinâmico",
    [UI_MENU_VK_DYN_VERTEX_DESC] = "Força entrada de vértice dinâmica ligada ou desligada; o padrão depende da GPU.",
    [UI_MENU_VK_SUBGROUP_PIN] = "Fixar tamanho de subgrupo",
    [UI_MENU_VK_SUBGROUP_PIN_DESC] = "Fixa o tamanho de subgrupo do shader.",
    [UI_MENU_TRACE] = "Log de desempenho", [UI_MENU_TRACE_DESC] = "Grava tempos de quadro em arquivo.",
    [UI_MENU_FLIGHT] = "Gravador de voo",
    [UI_MENU_FLIGHT_DESC] = "Mantém um histórico curto e salva quando um quadro trava.",
    [UI_MENU_FLIGHT_SPIKE_MS] = "Limite de travamento (ms)",
    [UI_MENU_FLIGHT_SPIKE_MS_DESC] = "Tempo de quadro que conta como travamento.",
    [UI_MENU_INPUT_RECORD] = "Gravação de entrada",
    [UI_MENU_INPUT_RECORD_DESC] = "Grava suas entradas em arquivo para relatórios de bug.",
    [UI_MENU_OPEN_LOG] = "Abrir pasta de logs", [UI_MENU_OPEN_LOG_DESC] = "Mostra a pasta com os arquivos de log.",
    [UI_MENU_COPY_INFO] = "Copiar informações do sistema",
    [UI_MENU_COPY_INFO_DESC] = "Copia GPU, driver, CPU, RAM e configurações alteradas para a área de transferência.",
    [UI_MENU_COPY_INFO_DONE] = "Copiado", [UI_MENU_ACTION_FAILED] = "não funcionou",
    [UI_MENU_RESET] = "Redefinir todas as configurações", [UI_MENU_RESET_DESC] = "Vale para todas as abas.",
    [UI_MENU_RESET_CONFIRM] = "Redefinir cada configuração para o padrão?", [UI_MENU_RESET_YES] = "Redefinir",
    [UI_MENU_RESET_NO] = "Cancelar",

    [UI_MENU_OV_FPS] = "%.0f FPS", [UI_MENU_OV_FRAME_MS] = "%.1f ms", [UI_MENU_OV_GAME_PCT] = "jogo %.0f%%",
    [UI_MENU_OV_MAX_MS] = "máx. %.1f ms",
    [UI_MENU_OV_GPU] = "GPU", [UI_MENU_OV_CPU] = "CPU", [UI_MENU_OV_RAM] = "RAM", [UI_MENU_OV_VRAM] = "VRAM",
    [UI_MENU_OV_TEMP] = "Temperatura", [UI_MENU_OV_POWER] = "Potência", [UI_MENU_OV_CLOCK] = "Clock",
    [UI_MENU_OV_NONE] = "-",
    [UI_MENU_OV_REPLAY] = "Preparação de quadro para GPU", [UI_MENU_OV_GPU_TIME] = "GPU ocupada por quadro",
    [UI_MENU_OV_PIPE_WAIT] = "Esperas de pipeline",
    [UI_MENU_OV_SHADER_WAIT] = "Esperas de shader", [UI_MENU_OV_TEX_UPLOADS] = "Envios de textura",
    [UI_MENU_OV_FMT_PCT] = "%.0f%%", [UI_MENU_OV_FMT_TEMP] = "%.0f °C", [UI_MENU_OV_FMT_POWER] = "%.0f W",
    [UI_MENU_OV_FMT_MHZ] = "%.0f MHz", [UI_MENU_OV_FMT_GB] = "%.1f GB", [UI_MENU_OV_FMT_GB_OF] = "/ %.1f GB",
    [UI_MENU_OV_GAME_GB] = "jogo %.1f GB", [UI_MENU_OV_FMT_MS] = "%.2f ms", [UI_MENU_OV_PER_FRAME] = "%.1f por quadro",
    [UI_MENU_OV_WINDOW] = "Janela", [UI_MENU_OV_OUTPUT] = "Imagem do jogo", [UI_MENU_OV_SCENE] = "Cena 3D",
    [UI_MENU_OV_SCALING] = "Escala", [UI_MENU_OV_LIMIT] = "Limite de quadros", [UI_MENU_OV_PRESENT] = "Apresentação",
    [UI_MENU_OV_FMT_SIZE] = "%ux%u", [UI_MENU_OV_FMT_SCENE] = "%ux%u, %.0f%%",
    [UI_MENU_OV_FMT_FSR] = "AMD FSR 1, nitidez %d%%", [UI_MENU_OV_SCALE_NONE] = "nenhuma (1:1)",
    [UI_MENU_OV_SCALE_BILINEAR] = "bilinear",
};

#define SWKBD_ONLY(name, ...) static const char *const name[UI_TEXT_COUNT] = {SWKBD(__VA_ARGS__)}
SWKBD_ONLY(text_ja, "文字入力", "名前を入力してください", "ウマ", "OK", "キャンセル", "コントローラー: A = OK、B = キャンセル");
SWKBD_ONLY(text_fr, "Saisie de texte", "Entrez un nom", "Cheval", "OK", "Annuler", "Manette : A = OK, B = Annuler");
SWKBD_ONLY(text_de, "Texteingabe", "Gib einen Namen ein", "Pferd", "OK", "Abbrechen", "Controller: A = OK, B = Abbrechen");
SWKBD_ONLY(text_it, "Inserimento testo", "Inserisci un nome", "Cavallo", "OK", "Annulla", "Controller: A = OK, B = Annulla");
SWKBD_ONLY(text_es, "Introducir texto", "Introduce un nombre", "Caballo", "Aceptar", "Cancelar",
           "Mando: A = Aceptar, B = Cancelar");
SWKBD_ONLY(text_nl, "Tekstinvoer", "Voer een naam in", "Paard", "OK", "Annuleren", "Controller: A = OK, B = Annuleren");
SWKBD_ONLY(text_zh_hans, "文字输入", "请输入名字", "马儿", "确定", "取消", "手柄：A 确定，B 取消");
SWKBD_ONLY(text_zh_hant, "文字輸入", "請輸入名字", "馬兒", "確定", "取消", "手把：A 確定，B 取消");
SWKBD_ONLY(text_ko, "문자 입력", "이름을 입력하세요", "말", "확인", "취소", "컨트롤러: A 확인, B 취소");

static const Row rows[] = {
    {"en", text_en}, {"ja", text_ja}, {"fr", text_fr}, {"de", text_de}, {"it", text_it}, {"es", text_es},
    {"nl", text_nl}, {"pt", text_pt}, {"ru", text_ru}, {"zh-Hans", text_zh_hans}, {"zh-Hant", text_zh_hant},
    {"ko", text_ko},
};

const char *ui_text_lang(const char *code) {
    static const struct { const char *code, *lang; } map[] = {
        {"en-US", "en"}, {"en-GB", "en"}, {"fr-CA", "fr"}, {"es-419", "es"}, {"pt-BR", "pt"},
        {"zh-CN", "zh-Hans"}, {"zh-TW", "zh-Hant"},
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcmp(code, map[i].code)) return map[i].lang;
    return code;
}

static const Row *find_row(const char *lang) {
    for (size_t i = 0; i < sizeof rows / sizeof *rows; i++)
        if (!strcmp(rows[i].lang, lang)) return &rows[i];
    return NULL;
}

int ui_text_defined(const char *lang, UiTextId id) {
    const Row *r = find_row(lang);
    return (unsigned)id < UI_TEXT_COUNT && r && r->text[id] && *r->text[id];
}

const char *ui_text_in(const char *lang, UiTextId id) {
    if ((unsigned)id >= UI_TEXT_COUNT) return "";
    return ui_text_defined(lang, id) ? find_row(lang)->text[id] : rows[0].text[id];
}

const char *ui_text(UiTextId id) { return ui_text_in(ui_text_lang(ov_language()), id); }

// The menu language, re-read when cfg_version changes. TOTK_MENU_LANG "ru", "en" or "pt"; anything else (unset included)
// follows the system UI language: Russian, Portuguese or English.
static const char *resolve_menu_lang(void) {
    const char *v = cfg_value("TOTK_MENU_LANG");
    if (v && !strcmp(v, "ru")) return "ru";
    if (v && !strcmp(v, "en")) return "en";
    if (v && !strcmp(v, "pt")) return "pt";
    const char *sys = plat_ui_language();
    if (sys && (!strcmp(sys, "ru") || !strncmp(sys, "ru-", 3))) return "ru";
    if (sys && (!strncmp(sys, "pt", 2) || !strncmp(sys, "PT", 2))) return "pt";
    return "en";
}

const char *menu_text(UiTextId id) {
    static _Atomic(const char *) lang = "en";
    static _Atomic unsigned seen = ~0u;
    unsigned v = cfg_version();
    if (v != atomic_load_explicit(&seen, memory_order_relaxed)) {
        atomic_store_explicit(&lang, resolve_menu_lang(), memory_order_relaxed);
        atomic_store_explicit(&seen, v, memory_order_relaxed);
    }
    return ui_text_in(atomic_load_explicit(&lang, memory_order_relaxed), id);
}
