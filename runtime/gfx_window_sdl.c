// SDL3 window, Vulkan surface and swapchain, present blit, 60 Hz vsync thread, focus (Linux host).
// Swapchain and present path match runtime/gfx_window.c (Win32); the platform layer uses SDL3 instead of a message loop.
//
// Threads: the window thread owns the SDL_Window and its event loop and never runs guest code. The vsync thread signals
// the bound system events. Presents come from any host thread and take g_pl; every queue submission takes vk_queue_lock.
// The swapchain is rebuilt lazily: a resize only marks it dirty, the next present (or gfx_window_resize) rebuilds it.
//
// The in-game menu (ui/menu.h) draws over the game image in the present's command buffer, after the blit; keyboard and
// mouse reach input_window_msg (input_win.c) and menu_window_msg (ui/menu_win.cpp) through synthetic Win32-style
// messages, and their state feeds GetAsyncKeyState and GetForegroundWindow (os_linux.c os_key_state, os_foreground).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "os.h"
#include "vk.h"

#include "cfg.h"
#include "flight.h"
#include "fs.h"
#include "gfx.h"
#include "host.h"
#include "input.h"

#include <SDL3/SDL.h>
#include "ui/menu.h"
#include <SDL3/SDL_vulkan.h>

extern GfxStats gfx_counters;

#define WM_CREATE 0x0001
#define WM_ACTIVATEAPP 0x001C
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_APP_SETTINGS (WM_APP + 1)
#define WM_APP_FULLSCREEN (WM_APP + 2)
#define WHEEL_DELTA 120
#define GET_X_LPARAM(lp) ((int)(short)((lp) & 0xffff))
#define GET_Y_LPARAM(lp) ((int)(short)(((lp) >> 16) & 0xffff))
#define GET_WHEEL_DELTA_WPARAM(w) ((short)((w) >> 16))

#define FRAMES 2
#define MAX_IMAGES 16
#define VSYNC_NS 1000000000ull
#define VSYNC_HZ 60

// ---- queue lock
static CRITICAL_SECTION g_ql;
__attribute__((constructor)) static void window_init_locks(void);
void vk_queue_lock(void) { EnterCriticalSection(&g_ql); }
void vk_queue_unlock(void) { LeaveCriticalSection(&g_ql); }

// ---- window state
static SDL_Window *g_window;
static HANDLE g_wnd_ready;
static VkSurfaceKHR g_surface;
static volatile LONG g_dirty;               // WM_SIZE seen; swapchain stale
static volatile LONG g_focused = 1;
static volatile LONG g_focus_msgs;
static CRITICAL_SECTION g_start_lock;       // gfx_window_start / vsync start
static int g_started, g_wnd_failed;
static int g_background;                   // TOTK_BACKGROUND_WINDOW: the window stays behind the user's windows
static int g_fs_pending;                   // window thread: TOTK_FULLSCREEN waits for the first activation

// ---- swapchain and present state (g_pl)
static CRITICAL_SECTION g_pl;
static VkSwapchainKHR g_sc;
static VkFormat g_sc_fmt;
static VkExtent2D g_sc_ext;
static int g_scaler;                        // the last present: 0 the game image 1:1, 1 scaled by the blit, 2 by FSR
static uint32_t g_sc_n;
static VkImage g_sc_img[MAX_IMAGES];
static VkImageView g_sc_view[MAX_IMAGES];   // colour attachment views for the menu overlay
static VkSemaphore g_render_done[MAX_IMAGES];
static VkCommandPool g_pool;
static VkCommandBuffer g_cb[FRAMES];
static VkFence g_fence[FRAMES];
static VkSemaphore g_acquire[FRAMES];
static unsigned g_frame;
static int g_capture;
static VkBuffer g_cap_buf;
static VmaAllocation g_cap_alloc;
static uint8_t *g_cap_ptr;
static VkDeviceSize g_cap_size;
static int g_cap_w, g_cap_h, g_cap_valid;
static int g_flip_on;            // vk_present_blit submitted the blit of swapchain image g_flip_idx; vk_present_flip presents it
static uint32_t g_flip_idx;

// ---- TOTK_FLIGHT thumbnails (flight.c; g_pl): each present also blits the swapchain image down into slot frame % g_th_n
// of a host-visible ring. A slot's tag names the frame it holds complete; recording a new frame clears the tag first,
// and the tag is set once the fence of that present has signalled.
#define TH_BYTES ((VkDeviceSize)FLIGHT_THUMB_W * FLIGHT_THUMB_H * 4)
static int g_th_n, g_th_failed;
static uint64_t g_th_frame;                // frame of the next vk_present_image call (vk_thumbs_frame)
static uint64_t *g_th_tag;                 // per ring slot
static uint64_t g_th_pending[FRAMES];      // per fence slot: the frame whose thumbnail its submission writes
static VkImage g_th_img;
static VmaAllocation g_th_img_alloc;
static VkFormat g_th_fmt;
static VkBuffer g_th_buf;
static VmaAllocation g_th_alloc;
static uint8_t *g_th_ptr;

static void window_init_locks(void) {
    InitializeCriticalSection(&g_ql);
    InitializeCriticalSection(&g_pl);
    InitializeCriticalSection(&g_start_lock);
}

// ---- vsync
static SRWLOCK g_vs_lock = SRWLOCK_INIT;
static uint64_t g_vs_ev[16];
static int g_vs_nev;
static volatile long *g_vs_ctr[4];
static int g_vs_nctr;
static volatile LONGLONG g_ticks;
static volatile LONGLONG g_vs_start_ns;
static int g_vs_started;

static void vsync_sleep_until(uint64_t due_ns) {
    for (;;) {
        uint64_t now = kern_now_ns();
        if (now >= due_ns) return;
        uint64_t left = due_ns - now;
        struct timespec ts;
        ts.tv_sec = (time_t)(left / 1000000000ull);
        ts.tv_nsec = (long)(left % 1000000000ull);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (ts.tv_sec == 0 && ts.tv_nsec < 1000000L) ts.tv_nsec = 1000000L;
        nanosleep(&ts, NULL);
    }
}

