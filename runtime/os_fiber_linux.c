// Host fiber API (Win32 fibers) for kernel.c on Linux, via ucontext.
#define _GNU_SOURCE
#include "os.h"

#ifndef _WIN32

#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

typedef struct HostFiber {
    ucontext_t ctx;
    char *stack;
    size_t stack_size;
    LPFIBER_START_ROUTINE start;
    void *param;
    int started;
} HostFiber;

static _Thread_local HostFiber *tls_current;
static _Thread_local ucontext_t tls_thread_ctx;
static _Thread_local int tls_thread_saved;

static void fiber_trampoline(void) {
    HostFiber *f = tls_current;
    if (f && f->start) f->start(f->param);
}

void *ConvertThreadToFiberEx(void *param, DWORD flags) {
    (void)param;
    (void)flags;
    if (!tls_thread_saved) {
        getcontext(&tls_thread_ctx);
        tls_thread_saved = 1;
    }
    HostFiber *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->ctx = tls_thread_ctx;
    tls_current = f;
    return f;
}

void *CreateFiberEx(SIZE_T stackCommit, SIZE_T stackReserve, DWORD flags, LPFIBER_START_ROUTINE start, void *param) {
    (void)flags;
    size_t n = stackReserve ? (size_t)stackReserve : (size_t)stackCommit;
    if (n < 0x10000) n = 0x10000;
    HostFiber *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->stack = malloc(n);
    if (!f->stack) {
        free(f);
        return NULL;
    }
    f->stack_size = n;
    f->start = start;
    f->param = param;
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = f->stack_size;
    f->ctx.uc_link = tls_thread_saved ? &tls_thread_ctx : NULL;
    makecontext(&f->ctx, fiber_trampoline, 0);
    return f;
}

void SwitchToFiber(void *fiber) {
    HostFiber *to = fiber;
    HostFiber *from = tls_current;
    if (!to) return;
    if (!from) {
        getcontext(&tls_thread_ctx);
        tls_thread_saved = 1;
        tls_current = to;
        swapcontext(&tls_thread_ctx, &to->ctx);
        return;
    }
    tls_current = to;
    swapcontext(&from->ctx, &to->ctx);
    tls_current = from;
}

void DeleteFiber(void *fiber) {
    HostFiber *f = fiber;
    if (!f) return;
    free(f->stack);
    free(f);
}

#endif
