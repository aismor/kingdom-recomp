// Linux implementation of platform.h (POSIX + os.h shims where convenient).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <limits.h>
#include <linux/futex.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>

#include "../os.h"
#include "platform.h"

_Static_assert(sizeof(PlatMutex) == sizeof(SRWLOCK), "PlatMutex holds an SRWLOCK");
_Static_assert(sizeof(PlatCond) == sizeof(CONDITION_VARIABLE), "PlatCond holds a CONDITION_VARIABLE");

struct PlatThread {
    pthread_t tid;
    PlatThreadFn fn;
    void *arg;
};

static void *thread_main(void *p) {
    PlatThread *t = p;
    t->fn(t->arg);
    return NULL;
}

PlatThread *plat_thread_new(PlatThreadFn fn, void *arg, const char *name) {
    PlatThread *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->tid, NULL, thread_main, t)) {
        free(t);
        return NULL;
    }
    if (name) pthread_setname_np(t->tid, name);
    return t;
}

void plat_thread_join(PlatThread *t) {
    pthread_join(t->tid, NULL);
    free(t);
}

void plat_thread_detach(PlatThread *t) {
    pthread_detach(t->tid);
    free(t);
}

uint64_t plat_thread_affinity(PlatThread *t, uint64_t mask) {
    cpu_set_t set, prev;
    CPU_ZERO(&set);
    for (int i = 0; i < 64; i++)
        if (mask & (1ull << i)) CPU_SET(i, &set);
    pthread_t who = t ? t->tid : pthread_self();
    if (pthread_getaffinity_np(who, sizeof prev, &prev)) return 0;
    pthread_setaffinity_np(who, sizeof set, &set);
    uint64_t old = 0;
    for (int i = 0; i < CPU_SETSIZE; i++)
        if (CPU_ISSET(i, &prev)) old |= 1ull << i;
    return old;
}

uint32_t plat_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
}

static PlatScale qpc_ns;
__attribute__((constructor)) static void qpc_ns_init(void) { qpc_ns = plat_scale(1000000000u, 1000000000u); }

uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define PLAT_SPIN_NS 300000u
void plat_sleep_ns(uint64_t ns) {
    uint64_t end = plat_now_ns() + ns;
    if (ns > PLAT_SPIN_NS) {
        struct timespec ts = {.tv_sec = (time_t)((ns - PLAT_SPIN_NS) / 1000000000ull),
                              .tv_nsec = (long)((ns - PLAT_SPIN_NS) % 1000000000ull)};
        nanosleep(&ts, NULL);
    }
    while (plat_now_ns() < end) sched_yield();
}

// Futex mutex and condition variable in the pointer-sized PlatMutex / PlatCond, zero-initialised like SRWLOCK and
// CONDITION_VARIABLE (os.h maps those onto these). Mutex word: 0 free, 1 locked, 2 locked with waiters (Drepper,
// "Futexes Are Tricky", mutex 2). Condition word: a sequence number bumped by every signal.
static uint32_t *futex_word(void *p) { return (uint32_t *)p; }

static void futex_wait(uint32_t *w, uint32_t val, const struct timespec *rel) {
    syscall(SYS_futex, w, FUTEX_WAIT_PRIVATE, val, rel, NULL, 0);
}

static void futex_wake(uint32_t *w, int n) { syscall(SYS_futex, w, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0); }

void plat_mutex_init(PlatMutex *m) { m->p = NULL; }