static DWORD WINAPI vsync_main(void *arg) {
    (void)arg;
    uint64_t n = 0;
    for (;;) {
        uint64_t start = (uint64_t)g_vs_start_ns;
        uint64_t due = start + (n + 1) * VSYNC_NS / VSYNC_HZ;
        uint64_t now = kern_now_ns();
        if (now > due + 100000000ull) {                  // the process stalled: restart the schedule, no catch-up burst
            g_vs_start_ns = (LONGLONG)(now - (n + 1) * VSYNC_NS / VSYNC_HZ);
        } else if (due > now) {
            vsync_sleep_until(due);
        }
        n++;
        InterlockedExchange64(&g_ticks, (LONGLONG)n);
        uint64_t ev[16];
        volatile long *ctr[4];
        int ne, nc;
        AcquireSRWLockShared(&g_vs_lock);
        ne = g_vs_nev;
        nc = g_vs_nctr;
        memcpy(ev, g_vs_ev, sizeof ev);
        memcpy(ctr, g_vs_ctr, sizeof ctr);
        ReleaseSRWLockShared(&g_vs_lock);
        for (int i = 0; i < nc; i++) InterlockedIncrement(ctr[i]);
        for (int i = 0; i < ne; i++) kern_signal_system_event(ev[i]);
    }
    return 0;
}

static void vsync_ensure(void) {
    EnterCriticalSection(&g_start_lock);
    if (!g_vs_started) {
        g_vs_started = 1;
        g_vs_start_ns = (LONGLONG)kern_now_ns();
        HANDLE h = CreateThread(NULL, 0, vsync_main, NULL, 0, NULL);
        if (!h) rt_fatal("gfx: cannot start the vsync thread (error %lu)", GetLastError());
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_start_lock);
}

void gfx_vsync_bind(uint64_t system_event) {
    AcquireSRWLockExclusive(&g_vs_lock);
    int have = 0;
    for (int i = 0; i < g_vs_nev; i++) have |= g_vs_ev[i] == system_event;
    if (!have) {
        if (g_vs_nev == 16) rt_fatal("gfx: more than 16 vsync events");
        g_vs_ev[g_vs_nev++] = system_event;
    }
    ReleaseSRWLockExclusive(&g_vs_lock);
    vsync_ensure();
}

void gfx_vsync_bind_counter(volatile long *counter) {
    AcquireSRWLockExclusive(&g_vs_lock);
    if (g_vs_nctr == 4) rt_fatal("gfx: more than 4 vsync counters");
    g_vs_ctr[g_vs_nctr++] = counter;
    ReleaseSRWLockExclusive(&g_vs_lock);
    vsync_ensure();
}

uint64_t gfx_vsync_ticks(void) { return (uint64_t)InterlockedCompareExchange64(&g_ticks, 0, 0); }
uint64_t gfx_vsync_time_ns(uint64_t tick) { return (uint64_t)g_vs_start_ns + tick * VSYNC_NS / VSYNC_HZ; }

// ---- focus
int gfx_window_focused(void) { return g_focused; }
unsigned gfx_window_focus_messages(void) { return (unsigned)g_focus_msgs; }
HWND gfx_window_handle(void) { return (HWND)g_window; }

static int window_minimized(void) {
    return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED);
}

static void window_client_size(int *w, int *h) {
    if (!g_window) {
        *w = *h = 0;
        return;
    }
    SDL_GetWindowSizeInPixels(g_window, w, h);
}

static HWND window_hwnd(void) { return (HWND)g_window; }

BOOL GetClientRect(HWND h, RECT *r) {
    if (!r || !h || h != window_hwnd()) return FALSE;
    int w, hh;
    window_client_size(&w, &hh);
    *r = (RECT){0, 0, w, hh};
    return TRUE;
}

// ---- window thread
static int g_saved_x, g_saved_y, g_saved_w, g_saved_h;   // restored when fullscreen ends

static int is_fullscreen(SDL_Window *w) {
    return w && (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN);
}

// TOTK_FULLSCREEN=1: borderless window over the monitor. TOTK_WINDOW_SIZE=<w>x<h>: client area of the window (the
// swapchain follows the window; the game image is scaled to it).
static int cfg_fullscreen(void) {
    return cfg_bool("TOTK_FULLSCREEN");
}

static void cfg_window_size(int *w, int *h) {
    int x = 0, y = 0;
    if (sscanf(cfg_value("TOTK_WINDOW_SIZE"), "%dx%d", &x, &y) != 2 || x < 640 || y < 360 || x > 7680 || y > 4320)
        sscanf(cfg_meta("WINDOW_SIZE")->def, "%dx%d", &x, &y);   // unparsable or out of range: the default
    *w = x;
    *h = y;
}

// F11: borderless window over the whole monitor, and back to the saved window. The swapchain follows through WM_SIZE.
// Not Alt+Enter: Enter is the Plus button (input_win.c). A background window (test runs) keeps its place behind the
// user's windows and takes no focus.
static void toggle_fullscreen(SDL_Window *win) {
    if (!win) return;
    if (!is_fullscreen(win)) {
        SDL_GetWindowPosition(win, &g_saved_x, &g_saved_y);
        SDL_GetWindowSize(win, &g_saved_w, &g_saved_h);
        SDL_SetWindowFullscreenMode(win, NULL);
        SDL_SetWindowFullscreen(win, true);
    } else {
        SDL_SetWindowFullscreen(win, false);
        if (g_saved_w > 0 && g_saved_h > 0) {
            SDL_SetWindowSize(win, g_saved_w, g_saved_h);
            SDL_SetWindowPosition(win, g_saved_x, g_saved_y);
        }
    }
}

// The window size and fullscreen state last taken from the settings (window thread). apply_settings acts on a change of
// either value only, so a window resized by hand keeps its size when another setting changes.
static char g_applied_size[32];
static int g_applied_fs;

static void note_applied(void) {
    snprintf(g_applied_size, sizeof g_applied_size, "%s", cfg_value("TOTK_WINDOW_SIZE"));
    g_applied_fs = cfg_fullscreen();
}

