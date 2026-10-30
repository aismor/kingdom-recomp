// Linux stubs for TOTK_PROFILE / perf (prof_win.c is Windows-only).
#include "os.h"
#include "host.h"
#include "perfmon.h"
#include "platform/platform.h"

static int overlay;

void prof_start(void) {}
void prof_write(void) {}
void gfx_exit_log(void) {}

void perf_set_overlay(int mode) { overlay = mode; }

int perf_gpu_timing(void) { return (rt_trace & RT_TRACE_PERF) || overlay == 2; }

static volatile LONG64 gpu_busy_total;

void perf_gpu_busy(uint64_t ns) { __sync_fetch_and_add(&gpu_busy_total, (LONG64)ns); }

uint64_t perf_gpu_busy_total(void) { return (uint64_t)gpu_busy_total; }

uint64_t perf_begin(void) { return plat_now_ns(); }

void perf_replay_thread(void) {}
void perf_main_thread(void) {}

void perf_end(int kind, uint64_t t0) { (void)kind; (void)t0; }

void (*perf_gpu_stats)(PerfGpu *out);

void perf_gpu_wait(uint64_t t0) { (void)t0; }

void perf_replay(uint64_t t0) { (void)t0; }

void perf_count(int kind) { (void)kind; }

void perf_summary_flush(void) {}

void perf_present(uint64_t frame) { (void)frame; }

int rt_ab(void) { return 0; }

int rt_knob(const char *name, const char *def) {
    (void)name;
    (void)def;
    return 0;
}