void plat_mutex_lock(PlatMutex *m) {
    uint32_t *w = futex_word(m), c = 0;
    if (__atomic_compare_exchange_n(w, &c, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    if (c != 2) c = __atomic_exchange_n(w, 2, __ATOMIC_ACQUIRE);
    while (c != 0) {
        futex_wait(w, 2, NULL);
        c = __atomic_exchange_n(w, 2, __ATOMIC_ACQUIRE);
    }
}

int plat_mutex_trylock(PlatMutex *m) {
    uint32_t c = 0;
    return __atomic_compare_exchange_n(futex_word(m), &c, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void plat_mutex_unlock(PlatMutex *m) {
    uint32_t *w = futex_word(m);
    if (__atomic_exchange_n(w, 0, __ATOMIC_RELEASE) == 2) futex_wake(w, 1);
}

void plat_cond_init(PlatCond *c) { c->p = NULL; }

static void cond_wait_rel(PlatCond *c, PlatMutex *m, const struct timespec *rel) {
    uint32_t *w = futex_word(c);
    uint32_t seq = __atomic_load_n(w, __ATOMIC_RELAXED);
    plat_mutex_unlock(m);
    futex_wait(w, seq, rel);
    plat_mutex_lock(m);
}

void plat_cond_wait(PlatCond *c, PlatMutex *m) { cond_wait_rel(c, m, NULL); }

int plat_cond_wait_ns(PlatCond *c, PlatMutex *m, uint64_t ns) {
    uint64_t end = plat_now_ns() + ns;
    struct timespec rel = {.tv_sec = (time_t)(ns / 1000000000ull), .tv_nsec = (long)(ns % 1000000000ull)};
    cond_wait_rel(c, m, &rel);
    return plat_now_ns() < end;
}

void plat_cond_signal(PlatCond *c) {
    __atomic_fetch_add(futex_word(c), 1, __ATOMIC_RELEASE);
    futex_wake(futex_word(c), 1);
}

void plat_cond_broadcast(PlatCond *c) {
    __atomic_fetch_add(futex_word(c), 1, __ATOMIC_RELEASE);
    futex_wake(futex_word(c), INT_MAX);
}

int plat_mem_commit(uint64_t addr, size_t n) {
    return VirtualAlloc((void *)(uintptr_t)addr, n, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

int plat_mem_read(uint64_t addr, void *out, size_t n) { return ReadProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)addr, out, n, NULL); }

static int widen(const char *s, wchar_t *w, int cap) {
    if (!s) { w[0] = 0; return 0; }
    size_t n = mbstowcs(w, s, cap > 0 ? (size_t)cap - 1 : 0);
    return n == (size_t)-1 ? 0 : (int)n + 1;
}

#define WPATH 2048

int plat_file_exists(const char *path) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return 0;
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES && !(GetFileAttributesW(w) & FILE_ATTRIBUTE_DIRECTORY);
}

void plat_file_mkdir_p(const char *path) {
    char tmp[4096];
    os_host_path(path, tmp, sizeof tmp);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    mkdir(tmp, 0755);
}

uint8_t *plat_file_read(const char *path, uint64_t *size) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return NULL;
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    uint8_t *buf = NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (1ll << 30)) {
        buf = malloc((size_t)sz.QuadPart + 1);
        DWORD got = 0;
        if (buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart) {
            buf[sz.QuadPart] = 0;
            *size = (uint64_t)sz.QuadPart;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    CloseHandle(h);
    return buf;
}

int plat_file_write(const char *path, const void *data, size_t n) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return 0;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD put = 0;
    int ok = WriteFile(h, data, (DWORD)n, &put, NULL) && put == n;
    CloseHandle(h);
    return ok;
}

PlatAppend *plat_append_open(const char *path) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return NULL;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (PlatAppend *)h;
}

int plat_append_write(PlatAppend *f, const void *data, size_t n) {
    DWORD put = 0;
    return WriteFile((HANDLE)f, data, (DWORD)n, &put, NULL) && put == n;
}

int plat_file_replace(const char *from, const char *to) {
    wchar_t a[WPATH], b[WPATH];
    if (!widen(from, a, WPATH) || !widen(to, b, WPATH)) return 0;
    return MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
}

int plat_file_write_atomic(const char *path, const void *data, size_t n) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.%lu.%lu.tmp", path, (unsigned long)getpid(), (unsigned long)plat_thread_id());
    if (!plat_file_write(tmp, data, n)) return 0;
    if (!plat_file_replace(tmp, path)) {
        plat_file_delete(tmp);
        return 0;
    }
    return 1;
}

int plat_file_flush(const char *path) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return 0;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

void plat_file_delete(const char *path) {
    wchar_t w[WPATH];
    if (widen(path, w, WPATH)) DeleteFileW(w);
}

int plat_file_truncate(const char *path, uint64_t size) {
    wchar_t w[WPATH];
    if (!widen(path, w, WPATH)) return 0;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER at = {.QuadPart = (int64_t)size};
    int ok = SetFilePointerEx(h, at, NULL, FILE_BEGIN) && SetEndOfFile(h);
    CloseHandle(h);
    return ok;
}