// Window thread: the window takes the size and fullscreen state of the settings when they changed. In fullscreen the
// size goes into the placement restored when fullscreen ends.
static void apply_settings(SDL_Window *win) {
    int size_changed = strcmp(cfg_value("TOTK_WINDOW_SIZE"), g_applied_size) != 0, fs = cfg_fullscreen(), fs_changed = fs != g_applied_fs;
    note_applied();
    if (size_changed) {
        int cw, ch;
        cfg_window_size(&cw, &ch);
        if (is_fullscreen(win)) {
            g_saved_w = cw;
            g_saved_h = ch;
        } else {
            int have_w, have_h;
            SDL_GetWindowSize(win, &have_w, &have_h);
            if (have_w != cw || have_h != ch) SDL_SetWindowSize(win, cw, ch);
        }
    }
    if (fs_changed && fs != is_fullscreen(win)) {
        input_cursor_suspend(1);
        toggle_fullscreen(win);
        input_cursor_suspend(0);
    }
}

static volatile LONG g_settings_pending;

void gfx_window_apply_settings(void) {
    if (g_window) InterlockedExchange(&g_settings_pending, 1);
}

// Present path (g_pl held): a settings change (cfg_version) reaches the window thread, which applies a changed size or
// fullscreen; a changed TOTK_VK_PRESENT rebuilds the swapchain at this present.
static void window_settings_live(void) {
    static unsigned seen;
    static int started;
    static char present[16];   // the TOTK_VK_PRESENT the swapchain was made with
    unsigned v = cfg_version();
    if (started && v == seen) return;
    seen = v;
    const char *e = cfg_value("TOTK_VK_PRESENT");
    if (!started || strncmp(e, present, sizeof present - 1)) {
        snprintf(present, sizeof present, "%s", e);
        if (started) InterlockedExchange(&g_dirty, 1);
    }
    if (started) gfx_window_apply_settings();
    started = 1;
}

void gfx_window_request_close(void) {
    if (g_window) {
        SDL_Event e = {.type = SDL_EVENT_QUIT};
        SDL_PushEvent(&e);
    }
}

// ---- refresh rate of the window's display (TOTK_FPS_LIMIT=display).
static SDL_DisplayID g_rate_display;
static volatile LONG g_rate_mhz;   // millihertz, 0 unknown

static void refresh_rate_update(SDL_Window *win, int force) {
    if (!win) return;
    SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
    if (!force && disp == g_rate_display) return;
    g_rate_display = disp;
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(disp);
    double hz = mode ? (double)mode->refresh_rate : 0;
    LONG mhz = (LONG)(hz * 1000 + 0.5);
    if (InterlockedExchange(&g_rate_mhz, mhz) != mhz && hz > 0) rt_log("gfx: monitor refresh rate %.3f Hz", hz);
}

double gfx_window_refresh_hz(void) { return InterlockedCompareExchange(&g_rate_mhz, 0, 0) / 1000.0; }

int gfx_window_monitor_size(int *w, int *h) {
    if (!g_window) return 0;
    SDL_DisplayID disp = SDL_GetDisplayForWindow(g_window);
    SDL_Rect r;
    if (!SDL_GetDisplayBounds(disp, &r)) return 0;
    *w = r.w;
    *h = r.h;
    return 1;
}

static WPARAM sdl_key_to_vk(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) return (WPARAM)('A' + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9) return (WPARAM)('0' + (key - SDLK_0));
    switch (key) {
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_TAB: return VK_TAB;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_F1: return VK_F1;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_Q: return 'Q';
    case SDLK_E: return 'E';
    default: return 0;
    }
}

static void dispatch_winmsg(UINT m, WPARAM w, LPARAM l) {
    HWND h = window_hwnd();
    LRESULT input_res, menu_res;
    // Ahead of the handlers, as Windows updates its key state and foreground window before the message arrives.
    switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: os_key_state((int)w, m == WM_KEYDOWN); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: os_key_state(VK_LBUTTON, m == WM_LBUTTONDOWN); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: os_key_state(VK_RBUTTON, m == WM_RBUTTONDOWN); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: os_key_state(VK_MBUTTON, m == WM_MBUTTONDOWN); break;
    case WM_ACTIVATEAPP: os_foreground(w ? h : NULL); break;
    }
    if (input_window_msg(h, m, w, l, &input_res)) return;
    if (menu_window_msg(h, m, w, l, &menu_res)) return;
    SDL_Window *win = g_window;
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_F11 && !(l & (1 << 30))) {
            input_cursor_suspend(1);
            toggle_fullscreen(win);
            input_cursor_suspend(0);
            cfg_set("TOTK_FULLSCREEN", is_fullscreen(win) ? "1" : "0");
            cfg_save();
            note_applied();
        }
        break;
    case WM_APP_SETTINGS:
        apply_settings(win);
        break;
    case WM_APP_FULLSCREEN:
        if (cfg_fullscreen() && !is_fullscreen(win)) {
            input_cursor_suspend(1);
            toggle_fullscreen(win);
            input_cursor_suspend(0);
        }
        break;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) InterlockedExchange(&g_dirty, 1);
        refresh_rate_update(win, 0);
        break;
    case WM_ACTIVATEAPP: {
        int in = w != 0;
        InterlockedExchange(&g_focused, in);
        oe_set_focus(in);
        oe_push_message(OE_MSG_FOCUS_STATE_CHANGED);
        InterlockedIncrement(&g_focus_msgs);
        if (in && g_fs_pending) {
            g_fs_pending = 0;
            dispatch_winmsg(WM_APP_FULLSCREEN, 0, 0);
        }
        break;
    }
    case WM_CLOSE:
        rt_log("gfx: window closed; exiting");
        fs_save_flush();
        vk_pipeline_save();
        prof_write();
        ExitProcess(0);
    default:
        break;
    }
}

// ---- messages from other threads
// SendMessageW and PostMessageW queue a message for the window thread, which dispatches it between SDL event polls,
// as Win32 delivers a cross-thread message on the window's own thread. SendMessageW waits for the dispatch; called on
// the window thread it dispatches at once.
typedef struct { UINT m; WPARAM w; LPARAM l; } WinMsg;
#define MSGQ_CAP 64
static SRWLOCK g_msgq_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_msgq_done = CONDITION_VARIABLE_INIT;
static WinMsg g_msgq[MSGQ_CAP];
static uint64_t g_msgq_head, g_msgq_tail;   // messages queued, messages dispatched
static DWORD g_window_tid;

