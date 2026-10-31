// Linux side of prof_win.c: no sampling profiler (TOTK_PROFILE), no perf log or flight recorder; the frame times, the
// render thread's waits, the replay time and the event counts feed the performance overlay (perfmon_frame), as
// prof_win.c perf_present does.
#include "os.h"
#include "host.h"
#include "perfmon.h"
#include "platform/platform.h"

static int perf_on;   // the full overlay: event counts are taken
static int overlay;   // performance overlay mode (perf_set_overlay): 0 off, 1 fps, 2 full
static DWORD render_tid, replay_tid;

void prof_start(void) {}
void prof_write(void) {}
__attribute__((weak)) void gfx_exit_log(void) {}

void perf_set_overlay(int mode) {
    __atomic_store_n(&overlay, mode, __ATOMIC_RELAXED);
    __atomic_store_n(&perf_on, mode == 2, __ATOMIC_RELAXED);
}

int perf_gpu_timing(void) { return (rt_trace & RT_TRACE_PERF) || __atomic_load_n(&overlay, __ATOMIC_RELAXED) == 2; }

static volatile LONG64 gpu_busy_total;

void perf_gpu_busy(uint64_t ns) { __atomic_fetch_add(&gpu_busy_total, (LONG64)ns, __ATOMIC_RELAXED); }

uint64_t perf_gpu_busy_total(void) { return (uint64_t)__atomic_load_n(&gpu_busy_total, __ATOMIC_RELAXED); }

static uint64_t frame_ns[PERF_N];   // the render (or replay) thread's waits since the previous present

uint64_t perf_begin(void) { return kern_now_ns(); }

void perf_replay_thread(void) { replay_tid = GetCurrentThreadId(); }
void perf_main_thread(void) {}

void perf_end(int kind, uint64_t t0) {
    if (!t0) return;
    DWORD tid = GetCurrentThreadId();
    if (tid != render_tid && tid != replay_tid) return;
    uint64_t d = kern_now_ns() - t0;
    if (tid == render_tid && !replay_tid) {   // one thread adds: no atomics on the synchronous path
        frame_ns[kind] += d;
        return;
    }
    __atomic_fetch_add(&frame_ns[kind], d, __ATOMIC_RELAXED);
}

void (*perf_gpu_stats)(PerfGpu *out);

void perf_gpu_wait(uint64_t t0) { (void)t0; }

static volatile LONG64 frame_replay_ns;   // perf_replay since the previous present

void perf_replay(uint64_t t0) {
    if (!t0) return;
    __atomic_fetch_add(&frame_replay_ns, (LONG64)(kern_now_ns() - t0), __ATOMIC_RELAXED);
}

static volatile LONG64 counts[PERF_CNT_N];

void perf_count(int kind) {
    if (__atomic_load_n(&perf_on, __ATOMIC_RELAXED)) __atomic_fetch_add(&counts[kind], 1, __ATOMIC_RELAXED);
}

void perf_summary_flush(void) {}

void perf_present(uint64_t frame) {
    (void)frame;
    static uint64_t last;
    render_tid = GetCurrentThreadId();
    uint64_t now = kern_now_ns(), d = last ? now - last : 0;
    uint64_t rep = (uint64_t)__atomic_exchange_n(&frame_replay_ns, 0, __ATOMIC_RELAXED);
    if (__atomic_load_n(&overlay, __ATOMIC_RELAXED))
        perfmon_frame(now, d, frame_ns, rep, __atomic_load_n(&counts[PERF_CNT_TEX_UP], __ATOMIC_RELAXED));
    memset(frame_ns, 0, sizeof frame_ns);
    last = now;
}

int rt_ab(void) { return 0; }

int rt_knob(const char *name, const char *def) {
    (void)name;
    (void)def;
    return 0;
}