int64_t plat_file_size(const char *path) {
    wchar_t w[WPATH];
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!widen(path, w, WPATH) || !GetFileAttributesExW(w, 0, &d)) return -1;
    return (int64_t)(((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow);
}

// The Windows read-only attribute: on clears every write bit, off gives the owner write access back.
int plat_file_set_readonly(const char *path_in, int on) {
    char path[4096];
    struct stat st;
    os_host_path(path_in, path, sizeof path);
    if (stat(path, &st)) return 0;
    mode_t m = on ? st.st_mode & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH) : st.st_mode | S_IWUSR;
    return chmod(path, m & 07777) == 0;
}

int plat_file_list(const char *dir, const char *ext, int (*cb)(const char *name, void *ctx), void *ctx) {
    char pat[4096];
    snprintf(pat, sizeof pat, "%s/*%s", dir, ext);
    wchar_t wpat[4096];
    MultiByteToWideChar(CP_UTF8, 0, pat, -1, wpat, 4096);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char name[512];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL);
        if (!cb(name, ctx)) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

int plat_exe_path(char *out, size_t cap) {
    return GetModuleFileNameA(NULL, out, (DWORD)cap) != 0;
}

size_t plat_getenv(const char *name, char *out, size_t cap) {
    const char *v = getenv(name);
    if (!v || !*v || cap == 0) return 0;
    size_t n = strlen(v);
    if (n >= cap) return 0;
    memcpy(out, v, n + 1);
    return n;
}

const char *plat_ui_language(void) {
    const char *l = setlocale(LC_MESSAGES, NULL);
    return l ? l : "";
}

uint32_t plat_thread_id(void) { return GetCurrentThreadId(); }

int plat_font_path(const char *lang, char *out, size_t cap) {
    (void)lang;
    const char *candidates[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (plat_file_exists(candidates[i])) return snprintf(out, cap, "%s", candidates[i]) < (int)cap;
    return 0;
}

int plat_os_version(char *out, size_t cap) {
    struct utsname u;
    if (uname(&u)) return 0;
    snprintf(out, cap, "%s %s", u.sysname, u.release);
    return 1;
}

int plat_stderr_path(char *out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
}

int plat_clipboard_set(const char *utf8) {
    (void)utf8;
    return 0;
}

int plat_open_folder(const char *path) {
    char host[4096];
    os_host_path(path, host, sizeof host);
    char cmd[4200];
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", host);
    return system(cmd) == 0;
}

void *plat_lib_open(const char *name) { return LoadLibraryA(name); }
void *plat_lib_sym(void *lib, const char *name) { return GetProcAddress((HMODULE)lib, name); }

// CPU times in clock ticks (USER_HZ), as /proc/stat and /proc/self/stat give them.
struct PlatLoad { uint64_t busy, all, proc; };

PlatLoad *plat_load_open(const uint8_t *gpu_luid) {
    (void)gpu_luid;
    return calloc(1, sizeof(PlatLoad));
}

void plat_load_close(PlatLoad *l) { free(l); }

// The first line of /proc/stat: the machine's busy and total ticks (idle and iowait count as idle).
static int machine_ticks(uint64_t *busy, uint64_t *all) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    unsigned long long v[8] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (n < 4) return 0;
    *all = 0;
    for (int i = 0; i < 8; i++) *all += v[i];
    *busy = *all - v[3] - v[4];
    return 1;
}

// utime + stime of /proc/self/stat (fields 14 and 15, after the parenthesised command name).
static int process_ticks(uint64_t *t) {
    char buf[1024];
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *p = strrchr(buf, ')');
    unsigned long long ut, st;
    if (!p || sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) != 2) return 0;
    *t = ut + st;
    return 1;
}