static void msgq_drain(void) {
    AcquireSRWLockExclusive(&g_msgq_lock);
    while (g_msgq_tail < g_msgq_head) {
        WinMsg msg = g_msgq[g_msgq_tail % MSGQ_CAP];
        ReleaseSRWLockExclusive(&g_msgq_lock);
        dispatch_winmsg(msg.m, msg.w, msg.l);
        AcquireSRWLockExclusive(&g_msgq_lock);
        g_msgq_tail++;
        WakeAllConditionVariable(&g_msgq_done);
    }
    ReleaseSRWLockExclusive(&g_msgq_lock);
}

static uint64_t msgq_push(UINT m, WPARAM w, LPARAM l) {
    AcquireSRWLockExclusive(&g_msgq_lock);
    while (g_msgq_head - g_msgq_tail >= MSGQ_CAP) SleepConditionVariableSRW(&g_msgq_done, &g_msgq_lock, INFINITE, 0);
    g_msgq[g_msgq_head % MSGQ_CAP] = (WinMsg){m, w, l};
    uint64_t ticket = ++g_msgq_head;
    ReleaseSRWLockExclusive(&g_msgq_lock);
    return ticket;
}

BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!h || h != window_hwnd()) return FALSE;
    msgq_push(m, w, l);
    return TRUE;
}

LRESULT SendMessageW(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!h || h != window_hwnd()) return 0;
    if (GetCurrentThreadId() == g_window_tid) {
        dispatch_winmsg(m, w, l);
        return 0;
    }
    uint64_t ticket = msgq_push(m, w, l);
    AcquireSRWLockExclusive(&g_msgq_lock);
    while (g_msgq_tail < ticket) SleepConditionVariableSRW(&g_msgq_done, &g_msgq_lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&g_msgq_lock);
    return 0;
}

// Cursor capture (input_win.c cursor_update): SDL relative mouse mode hides the cursor, keeps it in the window and
// reports motion past the window edge. SDL wants it set on the window thread, so other threads leave the request here.
static volatile LONG g_capture_want, g_capture_have;

void gfx_window_capture_mouse(int on) { InterlockedExchange(&g_capture_want, on != 0); }

static void capture_apply(void) {
    LONG want = g_capture_want;
    if (!g_window || want == g_capture_have) return;
    if (!SDL_SetWindowRelativeMouseMode(g_window, want != 0))
        rt_log("gfx: SDL_SetWindowRelativeMouseMode(%ld) failed (%s)", (long)want, SDL_GetError());
    g_capture_have = want;
}

static SDL_Window *sdl_event_window(const SDL_Event *ev) {
    if (ev->type >= SDL_EVENT_WINDOW_FIRST && ev->type <= SDL_EVENT_WINDOW_LAST)
        return SDL_GetWindowFromID(ev->window.windowID);
    return g_window;
}

static void handle_sdl_event(SDL_Event *ev) {
    if (sdl_event_window(ev) != g_window && ev->type != SDL_EVENT_QUIT) return;
    switch (ev->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        dispatch_winmsg(WM_CLOSE, 0, 0);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        dispatch_winmsg(WM_SIZE, SIZE_MINIMIZED, 0);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        dispatch_winmsg(WM_SIZE, 0, 0);
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        refresh_rate_update(g_window, 1);
        dispatch_winmsg(WM_SIZE, 0, 0);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        dispatch_winmsg(WM_ACTIVATEAPP, 1, 0);
        dispatch_winmsg(WM_SETFOCUS, 0, 0);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        dispatch_winmsg(WM_ACTIVATEAPP, 0, 0);
        dispatch_winmsg(WM_KILLFOCUS, 0, 0);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        WPARAM vk = sdl_key_to_vk(ev->key.key);
        if (!vk) break;
        UINT m = ev->type == SDL_EVENT_KEY_DOWN ? WM_KEYDOWN : WM_KEYUP;
        LPARAM l = ev->key.repeat ? (1 << 30) : 0;
        dispatch_winmsg(m, vk, l);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        // Relative motion in fractions of a pixel: the remainder carries to the next event, as raw input counts do.
        static float rem_x, rem_y;
        float fx = ev->motion.xrel + rem_x, fy = ev->motion.yrel + rem_y;
        int dx = (int)fx, dy = (int)fy;
        rem_x = fx - (float)dx, rem_y = fy - (float)dy;
        if (dx || dy) input_mouse_raw(dx, dy);
        int ix = (int)ev->motion.x, iy = (int)ev->motion.y;
        dispatch_winmsg(WM_MOUSEMOVE, 0, (LPARAM)((iy << 16) | (ix & 0xffff)));
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int ix = (int)ev->button.x, iy = (int)ev->button.y;
        LPARAM lp = (LPARAM)((iy << 16) | (ix & 0xffff));
        UINT m = 0;
        if (ev->button.button == SDL_BUTTON_LEFT)
            m = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        else if (ev->button.button == SDL_BUTTON_RIGHT)
            m = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? WM_RBUTTONDOWN : WM_RBUTTONUP;
        else if (ev->button.button == SDL_BUTTON_MIDDLE)
            m = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? WM_MBUTTONDOWN : WM_MBUTTONUP;
        else if (ev->button.button == SDL_BUTTON_X1 || ev->button.button == SDL_BUTTON_X2)
            os_key_state(ev->button.button == SDL_BUTTON_X1 ? VK_XBUTTON1 : VK_XBUTTON2,
                         ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (m) dispatch_winmsg(m, 0, lp);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        dispatch_winmsg(WM_MOUSEWHEEL, (WPARAM)((short)(ev->wheel.y * WHEEL_DELTA) << 16), 0);
        break;
    default:
        break;
    }
}

static DWORD WINAPI window_main(void *arg) {
    (void)arg;
    g_window_tid = GetCurrentThreadId();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        rt_log("gfx: SDL_Init VIDEO failed (%s)", SDL_GetError());
        g_wnd_failed = 1;
        SetEvent(g_wnd_ready);
        return 1;
    }
    int cw, ch;
    cfg_window_size(&cw, &ch);
    g_background = cfg_get("TOTK_BACKGROUND_WINDOW") && atoi(cfg_get("TOTK_BACKGROUND_WINDOW"));
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    g_window = SDL_CreateWindow("TotK Recomp", cw, ch, flags);
    if (!g_window) {
        rt_log("gfx: SDL_CreateWindow failed (%s)", SDL_GetError());
        g_wnd_failed = 1;
    } else {
        SDL_ShowWindow(g_window);
        if (g_background) SDL_MinimizeWindow(g_window);
        else g_fs_pending = cfg_fullscreen();
        note_applied();
        refresh_rate_update(g_window, 1);
        dispatch_winmsg(WM_CREATE, 0, 0);
    }
    SetEvent(g_wnd_ready);
    if (g_wnd_failed) return 1;
    for (;;) {
        if (InterlockedCompareExchange(&g_settings_pending, 0, 0)) {
            InterlockedExchange(&g_settings_pending, 0);
            dispatch_winmsg(WM_APP_SETTINGS, 0, 0);
        }
        msgq_drain();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handle_sdl_event(&ev);
        capture_apply();
        SDL_Delay(1);
    }
    return 0;
}

