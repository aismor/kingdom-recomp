// Linux stub: in-game text entry is not implemented yet; the game gets a cancelled keyboard result.
#include "host.h"

static void ov_show_keyboard(Cpu *c) {
    ret_x(c, 1);   // ResultCancelled
}

HOST_OVERRIDE("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", ov_show_keyboard);