// The CPU package sensor (k10temp, zenpower, coretemp), else the hottest ACPI zone as on Windows; -1 without either.
static float cpu_temp(void) {
    float pkg = -1, acpi = -1;
    for (int i = 0; i < 64; i++) {
        char path[96], name[32] = {0};
        snprintf(path, sizeof path, "/sys/class/hwmon/hwmon%d/name", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int got = fscanf(f, "%31s", name);
        fclose(f);
        if (got != 1) continue;
        int is_pkg = !strcmp(name, "k10temp") || !strcmp(name, "zenpower") || !strcmp(name, "coretemp");
        if (!is_pkg && strcmp(name, "acpitz")) continue;
        snprintf(path, sizeof path, "/sys/class/hwmon/hwmon%d/temp1_input", i);
        long mc;
        if (!(f = fopen(path, "r"))) continue;
        got = fscanf(f, "%ld", &mc);
        fclose(f);
        float c = (float)mc / 1000.0f;
        if (got != 1 || c <= 0 || c >= 150) continue;
        if (is_pkg && c > pkg) pkg = c;
        if (!is_pkg && c > acpi) acpi = c;
    }
    return pkg >= 0 ? pkg : acpi;
}

void plat_cpu_load(PlatLoad *l, float *total_pct, float *proc_pct, float *temp_c) {
    *total_pct = *proc_pct = -1;
    *temp_c = cpu_temp();
    if (!l) return;
    uint64_t busy, all, proc;
    if (!machine_ticks(&busy, &all) || !process_ticks(&proc)) return;
    if (l->all && all > l->all) {
        float d = (float)(all - l->all);
        *total_pct = 100.0f * (float)(busy - l->busy) / d;
        *proc_pct = 100.0f * (float)(proc - l->proc) / d;
    }
    l->busy = busy, l->all = all, l->proc = proc;
}

void plat_gpu_load(PlatLoad *l, float *load_pct, float *vram_used_mb) {
    (void)l;
    *load_pct = *vram_used_mb = -1;
}

// In use: MemTotal - MemAvailable (what Task Manager counts as in use); the process: its resident set (VmRSS), the
// working set's counterpart.
void plat_mem_info(float *used_mb, float *total_mb, float *proc_mb) {
    *used_mb = *total_mb = *proc_mb = -1;
    char line[256];
    unsigned long long total = 0, avail = 0, rss = 0, v;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "MemTotal: %llu kB", &v) == 1) total = v;
            else if (sscanf(line, "MemAvailable: %llu kB", &v) == 1) avail = v;
        }
        fclose(f);
    }
    if (total) *total_mb = (float)total / 1024.0f, *used_mb = (float)(total - (avail < total ? avail : total)) / 1024.0f;
    if ((f = fopen("/proc/self/status", "r"))) {
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "VmRSS: %llu kB", &v) == 1) rss = v;
        fclose(f);
    }
    if (rss) *proc_mb = (float)rss / 1024.0f;
}

struct PlatChild {
    pid_t pid;
    int in_w, out_r;
};

PlatChild *plat_child_start(const char *exe_in, const char *arg, uint32_t *err) {
    (void)err;
    char host_exe[4096];
    os_host_path(exe_in, host_exe, sizeof host_exe);
    const char *exe = host_exe;
    /* Close-on-exec, as win32.c keeps the parent's ends uninheritable: a later child holding an earlier child's stdin
       would keep it from seeing EOF when we exit. dup2 clears the flag on the child's stdin and stdout. */
    int inpipe[2], outpipe[2];
    if (pipe2(inpipe, O_CLOEXEC)) return NULL;
    if (pipe2(outpipe, O_CLOEXEC)) return close(inpipe[0]), close(inpipe[1]), NULL;
    pid_t pid = fork();
    if (pid < 0) return NULL;
    if (pid == 0) {
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]);
        close(inpipe[1]);
        close(outpipe[0]);
        close(outpipe[1]);
        if (arg) {
            execl(exe, exe, arg, (char *)NULL);
        } else execl(exe, exe, (char *)NULL);
        _exit(127);
    }
    close(inpipe[0]);
    close(outpipe[1]);
    PlatChild *c = malloc(sizeof *c);
    c->pid = pid;
    c->in_w = inpipe[1];
    c->out_r = outpipe[0];
    return c;
}

void plat_child_stop(PlatChild *c) {
    if (!c) return;
    kill(c->pid, SIGTERM);
    waitpid(c->pid, NULL, 0);
    close(c->in_w);
    close(c->out_r);
    free(c);
}

int plat_child_write(PlatChild *c, const void *data, size_t n) {
    return write(c->in_w, data, n) == (ssize_t)n;
}

long plat_child_read(PlatChild *c, void *buf, size_t cap) { return read(c->out_r, buf, cap); }