// ---- swapchain
static const char *g_present_got = "";   // the TOTK_VK_PRESENT value of the mode the swapchain got; "" before one

static VkPresentModeKHR pick_present_mode(VkPhysicalDevice pd) {
    const char *e = cfg_value("TOTK_VK_PRESENT");
    VkPresentModeKHR want = !strcmp(e, "mailbox") ? VK_PRESENT_MODE_MAILBOX_KHR
                          : !strcmp(e, "immediate") ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, g_surface, &n, NULL);
    VkPresentModeKHR m[16];
    if (n > 16) n = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, g_surface, &n, m);
    VkPresentModeKHR got = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < n; i++) if (m[i] == want) got = want;
    const char *name = got == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : got == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo";
    __atomic_store_n(&g_present_got, name, __ATOMIC_RELEASE);
    static int logged = -1;   // the mode last logged: a change from the menu shows in the log
    if ((int)got != logged) {
        logged = (int)got;
        rt_log("gfx: present mode %s (TOTK_VK_PRESENT=%s)", name, e);
    }
    return got;
}

const char *gfx_window_present_mode(void) { return __atomic_load_n(&g_present_got, __ATOMIC_ACQUIRE); }

// Queues the present of a blit vk_present_blit left waiting (g_pl held). A swapchain rebuild and the next blit flush it
// first; the device release drops it.
static void flip_locked(void) {
    if (!g_flip_on) return;
    g_flip_on = 0;
    if (!g_sc) return;
    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &g_render_done[g_flip_idx], .swapchainCount = 1, .pSwapchains = &g_sc,
                           .pImageIndices = &g_flip_idx};
    vk_queue_lock();
    VkResult r = vkQueuePresentKHR(vk_ctx()->gfx_queue, &pi);
    vk_queue_unlock();
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) InterlockedExchange(&g_dirty, 1);
}

static void free_capture(void) {
    VkCtx *c = vk_ctx();
    if (g_cap_buf && c) vmaDestroyBuffer(c->vma, g_cap_buf, g_cap_alloc);
    g_cap_buf = VK_NULL_HANDLE;
    g_cap_ptr = NULL;
    g_cap_size = 0;
    g_cap_valid = 0;
}

// Returns 1 with a usable swapchain, 0 when the window has no area (minimised). Called with g_pl held.
static int create_swapchain(void) {
    flip_locked();   // the acquired image goes out before its swapchain is replaced
    VkCtx *c = vk_ctx();
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->phys, g_surface, &caps) != VK_SUCCESS) return 0;
    int cww = 0, chh = 0;
    window_client_size(&cww, &chh);
    VkExtent2D ext = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
                                                            : (VkExtent2D){(uint32_t)cww, (uint32_t)chh};
    if (!ext.width || !ext.height) return 0;
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, g_surface, &nf, NULL);
    VkSurfaceFormatKHR fmts[64];
    if (nf > 64) nf = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, g_surface, &nf, fmts);
    if (!nf) return 0;
    // UNORM: the blit writes the bytes the game wrote (no sRGB encode).
    VkSurfaceFormatKHR sf = fmts[0];
    int found = 0;
    for (uint32_t i = 0; i < nf && !found; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = fmts[i]; found = 1; }
    for (uint32_t i = 0; i < nf && !found; i++)
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = fmts[i]; found = 1; }
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > 8) count = 8;

    vk_queue_lock();
    vkDeviceWaitIdle(c->device);
    VkSwapchainKHR old = g_sc;
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = g_surface, .minImageCount = count,
        .imageFormat = sf.format, .imageColorSpace = sf.colorSpace, .imageExtent = ext, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,   // every surface supports it (the menu overlay draws into it)
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = pick_present_mode(c->phys), .clipped = VK_TRUE,
        .oldSwapchain = old};
    VkResult r = vkCreateSwapchainKHR(c->device, &ci, NULL, &g_sc);
    if (r != VK_SUCCESS) {
        rt_log("gfx: vkCreateSwapchainKHR failed (%d)", (int)r);
        g_sc = old;
        vk_queue_unlock();
        return 0;
    }
    for (uint32_t i = 0; i < g_sc_n; i++) {
        vkDestroySemaphore(c->device, g_render_done[i], NULL);
        if (g_sc_view[i]) vkDestroyImageView(c->device, g_sc_view[i], NULL);
        g_sc_view[i] = VK_NULL_HANDLE;
    }
    if (old) vkDestroySwapchainKHR(c->device, old, NULL);
    g_sc_fmt = sf.format;
    g_sc_ext = ext;
    g_sc_n = MAX_IMAGES;
    vkGetSwapchainImagesKHR(c->device, g_sc, &g_sc_n, g_sc_img);
    VkSemaphoreCreateInfo semi = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < g_sc_n; i++) {
        vkCreateSemaphore(c->device, &semi, NULL, &g_render_done[i]);
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = g_sc_img[i],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = sf.format,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        if (vkCreateImageView(c->device, &vi, NULL, &g_sc_view[i]) != VK_SUCCESS) g_sc_view[i] = VK_NULL_HANDLE;
    }
    free_capture();
    vk_queue_unlock();
    InterlockedExchange(&g_dirty, 0);
    return 1;
}

static void sync_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 s0,
                         VkAccessFlags2 a0, VkPipelineStageFlags2 s1, VkAccessFlags2 a1) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = s0, .srcAccessMask = a0,
                               .dstStageMask = s1, .dstAccessMask = a1, .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

static int ensure_capture_buffer(void) {
    VkCtx *c = vk_ctx();
    VkDeviceSize need = (VkDeviceSize)g_sc_ext.width * g_sc_ext.height * 4;
    if (g_cap_buf && g_cap_size == need) return 1;
    free_capture();
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = need, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    VmaAllocationInfo info;
    if (vmaCreateBuffer(c->vma, &bi, &ai, &g_cap_buf, &g_cap_alloc, &info) != VK_SUCCESS) { g_cap_buf = VK_NULL_HANDLE; return 0; }
    g_cap_ptr = info.pMappedData;
    g_cap_size = need;
    return g_cap_ptr != NULL;
}

void vk_thumbs_enable(int n) {
    EnterCriticalSection(&g_pl);
    if (!g_th_n && n > 0 && (g_th_tag = calloc((size_t)n, sizeof *g_th_tag))) g_th_n = n;
    LeaveCriticalSection(&g_pl);
}

void vk_thumbs_frame(uint64_t frame) {
    EnterCriticalSection(&g_pl);
    g_th_frame = frame;
    LeaveCriticalSection(&g_pl);
}

static void free_thumbs(void) {
    VkCtx *c = vk_ctx();
    if (g_th_img && c) vmaDestroyImage(c->vma, g_th_img, g_th_img_alloc);
    if (g_th_buf && c) vmaDestroyBuffer(c->vma, g_th_buf, g_th_alloc);
    g_th_img = VK_NULL_HANDLE, g_th_buf = VK_NULL_HANDLE, g_th_ptr = NULL;
    if (g_th_tag) memset(g_th_tag, 0, sizeof *g_th_tag * (size_t)g_th_n);
    memset(g_th_pending, 0, sizeof g_th_pending);
}

// The ring and the blit target in the swapchain's format; allocated on the first present, the image again when the
// swapchain format changes. A failure turns the thumbnails off for the session.
static int ensure_thumbs(void) {
    if (!g_th_n || g_th_failed || !g_th_frame) return 0;
    VkCtx *c = vk_ctx();
    if (!g_th_buf) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = TH_BYTES * (VkDeviceSize)g_th_n,
                                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        if (vmaCreateBuffer(c->vma, &bi, &ai, &g_th_buf, &g_th_alloc, &info) != VK_SUCCESS || !info.pMappedData) {
            if (g_th_buf) vmaDestroyBuffer(c->vma, g_th_buf, g_th_alloc);
            g_th_buf = VK_NULL_HANDLE;
            g_th_failed = 1;
            rt_log("gfx: flight thumbnails off: no host-visible ring of %llu MB", (unsigned long long)(bi.size >> 20));
            return 0;
        }
        g_th_ptr = info.pMappedData;
    }
    if (g_th_img && g_th_fmt != g_sc_fmt) {
        vmaDestroyImage(c->vma, g_th_img, g_th_img_alloc);   // the previous present's fence was waited for: idle
        g_th_img = VK_NULL_HANDLE;
    }
    if (!g_th_img) {
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = g_sc_fmt,
                                .extent = {FLIGHT_THUMB_W, FLIGHT_THUMB_H, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateImage(c->vma, &ci, &ai, &g_th_img, &g_th_img_alloc, NULL) != VK_SUCCESS) {
            g_th_img = VK_NULL_HANDLE;
            g_th_failed = 1;
            rt_log("gfx: flight thumbnails off: no %ux%u image", FLIGHT_THUMB_W, FLIGHT_THUMB_H);
            return 0;
        }
        g_th_fmt = g_sc_fmt;
    }
    return 1;
}

// dst (TRANSFER_SRC, the swapchain image just written) -> thumbnail image -> ring slot of g_th_frame.
static void record_thumb(VkCommandBuffer cb, VkImage dst, unsigned fence_slot) {
    uint64_t f = g_th_frame;
    unsigned ring = (unsigned)(f % (uint64_t)g_th_n);
    g_th_tag[ring] = 0;   // under g_pl: vk_thumbs_read no longer hands this slot out
    sync_barrier(cb, g_th_img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageBlit region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {(int)g_sc_ext.width, (int)g_sc_ext.height, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {FLIGHT_THUMB_W, FLIGHT_THUMB_H, 1}}};
    vkCmdBlitImage(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_th_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_LINEAR);
    sync_barrier(cb, g_th_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy bc = {.bufferOffset = TH_BYTES * ring, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            .imageExtent = {FLIGHT_THUMB_W, FLIGHT_THUMB_H, 1}};
    vkCmdCopyImageToBuffer(cb, g_th_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_th_buf, 1, &bc);
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                           .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &d);
    g_th_pending[fence_slot] = f;
    g_th_frame = 0;
}

int vk_thumbs_read(uint64_t frame, uint8_t *rgba) {
    int ok = 0;
    EnterCriticalSection(&g_pl);
    if (g_th_n && g_th_ptr && frame) {
        unsigned ring = (unsigned)(frame % (uint64_t)g_th_n);
        if (g_th_tag[ring] == frame) {
            VkCtx *c = vk_ctx();
            vmaInvalidateAllocation(c->vma, g_th_alloc, TH_BYTES * ring, TH_BYTES);
            memcpy(rgba, g_th_ptr + TH_BYTES * ring, TH_BYTES);
            ok = g_th_fmt == VK_FORMAT_B8G8R8A8_UNORM || g_th_fmt == VK_FORMAT_B8G8R8A8_SRGB ? 2 : 1;
        }
    }
    LeaveCriticalSection(&g_pl);
    if (ok == 2)
        for (size_t i = 0; i < TH_BYTES; i += 4) {
            uint8_t t = rgba[i];
            rgba[i] = rgba[i + 2], rgba[i + 2] = t;
        }
    return ok != 0;
}

void vk_present_capture(int on) {
    EnterCriticalSection(&g_pl);
    g_capture = on;
    if (!on) g_cap_valid = 0;
    LeaveCriticalSection(&g_pl);
}

int vk_present_captured(const uint8_t **px, int *w, int *h, VkFormat *fmt) {
    EnterCriticalSection(&g_pl);
    int ok = g_cap_valid;
    if (ok) { *px = g_cap_ptr; *w = g_cap_w; *h = g_cap_h; *fmt = g_sc_fmt; }
    LeaveCriticalSection(&g_pl);
    return ok;
}

// The captured swapchain image as a PNG (the menu's test hook), RGBA with opaque alpha.
static void save_capture(const char *path) {
    size_t n = (size_t)g_cap_w * (size_t)g_cap_h * 4;
    uint8_t *px = malloc(n);
    if (!px) return;
    int bgra = g_sc_fmt == VK_FORMAT_B8G8R8A8_UNORM || g_sc_fmt == VK_FORMAT_B8G8R8A8_SRGB;
    for (size_t i = 0; i < n; i += 4) {
        px[i] = g_cap_ptr[i + (bgra ? 2 : 0)];
        px[i + 1] = g_cap_ptr[i + 1];
        px[i + 2] = g_cap_ptr[i + (bgra ? 0 : 2)];
        px[i + 3] = 255;
    }
    rt_write_png(path, px, (uint32_t)g_cap_w, (uint32_t)g_cap_h);
    free(px);
    rt_log("gfx: menu test shot %s (%dx%d)", path, g_cap_w, g_cap_h);
}

static void present_image(VkImage src, VkImageLayout layout, VkFormat fmt, int w, int h, const int crop[4], int defer) {
    int general = layout == VK_IMAGE_LAYOUT_GENERAL;
    gfx_window_start();
    VkCtx *c = vk_ctx();
    EnterCriticalSection(&g_pl);
    flip_locked();
    if (!g_sc || InterlockedCompareExchange(&g_dirty, 0, 0)) {
        if (!window_minimized() && !create_swapchain() && !g_sc) goto out;
    }
    if (window_minimized() || !g_sc) goto out;

    unsigned slot = g_frame % FRAMES;
    if (vkWaitForFences(c->device, 1, &g_fence[slot], VK_TRUE, 5000000000ull) != VK_SUCCESS) {
        rt_log("gfx: present fence wait timed out");
        goto out;
    }
    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(c->device, g_sc, 1000000000ull, g_acquire[slot], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        InterlockedExchange(&g_dirty, 1);
        if (!create_swapchain()) goto out;
        r = vkAcquireNextImageKHR(c->device, g_sc, 1000000000ull, g_acquire[slot], VK_NULL_HANDLE, &idx);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) goto out;     // lost, not ready: skip the frame
    if (r == VK_SUBOPTIMAL_KHR) InterlockedExchange(&g_dirty, 1);
    vkResetFences(c->device, 1, &g_fence[slot]);
    if (g_th_pending[slot]) {   // that fence has signalled: the thumbnail its submission wrote is complete
        g_th_tag[g_th_pending[slot] % (uint64_t)g_th_n] = g_th_pending[slot];
        g_th_pending[slot] = 0;
    }

    int cx = 0, cy = 0, cw = w, ch = h;
    if (crop && crop[2] > 0 && crop[3] > 0) { cx = crop[0]; cy = crop[1]; cw = crop[2]; ch = crop[3]; }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx + cw > w) cw = w - cx;
    if (cy + ch > h) ch = h - cy;
    window_settings_live();
    // The menu overlay: laid out before recording (its first frame starts the ImGui back end and uploads the font)
    const char *menu_shot = NULL;
    MenuTarget mt = {c->instance, c->phys, c->device, c->gfx_family, c->gfx_queue, g_sc_fmt, g_sc_ext.width,
                     g_sc_ext.height, cw, ch, g_scaler};
    int overlay = g_sc_view[idx] &&
                  menu_overlay_frame(__atomic_load_n(&gfx_counters.frames, __ATOMIC_RELAXED), &mt, &menu_shot);
    int cap = (g_capture || menu_shot) && ensure_capture_buffer();
    int thumb = g_th_n && ensure_thumbs();

    VkCommandBuffer cb = g_cb[slot];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cb, &bi);
    VkImage dst = g_sc_img[idx];
    sync_barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const int rect[4] = {cx, cy, cw, ch};
    // FSR (TOTK_UPSCALER) when the swapchain is larger than the crop; else the bilinear blit
    int fsr = general && vk_fsr_record(cb, slot, src, fmt, w, h, rect, dst, g_sc_ext.width, g_sc_ext.height);
    g_scaler = fsr ? 2 : (uint32_t)cw != g_sc_ext.width || (uint32_t)ch != g_sc_ext.height;
    if (!fsr) {
        if (general)   // a GENERAL source (vk_texture.c images): earlier submissions' writes become visible to the blit here
            sync_barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkImageBlit region = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{cx, cy, 0}, {cx + cw, cy + ch, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {(int)g_sc_ext.width, (int)g_sc_ext.height, 1}}};
        vkCmdBlitImage(cb, src, general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
        if (general)   // later submissions that write the source wait for the blit's read
            sync_barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }
    // last: the image's layout, and the stage and access of its last write, as each step leaves it
    VkImageLayout last = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkPipelineStageFlags2 last_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    VkAccessFlags2 last_write = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (overlay) {
        sync_barrier(cb, dst, last, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, last_stage, last_write,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = g_sc_view[idx],
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, g_sc_ext}, .layerCount = 1,
                              .colorAttachmentCount = 1, .pColorAttachments = &ca};
        vkCmdBeginRendering(cb, &ri);
        menu_overlay_draw(cb);
        vkCmdEndRendering(cb);
        last = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        last_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        last_write = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (cap) {
        sync_barrier(cb, dst, last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, last_stage, last_write,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy bc = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {g_sc_ext.width, g_sc_ext.height, 1}};
        vkCmdCopyImageToBuffer(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_cap_buf, 1, &bc);
        last = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        last_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        last_write = 0;
    }
    if (thumb) {
        if (last != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            sync_barrier(cb, dst, last, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, last_stage, last_write,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        last = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        last_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        last_write = 0;
        record_thumb(cb, dst, slot);
    }
    sync_barrier(cb, dst, last, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, last_stage, last_write, VK_PIPELINE_STAGE_2_NONE, 0);
    vkEndCommandBuffer(cb);

    VkSemaphoreSubmitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = g_acquire[slot],
                                  .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT};
    VkSemaphoreSubmitInfo sig = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = g_render_done[idx],
                                 .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkCommandBufferSubmitInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb};
    VkSubmitInfo2 si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait,
                        .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbi, .signalSemaphoreInfoCount = 1,
                        .pSignalSemaphoreInfos = &sig};
    vk_queue_lock();
    r = vkQueueSubmit2(c->gfx_queue, 1, &si, g_fence[slot]);
    vk_queue_unlock();
    if (r == VK_SUCCESS) {
        g_flip_on = 1;
        g_flip_idx = idx;
        if (!defer) flip_locked();
    } else {
        rt_log("gfx: present submit failed (%d)", (int)r);
    }
    g_frame++;
    if (cap) {
        vkWaitForFences(c->device, 1, &g_fence[slot], VK_TRUE, 5000000000ull);
        vmaInvalidateAllocation(c->vma, g_cap_alloc, 0, VK_WHOLE_SIZE);
        g_cap_w = (int)g_sc_ext.width;
        g_cap_h = (int)g_sc_ext.height;
        g_cap_valid = 1;
        if (menu_shot) save_capture(menu_shot);
    }
out:
    LeaveCriticalSection(&g_pl);
}

void vk_present_image(VkImage src, VkImageLayout layout, VkFormat fmt, int w, int h, const int crop[4]) {
    present_image(src, layout, fmt, w, h, crop, 0);
}

void vk_present_blit(VkImage src, VkImageLayout layout, VkFormat fmt, int w, int h, const int crop[4]) {
    present_image(src, layout, fmt, w, h, crop, 1);
}

void vk_present_flip(void) {
    EnterCriticalSection(&g_pl);
    flip_locked();
    LeaveCriticalSection(&g_pl);
}

void gfx_window_resize(int cw, int chh) {
    gfx_window_start();
    if (g_window) SDL_SetWindowSize(g_window, cw, chh);
    EnterCriticalSection(&g_pl);
    create_swapchain();
    LeaveCriticalSection(&g_pl);
}

// Called by vk_shutdown before the device goes away; the window and its thread stay.
void gfx_window_release_vk(void) {
    VkCtx *c = vk_ctx();
    EnterCriticalSection(&g_pl);
    if (c && g_started) {
        vk_queue_lock();
        vkDeviceWaitIdle(c->device);
        vk_queue_unlock();
        g_flip_on = 0;   // its blit is done (device idle); the swapchain goes away unpresented
        menu_vk_release();
        vk_fsr_release();
        free_capture();
        free_thumbs();
        for (int i = 0; i < FRAMES; i++) {
            vkDestroySemaphore(c->device, g_acquire[i], NULL);
            vkDestroyFence(c->device, g_fence[i], NULL);
        }
        for (uint32_t i = 0; i < g_sc_n; i++) {
            vkDestroySemaphore(c->device, g_render_done[i], NULL);
            if (g_sc_view[i]) vkDestroyImageView(c->device, g_sc_view[i], NULL);
            g_sc_view[i] = VK_NULL_HANDLE;
        }
        if (g_pool) vkDestroyCommandPool(c->device, g_pool, NULL);
        if (g_sc) vkDestroySwapchainKHR(c->device, g_sc, NULL);
        if (g_surface) vkDestroySurfaceKHR(c->instance, g_surface, NULL);
        g_sc = VK_NULL_HANDLE; g_surface = VK_NULL_HANDLE; g_pool = VK_NULL_HANDLE; g_sc_n = 0;
        g_started = 0;     // the window and its thread stay; the next gfx_window_start makes a new surface
    }
    LeaveCriticalSection(&g_pl);
}

void gfx_window_start(void) {
    EnterCriticalSection(&g_start_lock);
    if (!g_started) {
        if (!vk_init()) rt_fatal("gfx: no usable Vulkan device (see the vk: lines above)");
        VkCtx *c = vk_ctx();
        if (!g_window) {
            g_wnd_failed = 0;
            g_wnd_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
            HANDLE t = CreateThread(NULL, 0, window_main, NULL, 0, NULL);
            if (!t) rt_fatal("gfx: cannot start the window thread (error %lu)", GetLastError());
            CloseHandle(t);
            WaitForSingleObject(g_wnd_ready, INFINITE);
            CloseHandle(g_wnd_ready);
            if (g_wnd_failed) rt_fatal("gfx: window creation failed");
        }
        if (!SDL_Vulkan_CreateSurface(g_window, c->instance, NULL, &g_surface))
            rt_fatal("gfx: SDL_Vulkan_CreateSurface failed (%s)", SDL_GetError());

        VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = c->gfx_family};
        vkCreateCommandPool(c->device, &pci, NULL, &g_pool);
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g_pool,
                                          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = FRAMES};
        vkAllocateCommandBuffers(c->device, &ai, g_cb);
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        VkSemaphoreCreateInfo semi = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (int i = 0; i < FRAMES; i++) {
            vkCreateFence(c->device, &fi, NULL, &g_fence[i]);
            vkCreateSemaphore(c->device, &semi, NULL, &g_acquire[i]);
        }
        g_frame = 0;
        g_started = 1;
        EnterCriticalSection(&g_pl);
        create_swapchain();
        LeaveCriticalSection(&g_pl);
        int log_w = 0, log_h = 0;
        window_client_size(&log_w, &log_h);
        rt_log("gfx: window %dx%d, swapchain %ux%u format %d", log_w, log_h, g_sc_ext.width, g_sc_ext.height, (int)g_sc_fmt);
    }
    LeaveCriticalSection(&g_start_lock);
    vsync_ensure();
}
