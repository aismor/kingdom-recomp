// Linux implementations of the Win32-shaped API declared in os.h.
#define _GNU_SOURCE
#include "os.h"

#ifndef _WIN32

#include <sched.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <locale.h>
#include <poll.h>
#include <wchar.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <ucontext.h>


#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define FILE_BEGIN 0
#define GetFileExInfoStandard 0
#define QDC_ONLY_ACTIVE_PATHS 0

typedef struct Region {
    uint64_t base, size;
    int committed;
    int write_watch;
    struct Region *next;
} Region;

static Region *regions;
static pthread_mutex_t region_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local DWORD last_error;

static struct {
    int installed;
    PVECTORED_EXCEPTION_HANDLER handler;
} g_vectored;

DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD e) { last_error = e; }

uint64_t GetTickCount64(void) { return plat_now_ns() / 1000000ull; }
void Sleep(DWORD ms) { plat_sleep_ns((uint64_t)ms * 1000000); }
BOOL SwitchToThread(void) { sched_yield(); return TRUE; }
void ExitProcess(UINT code) { _exit((int)code); }
HANDLE GetCurrentProcess(void) { return (HANDLE)(intptr_t)getpid(); }

static Region *find_region(uint64_t a) {
    for (Region *r = regions; r; r = r->next)
        if (a >= r->base && a < r->base + r->size) return r;
    return NULL;
}

static void add_region(uint64_t base, uint64_t size, int ww) {
    Region *r = calloc(1, sizeof *r);
    r->base = base;
    r->size = size;
    r->write_watch = ww;
    pthread_mutex_lock(&region_lock);
    r->next = regions;
    regions = r;
    pthread_mutex_unlock(&region_lock);
}

static int clear_soft_dirty(void) {
    int fd = open("/proc/self/clear_refs", O_WRONLY);
    if (fd < 0) return 0;
    char c = '4';
    int ok = write(fd, &c, 1) == 1;
    close(fd);
    return ok;
}

static int page_soft_dirty(uint64_t page) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) return 0;
    uint64_t entry = 0;
    off_t off = (off_t)(page / 4096) * (off_t)sizeof entry;
    if (pread(fd, &entry, sizeof entry, off) != (ssize_t)sizeof entry) {
        close(fd);
        return 0;
    }
    close(fd);
    return entry & (1ull << 55);
}

void *VirtualAlloc(void *addr, SIZE_T size, DWORD type, DWORD prot) {
    uint64_t at = (uint64_t)(uintptr_t)addr;
    int ww = (type & MEM_WRITE_WATCH) != 0;
    type &= ~MEM_WRITE_WATCH;
    if (type == MEM_RELEASE) return VirtualFree(addr, size, MEM_RELEASE), NULL;
    int fixed = at != 0;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (fixed) flags |= MAP_FIXED;
    if (type == MEM_RESERVE) flags |= MAP_NORESERVE;
    int pflags = PROT_NONE;
    if (prot == PAGE_READWRITE) pflags = PROT_READ | PROT_WRITE;
    else if (prot == PAGE_READONLY) pflags = PROT_READ;
    if (type == MEM_COMMIT && !fixed) {
        errno = EINVAL;
        return NULL;
    }
    if (type == MEM_COMMIT) {
        Region *r = find_region(at);
        if (!r) {
            void *p = mmap((void *)(uintptr_t)at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                           -1, 0);
            if (p == MAP_FAILED) return NULL;
            add_region(at, size, 0);
            return p;
        }
        if (mprotect((void *)(uintptr_t)at, size, PROT_READ | PROT_WRITE)) return NULL;
        r->committed = 1;
        return (void *)(uintptr_t)at;
    }
    void *p = mmap((void *)(uintptr_t)at, size, pflags, flags, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (fixed && p != (void *)(uintptr_t)at) {
        munmap(p, size);
        return NULL;
    }
    add_region((uint64_t)(uintptr_t)p, size, ww);
    if (ww) clear_soft_dirty();
    return p;
}

BOOL VirtualFree(void *addr, SIZE_T size, DWORD op) {
    (void)size;
    uint64_t at = (uint64_t)(uintptr_t)addr;
    if (op == MEM_RELEASE) {
        Region **pp = &regions, *r;
        pthread_mutex_lock(&region_lock);
        while ((r = *pp)) {
            if (r->base == at) {
                *pp = r->next;
                munmap((void *)(uintptr_t)at, r->size);
                free(r);
                pthread_mutex_unlock(&region_lock);
                return TRUE;
            }
            pp = &r->next;
        }
        pthread_mutex_unlock(&region_lock);
        return FALSE;
    }
    if (op == MEM_DECOMMIT) {
        if (mprotect(addr, size, PROT_NONE)) return FALSE;
        Region *r = find_region(at);
        if (r) r->committed = 0;
        return TRUE;
    }
    return FALSE;
}

BOOL VirtualProtect(void *addr, SIZE_T size, DWORD prot, DWORD *old) {
    int p = PROT_NONE;
    if (prot == PAGE_READWRITE) p = PROT_READ | PROT_WRITE;
    else if (prot == PAGE_READONLY) p = PROT_READ;
    else if (prot == PAGE_NOACCESS) p = PROT_NONE;
    if (mprotect(addr, size, p)) return FALSE;
    if (old) *old = PAGE_READWRITE;
    return TRUE;
}

SIZE_T VirtualQuery(void *addr, MEMORY_BASIC_INFORMATION *mbi, SIZE_T len) {
    (void)len;
    uint64_t a = (uint64_t)(uintptr_t)addr;
    Region *r = find_region(a);
    memset(mbi, 0, sizeof *mbi);
    mbi->lpBaseAddress = (void *)(uintptr_t)a;
    if (!r) {
        mbi->State = 0x10000;
        mbi->Protect = PAGE_NOACCESS;
        mbi->RegionSize = 4096;
        return sizeof *mbi;
    }
    mbi->RegionSize = r->size - (a - r->base);
    mbi->State = r->committed ? MEM_COMMIT : MEM_RESERVE;
    mbi->Protect = r->committed ? PAGE_READWRITE : PAGE_NOACCESS;
    return sizeof *mbi;
}

UINT GetWriteWatch(DWORD flags, void *addr, SIZE_T size, void **pages, PULONG_PTR count, DWORD *gran) {
    (void)gran;
    uint64_t a = (uint64_t)(uintptr_t)addr;
    Region *r = find_region(a);
    if (!r || !r->write_watch) return 1;
    uint64_t max = *count;
    uint64_t n = 0;
    for (uint64_t off = 0; off < size; off += 4096) {
        if (page_soft_dirty(a + off) && n < max) pages[n++] = (void *)(uintptr_t)(a + off);
    }
    *count = n;
    if (flags & WRITE_WATCH_FLAG_RESET) clear_soft_dirty();
    return 0;
}

BOOL ReadProcessMemory(HANDLE proc, LPCVOID addr, void *out, SIZE_T n, SIZE_T *got) {
    (void)proc;
    memcpy(out, addr, n);
    if (got) *got = n;
    return TRUE;
}

BOOL GetProcessMemoryInfo(HANDLE proc, PROCESS_MEMORY_COUNTERS *pmc, DWORD sz) {
    (void)proc;
    if (sz < sizeof *pmc) return FALSE;
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru)) return FALSE;
    pmc->cb = sizeof *pmc;
    pmc->PeakWorkingSetSize = (SIZE_T)ru.ru_maxrss * 1024;
    pmc->PrivateUsage = pmc->PeakWorkingSetSize;
    return TRUE;
}

BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *s) {
    long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    s->dwLength = sizeof *s;
    s->ullTotalPhys = (uint64_t)pages * (uint64_t)page;
    s->ullAvailPhys = (uint64_t)avail * (uint64_t)page;
    return TRUE;
}

typedef struct OsThread {
    uint32_t kind; /* 'THRD' */
    pthread_t tid;
    sem_t done;
    int detached;
    LPTHREAD_START_ROUTINE fn;
    void *arg;
    DWORD id;
} OsThread;

#define OS_KIND_THREAD 0x54485244u
#define OS_KIND_SEM 0x53454d00u
#define OS_KIND_TIMER 0x54494d52u
#define OS_KIND_FILE 0x46494c45u

typedef struct OsFile {
    uint32_t kind;
    int fd;
} OsFile;

static int os_file_fd(HANDLE h) {
    OsFile *f = h;
    return f && f->kind == OS_KIND_FILE ? f->fd : -1;
}

typedef struct OsTimer {
    uint32_t kind;
    int fd;
} OsTimer;

typedef struct OsSem {
    uint32_t kind;
    sem_t sem;
} OsSem;

static int handle_is_kind(HANDLE h, uint32_t kind) {
    if (!h) return 0;
    return *(uint32_t *)h == kind;
}

static DWORD next_tid = 1;

static void *thread_trampoline(void *p) {
    OsThread *t = p;
    t->fn(t->arg);
    if (!t->detached) sem_post(&t->done);
    return NULL;
}

HANDLE CreateThread(void *sec, SIZE_T stack, LPTHREAD_START_ROUTINE fn, void *arg, DWORD flags, DWORD *tid) {
    (void)sec;
    (void)stack;
    (void)flags;
    OsThread *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->kind = OS_KIND_THREAD;
    t->fn = fn;
    t->arg = arg;
    t->id = __sync_fetch_and_add(&next_tid, 1);
    sem_init(&t->done, 0, 0);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack) pthread_attr_setstacksize(&attr, stack);
    if (pthread_create(&t->tid, &attr, thread_trampoline, t)) {
        free(t);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    if (tid) *tid = t->id;
    return t;
}

BOOL CloseHandle(HANDLE h) {
    if (!h) return FALSE;
    if (handle_is_kind(h, OS_KIND_THREAD)) {
        OsThread *t = h;
        if (!t->detached) sem_destroy(&t->done);
        free(t);
        return TRUE;
    }
    if (handle_is_kind(h, OS_KIND_SEM)) {
        OsSem *s = h;
        sem_destroy(&s->sem);
        free(s);
        return TRUE;
    }
    if (handle_is_kind(h, OS_KIND_TIMER)) {
        OsTimer *tm = h;
        if (tm->fd >= 0) close(tm->fd);
        free(tm);
        return TRUE;
    }
    if (handle_is_kind(h, OS_KIND_FILE)) {
        OsFile *f = h;
        if (f->fd >= 0) close(f->fd);
        free(f);
        return TRUE;
    }
    return FALSE;
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    if (handle_is_kind(h, OS_KIND_THREAD)) {
        OsThread *t = h;
        if (ms == INFINITE) {
            sem_wait(&t->done);
            return 0;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        return sem_timedwait(&t->done, &ts) == 0 ? 0 : WAIT_TIMEOUT;
    }
    if (handle_is_kind(h, OS_KIND_SEM)) {
        OsSem *s = h;
        if (ms == INFINITE) {
            sem_wait(&s->sem);
            return 0;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        return sem_timedwait(&s->sem, &ts) == 0 ? 0 : WAIT_TIMEOUT;
    }
    if (handle_is_kind(h, OS_KIND_TIMER)) {
        OsTimer *tm = h;
        struct pollfd pfd = {.fd = tm->fd, .events = POLLIN};
        int to = ms == INFINITE ? -1 : (int)ms;
        int r = poll(&pfd, 1, to);
        if (r > 0) {
            uint64_t exp;
            (void)read(tm->fd, &exp, sizeof exp);
            return 0;
        }
        return r == 0 ? WAIT_TIMEOUT : WAIT_FAILED;
    }
    return WAIT_FAILED;
}

DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL wait_all, DWORD ms) {
    if (!hs || !n) return WAIT_FAILED;
    if (n > MAXIMUM_WAIT_OBJECTS) return WAIT_FAILED;
    uint64_t deadline = ms == INFINITE ? 0 : plat_now_ns() + (uint64_t)ms * 1000000ull;
    for (;;) {
        for (DWORD i = 0; i < n; i++) {
            DWORD r = WaitForSingleObject(hs[i], 0);
            if (r == 0) return wait_all ? WAIT_TIMEOUT : WAIT_OBJECT_0 + i;
        }
        if (!wait_all) {
            for (DWORD i = 0; i < n; i++) {
                DWORD r = WaitForSingleObject(hs[i], 1);
                if (r == 0) return WAIT_OBJECT_0 + i;
            }
        }
        if (ms != INFINITE && plat_now_ns() >= deadline) return WAIT_TIMEOUT;
        sched_yield();
    }
}

DWORD ResumeThread(HANDLE h) { (void)h; return 0; }
DWORD SuspendThread(HANDLE h) { (void)h; return (DWORD)-1; }
BOOL SetThreadAffinityMask(HANDLE h, ULONG_PTR mask) {
    (void)h;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < 64; i++)
        if (mask & (1ull << i)) CPU_SET(i, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0;
}
DWORD GetCurrentThreadId(void) { return (DWORD)syscall(SYS_gettid); }
HANDLE GetCurrentThread(void) { return (HANDLE)pthread_self(); }

static _Thread_local int tls_thread_priority = THREAD_PRIORITY_NORMAL;

BOOL SetThreadPriority(HANDLE h, int priority) {
    (void)h;
    int pol = SCHED_OTHER;
    struct sched_param sp = {0};
    if (priority >= THREAD_PRIORITY_HIGHEST) sp.sched_priority = sched_get_priority_max(pol);
    tls_thread_priority = priority;
    return pthread_setschedparam(pthread_self(), pol, &sp) == 0 || errno == EPERM;
}

int GetThreadPriority(HANDLE h) {
    (void)h;
    return tls_thread_priority;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *t) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t->QuadPart = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) {
    f->QuadPart = 1000000000ll;
    return TRUE;
}

HANDLE CreateEventW(void *sec, BOOL manual, BOOL init, LPCWSTR name) {
    (void)sec;
    (void)manual;
    (void)name;
    OsSem *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->kind = OS_KIND_SEM;
    sem_init(&s->sem, 0, init ? 1 : 0);
    return s;
}
HANDLE CreateEventA(void *sec, BOOL manual, BOOL init, LPCSTR name) {
    (void)name;
    return CreateEventW(sec, manual, init, NULL);
}
BOOL SetEvent(HANDLE h) {
    OsSem *s = h;
    return sem_post(&s->sem) == 0;
}
BOOL ResetEvent(HANDLE h) {
    OsSem *s = h;
    while (sem_trywait(&s->sem) == 0) {}
    return TRUE;
}

static void segv_handler(int sig, siginfo_t *info, void *ctx) {
    (void)sig;
    if (!g_vectored.handler) {
        signal(SIGSEGV, SIG_DFL);
        return;
    }
    EXCEPTION_RECORD er = {.ExceptionCode = EXCEPTION_ACCESS_VIOLATION, .NumberParameters = 2,
                           .ExceptionInformation = {1, (ULONG_PTR)(uintptr_t)info->si_addr}};
    EXCEPTION_POINTERS ep = {.ExceptionRecord = &er, .ContextRecord = ctx};
    if (g_vectored.handler(&ep) == EXCEPTION_CONTINUE_EXECUTION) return;
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

PVECTORED_EXCEPTION_HANDLER AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER fn) {
    (void)first;
    g_vectored.handler = fn;
    if (!g_vectored.installed) {
        struct sigaction sa = {.sa_sigaction = segv_handler, .sa_flags = SA_SIGINFO | SA_ONSTACK};
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        g_vectored.installed = 1;
    }
    return fn;
}
ULONG RemoveVectoredExceptionHandler(PVECTORED_EXCEPTION_HANDLER fn) {
    if (g_vectored.handler == fn) g_vectored.handler = NULL;
    return 1;
}

static int utf8_to_wide(const char *s, wchar_t *w, int cap) {
    if (!s) { if (w && cap) w[0] = 0; return 0; }
    size_t n = mbstowcs(w, s, cap > 0 ? (size_t)cap - 1 : 0);
    if (n == (size_t)-1) return 0;
    if (w && cap) w[n] = 0;
    return (int)n + 1;
}

static int wide_to_utf8(const wchar_t *w, char *s, int cap) {
    if (!w) { if (s && cap) s[0] = 0; return 0; }
    size_t n = wcstombs(s, w, cap > 0 ? (size_t)cap - 1 : 0);
    if (n == (size_t)-1) return 0;
    if (s && cap) s[n] = 0;
    return (int)n + 1;
}

int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int srclen, LPWSTR dst, int dstcap) {
    (void)cp; (void)flags;
    if (srclen < 0) srclen = (int)strlen(src);
    char tmp[8192];
    if (srclen >= (int)sizeof tmp) return 0;
    memcpy(tmp, src, srclen);
    tmp[srclen] = 0;
    return utf8_to_wide(tmp, dst, dstcap);
}

int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int srclen, LPSTR dst, int dstcap, const char *def, BOOL *used) {
    (void)cp; (void)flags; (void)def; if (used) *used = FALSE;
    if (srclen < 0) srclen = (int)wcslen(src);
    wchar_t tmp[4096];
    if (srclen >= (int)(sizeof tmp / sizeof tmp[0])) return 0;
    wmemcpy(tmp, src, srclen);
    tmp[srclen] = 0;
    return wide_to_utf8(tmp, dst, dstcap);
}

static void path_wide_to_utf8(LPCWSTR w, char *out, size_t cap) {
    wide_to_utf8(w, out, (int)cap);
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attr, HANDLE tmpl) {
    (void)share; (void)sec; (void)attr; (void)tmpl;
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    int flags = O_CLOEXEC;
    if (access & GENERIC_WRITE) {
        flags |= O_RDWR;
        if (disp == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
        else flags |= O_RDWR;
    } else flags |= O_RDONLY;
    if (disp == OPEN_EXISTING) {}
    mode_t mode = 0644;
    int fd = open(p, flags, mode);
    if (fd < 0) {
        SetLastError(errno);
        return INVALID_HANDLE_VALUE;
    }
    OsFile *of = calloc(1, sizeof *of);
    if (!of) {
        close(fd);
        return INVALID_HANDLE_VALUE;
    }
    of->kind = OS_KIND_FILE;
    of->fd = fd;
    return of;
}

HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attr, HANDLE tmpl) {
    wchar_t w[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096);
    return CreateFileW(w, access, share, sec, disp, attr, tmpl);
}

BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    OVERLAPPED *o = ov;
    ssize_t r = o ? pread(fd, buf, n, (off_t)(((uint64_t)o->OffsetHigh << 32) | o->Offset)) : read(fd, buf, n);
    if (r < 0) {
        SetLastError(errno);
        return FALSE;
    }
    if (got) *got = (DWORD)r;
    return TRUE;
}

BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    OVERLAPPED *o = ov;
    ssize_t r = o ? pwrite(fd, buf, n, (off_t)(((uint64_t)o->OffsetHigh << 32) | o->Offset)) : write(fd, buf, n);
    if (r < 0) {
        SetLastError(errno);
        return FALSE;
    }
    if (put) *put = (DWORD)r;
    return TRUE;
}

BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER *sz) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    struct stat st;
    if (fstat(fd, &st)) return FALSE;
    sz->QuadPart = st.st_size;
    return TRUE;
}

BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER at, LARGE_INTEGER *out, DWORD method) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    int whence = SEEK_SET;
    if (method == 1) whence = SEEK_CUR;
    else if (method == 2) whence = SEEK_END;
    off_t r = lseek(fd, (off_t)at.QuadPart, whence);
    if (r < 0) return FALSE;
    if (out) out->QuadPart = r;
    return TRUE;
}

BOOL SetEndOfFile(HANDLE h) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    off_t pos = lseek(fd, 0, SEEK_CUR);
    return ftruncate(fd, pos) == 0;
}

BOOL FlushFileBuffers(HANDLE h) {
    int fd = os_file_fd(h);
    return fd >= 0 && fsync(fd) == 0;
}

BOOL SetFileInformationByHandle(HANDLE h, FILE_INFO_BY_HANDLE_CLASS cls, void *info, DWORD len) {
    (void)len;
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    if (cls == FileEndOfFileInfo) {
        FILE_END_OF_FILE_INFO *eof = info;
        return ftruncate(fd, (off_t)eof->EndOfFile.QuadPart) == 0;
    }
    return FALSE;
}

BOOL CreateHardLinkW(LPCWSTR target, LPCWSTR link_path, void *sec) {
    (void)sec;
    char t[4096], l[4096];
    path_wide_to_utf8(target, t, sizeof t);
    path_wide_to_utf8(link_path, l, sizeof l);
    return link(t, l) == 0;
}

void GetSystemTime(SYSTEMTIME *st) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    st->wYear = (WORD)(tm.tm_year + 1900);
    st->wMonth = (WORD)(tm.tm_mon + 1);
    st->wDayOfWeek = (WORD)tm.tm_wday;
    st->wDay = (WORD)tm.tm_mday;
    st->wHour = (WORD)tm.tm_hour;
    st->wMinute = (WORD)tm.tm_min;
    st->wSecond = (WORD)tm.tm_sec;
    st->wMilliseconds = (WORD)(ts.tv_nsec / 1000000L);
}

BOOL DeleteFileW(LPCWSTR path) {
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    return unlink(p) == 0;
}

BOOL DeleteFileA(LPCSTR path) {
    wchar_t w[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096);
    return DeleteFileW(w);
}

BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    char a[4096], b[4096];
    path_wide_to_utf8(from, a, sizeof a);
    path_wide_to_utf8(to, b, sizeof b);
    if (flags & MOVEFILE_REPLACE_EXISTING) return rename(a, b) == 0;
    return link(a, b) == 0 || rename(a, b) == 0;
}

BOOL CreateDirectoryW(LPCWSTR path, void *sec) {
    (void)sec;
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    return mkdir(p, 0755) == 0 || errno == EEXIST;
}

BOOL CreateDirectoryA(LPCSTR path, void *sec) {
    (void)sec;
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

WORD GetActiveProcessorCount(WORD group) {
    (void)group;
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > 65535) n = 65535;
    return (WORD)n;
}

DWORD GetFileAttributesW(LPCWSTR path) {
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    struct stat st;
    if (stat(p, &st)) return INVALID_FILE_ATTRIBUTES;
    if (S_ISDIR(st.st_mode)) return FILE_ATTRIBUTE_DIRECTORY;
    return FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFileAttributesA(LPCSTR path) {
    wchar_t w[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096);
    return GetFileAttributesW(w);
}

BOOL GetFileAttributesExW(LPCWSTR path, int level, void *data) {
    (void)level;
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    struct stat st;
    if (stat(p, &st)) return FALSE;
    WIN32_FILE_ATTRIBUTE_DATA *d = data;
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeLow = (DWORD)st.st_size;
    d->nFileSizeHigh = (DWORD)(st.st_size >> 32);
    return TRUE;
}

typedef struct { DIR *dir; char pattern[512]; char first[512]; int has_first; } FindCtx;

HANDLE FindFirstFileW(LPCWSTR pat, WIN32_FIND_DATAW *fd) {
    char p[4096];
    path_wide_to_utf8(pat, p, sizeof p);
    char *slash = strrchr(p, '/');
    if (!slash) slash = strrchr(p, '\\');
    char dir[4096] = ".", name[512] = "*";
    if (slash) {
        *slash = 0;
        snprintf(dir, sizeof dir, "%s", p);
        snprintf(name, sizeof name, "%s", slash + 1);
    }
    FindCtx *c = calloc(1, sizeof *c);
    c->dir = opendir(dir[0] ? dir : ".");
    snprintf(c->pattern, sizeof c->pattern, "%s", name);
    if (!c->dir) { free(c); return INVALID_HANDLE_VALUE; }
    if (!FindNextFileW(c, fd)) { FindClose(c); return INVALID_HANDLE_VALUE; }
    return c;
}

BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd) {
    FindCtx *c = h;
    struct dirent *de;
    while ((de = readdir(c->dir))) {
        if (de->d_name[0] == '.' && (!de->d_name[1] || (de->d_name[1] == '.' && !de->d_name[2]))) continue;
        if (strcmp(c->pattern, "*") && fnmatch(c->pattern, de->d_name, 0)) continue;
        utf8_to_wide(de->d_name, fd->cFileName, MAX_PATH);
        fd->dwFileAttributes = (de->d_type == DT_DIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        return TRUE;
    }
    return FALSE;
}

BOOL FindClose(HANDLE h) {
    FindCtx *c = h;
    if (c->dir) closedir(c->dir);
    free(c);
    return TRUE;
}

HANDLE FindFirstFileExA(LPCSTR pat, int level, WIN32_FIND_DATAA *fd, int filter, LPCSTR name, DWORD flags) {
    (void)level;
    (void)filter;
    (void)name;
    (void)flags;
    wchar_t wpat[4096];
    MultiByteToWideChar(CP_UTF8, 0, pat, -1, wpat, 4096);
    WIN32_FIND_DATAW wfd;
    HANDLE h = FindFirstFileW(wpat, &wfd);
    if (h == INVALID_HANDLE_VALUE) return h;
    fd->dwFileAttributes = wfd.dwFileAttributes;
    fd->nFileSizeLow = wfd.nFileSizeLow;
    fd->nFileSizeHigh = wfd.nFileSizeHigh;
    WideCharToMultiByte(CP_UTF8, 0, wfd.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
    return h;
}

HANDLE FindFirstFileA(LPCSTR pat, WIN32_FIND_DATAA *fd) {
    return FindFirstFileExA(pat, FindExInfoBasic, fd, FindExSearchNameMatch, NULL, 0);
}

BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd) {
    WIN32_FIND_DATAW wfd;
    if (!FindNextFileW(h, &wfd)) return FALSE;
    fd->dwFileAttributes = wfd.dwFileAttributes;
    fd->nFileSizeLow = wfd.nFileSizeLow;
    fd->nFileSizeHigh = wfd.nFileSizeHigh;
    WideCharToMultiByte(CP_UTF8, 0, wfd.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
    return TRUE;
}

DWORD GetModuleFileNameW(HMODULE mod, LPWSTR out, DWORD cap) {
    (void)mod;
    char p[4096];
    ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n < 0) return 0;
    p[n] = 0;
    return (DWORD)utf8_to_wide(p, out, (int)cap);
}

DWORD GetModuleFileNameA(HMODULE mod, LPSTR out, DWORD cap) {
    (void)mod;
    char p[4096];
    ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n < 0) return 0;
    p[n] = 0;
    return (DWORD)snprintf(out, cap, "%s", p);
}

HMODULE GetModuleHandleW(LPCWSTR name) { (void)name; return (HMODULE)(intptr_t)1; }
HMODULE LoadLibraryW(LPCWSTR name) {
    char p[4096];
    path_wide_to_utf8(name, p, sizeof p);
    return dlopen(p, RTLD_NOW);
}
HMODULE LoadLibraryA(LPCSTR name) { return dlopen(name, RTLD_NOW); }
void *GetProcAddress(HMODULE mod, LPCSTR name) { return dlsym(mod, name); }
BOOL FreeLibrary(HMODULE mod) { return dlclose(mod) == 0; }

void timeBeginPeriod(UINT ms) { (void)ms; }
void timeEndPeriod(UINT ms) { (void)ms; }

short GetAsyncKeyState(int vk) { (void)vk; return 0; }

HWND GetForegroundWindow(void) { return NULL; }

BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)m; (void)w; (void)l; return TRUE; }

LRESULT SendMessageW(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)m; (void)w; (void)l; return 0; }

BOOL GetClientRect(HWND h, RECT *r) {
    (void)h;
    if (r) memset(r, 0, sizeof *r);
    return TRUE;
}

BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    (void)h; (void)after; (void)x; (void)y; (void)cx; (void)cy; (void)flags;
    return TRUE;
}

BOOL ShowWindow(HWND h, int cmd) { (void)h; (void)cmd; return TRUE; }

BOOL ClipCursor(const RECT *r) { (void)r; return TRUE; }
HWND SetCapture(HWND h) { return h; }
BOOL ReleaseCapture(void) { return TRUE; }

HANDLE GetStdHandle(DWORD n) { (void)n; return (HANDLE)(intptr_t)2; }
DWORD GetFileType(HANDLE h) { (void)h; return 0; }

typedef struct { volatile int done; PINIT_ONCE_FN fn; PVOID param; PVOID *ctx; } InitOnceCtx;

BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID param, PVOID *ctx) {
    if ((__sync_fetch_and_add(&once->state, 0) & 2) != 0) return TRUE;
    if (__sync_bool_compare_and_swap(&once->state, 0, 1)) {
        BOOL ok = fn(once, param, ctx);
        __sync_fetch_and_or(&once->state, 2);
        return ok;
    }
    while ((__sync_fetch_and_add(&once->state, 0) & 2) == 0) sched_yield();
    return TRUE;
}

BOOL InitOnceBeginInitialize(PINIT_ONCE once, DWORD flags, BOOL *pending, void **ctx) {
    (void)flags;
    (void)ctx;
    if ((__sync_fetch_and_add(&once->state, 0) & 2) != 0) {
        if (pending) *pending = FALSE;
        return TRUE;
    }
    if (__sync_bool_compare_and_swap(&once->state, 0, 1)) {
        if (pending) *pending = TRUE;
        return TRUE;
    }
    while ((__sync_fetch_and_add(&once->state, 0) & 2) == 0) sched_yield();
    if (pending) *pending = FALSE;
    return TRUE;
}

BOOL InitOnceComplete(PINIT_ONCE once, DWORD flags, void *ctx) {
    (void)flags;
    (void)ctx;
    __sync_fetch_and_or(&once->state, 2);
    return TRUE;
}

BOOL CreatePipe(HANDLE *r, HANDLE *w, SECURITY_ATTRIBUTES *sec, DWORD size) {
    (void)sec;
    (void)size;
    int fd[2];
    if (pipe(fd)) return FALSE;
    OsFile *ri = calloc(1, sizeof *ri), *wi = calloc(1, sizeof *wi);
    if (!ri || !wi) {
        close(fd[0]);
        close(fd[1]);
        free(ri);
        free(wi);
        return FALSE;
    }
    ri->kind = wi->kind = OS_KIND_FILE;
    ri->fd = fd[0];
    wi->fd = fd[1];
    *r = ri;
    *w = wi;
    return TRUE;
}

BOOL SetHandleInformation(HANDLE h, DWORD mask, DWORD flags) {
    (void)h;
    (void)mask;
    (void)flags;
    return TRUE;
}

static wchar_t g_cmdline_w[4096];
LPWSTR GetCommandLineW(void) {
    if (!g_cmdline_w[0]) {
        char buf[4096] = {0};
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            read(fd, buf, sizeof buf - 1);
            close(fd);
        }
        utf8_to_wide(buf, g_cmdline_w, 4096);
    }
    return g_cmdline_w;
}

LPWSTR _wcsdup(LPCWSTR s) {
    size_t n = wcslen(s) + 1;
    LPWSTR p = malloc(n * sizeof *p);
    if (p) wmemcpy(p, s, n);
    return p;
}

BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) {
    if (!value) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
}

BOOL CreateProcessW(LPCWSTR app, LPWSTR cmdline, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, LPCWSTR dir,
                   STARTUPINFOW *si, PROCESS_INFORMATION *pi) {
    (void)app;
    (void)cmdline;
    (void)pa;
    (void)ta;
    (void)inherit;
    (void)flags;
    (void)env;
    (void)dir;
    (void)si;
    (void)pi;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

HANDLE OpenThread(DWORD access, BOOL inherit, DWORD tid) {
    (void)access;
    (void)inherit;
    (void)tid;
    return NULL;
}

BOOL GetThreadContext(HANDLE h, CONTEXT *c) {
    (void)h;
    if (c) memset(c, 0, sizeof *c);
    return FALSE;
}

BOOL SetThreadContext(HANDLE h, const CONTEXT *c) {
    (void)h;
    (void)c;
    return FALSE;
}

UINT MapVirtualKeyW(UINT code, UINT map) {
    (void)map;
    return code;
}

BOOL IsIconic(HWND h) {
    (void)h;
    return FALSE;
}

DWORD TlsAlloc(void) { return 1; }
DWORD FlsAlloc(void (*cb)(void *)) { (void)cb; return 1; }

HANDLE CreateWaitableTimerExW(void *a, void *b, DWORD flags, DWORD access) {
    (void)a;
    (void)b;
    (void)flags;
    (void)access;
    OsTimer *t = calloc(1, sizeof *t);
    if (!t) return INVALID_HANDLE_VALUE;
    t->kind = OS_KIND_TIMER;
    t->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (t->fd < 0) {
        free(t);
        return INVALID_HANDLE_VALUE;
    }
    return t;
}

static BOOL timer_set_rel(OsTimer *t, const LARGE_INTEGER *due, LONG period) {
    struct itimerspec its = {0};
    int64_t rel100 = due ? -due->QuadPart : 0;
    if (rel100 < 0) rel100 = 0;
    its.it_value.tv_sec = (time_t)(rel100 / 10000000ll);
    its.it_value.tv_nsec = (long)((rel100 % 10000000ll) * 100ll);
    if (period > 0) {
        int64_t p100 = period;
        its.it_interval.tv_sec = (time_t)(p100 / 10000000ll);
        its.it_interval.tv_nsec = (long)((p100 % 10000000ll) * 100ll);
    }
    return timerfd_settime(t->fd, 0, &its, NULL) == 0;
}

BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, void *cb, void *arg, BOOL resume) {
    (void)cb;
    (void)arg;
    (void)resume;
    if (!handle_is_kind(h, OS_KIND_TIMER)) return FALSE;
    return timer_set_rel(h, due, period);
}

BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER *due, LONG period, void *cb, void *arg, void *x, ULONG y) {
    (void)x;
    (void)y;
    return SetWaitableTimer(h, due, period, cb, arg, FALSE);
}
BOOL AssignProcessToJobObject(HANDLE job, HANDLE proc) { (void)job; (void)proc; return TRUE; }

BOOL GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *info) {
    int fd = os_file_fd(h);
    if (fd < 0 || !info) return FALSE;
    struct stat st;
    if (fstat(fd, &st)) return FALSE;
    memset(info, 0, sizeof *info);
    info->nFileSizeLow = (DWORD)st.st_size;
    info->nFileSizeHigh = (DWORD)(st.st_size >> 32);
    info->nNumberOfLinks = (DWORD)st.st_nlink;
    info->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    return TRUE;
}

DWORD GetTempPathW(DWORD cap, LPWSTR out) {
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    return (DWORD)utf8_to_wide(t, out, (int)cap);
}

DWORD GetTempPathA(DWORD cap, LPSTR out) {
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    if (!out || !cap) return (DWORD)strlen(t);
    snprintf(out, cap, "%s", t);
    size_t n = strlen(out);
    if (n && out[n - 1] != '/') strncat(out, "/", cap - n - 1);
    return (DWORD)strlen(out);
}

UINT GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out) {
    (void)unique;
    if (!dir || !prefix || !out) return 0;
    char path[4096];
    snprintf(path, sizeof path, "%s/%sXXXXXX", dir, prefix);
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);
    snprintf(out, MAX_PATH, "%s", path);
    return 1;
}

DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }

void *_aligned_malloc(size_t size, size_t align) {
    void *p = NULL;
    if (posix_memalign(&p, align, size)) return NULL;
    return p;
}

void _aligned_free(void *p) { free(p); }

BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *mod) {
    (void)flags;
    (void)name;
    if (mod) *mod = (HMODULE)(intptr_t)1;
    return TRUE;
}

BOOL GetModuleInformation(HANDLE proc, HMODULE mod, void *info, DWORD sz) {
    (void)proc;
    (void)mod;
    (void)sz;
    if (info) memset(info, 0, sz);
    return TRUE;
}

BOOL GetExitCodeProcess(HANDLE proc, DWORD *code) {
    (void)proc;
    if (code) *code = 0;
    return TRUE;
}

BOOL GetThreadTimes(HANDLE thr, FILETIME *c, FILETIME *e, FILETIME *k, FILETIME *u) {
    (void)thr;
    (void)c;
    (void)e;
    (void)k;
    (void)u;
    return TRUE;
}

BOOL ConvertFiberToThread(void) { return TRUE; }

void YieldProcessor(void) { __builtin_ia32_pause(); }

BOOL ClientToScreen(HWND h, POINT *p) {
    (void)h;
    (void)p;
    return TRUE;
}

char *_strdup(const char *s) { return strdup(s); }
int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }
char *strtok_s(char *str, const char *delim, char **ctx) { return strtok_r(str, delim, ctx); }
int _putenv_s(const char *name, const char *value) {
    if (!value || !*value) return unsetenv(name) == 0 ? 0 : -1;
    return setenv(name, value, 1);
}

BOOL SetFileAttributesW(LPCWSTR path, DWORD attr) {
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    if (attr & FILE_ATTRIBUTE_READONLY) return chmod(p, 0444) == 0;
    return chmod(p, 0644) == 0;
}

BOOL RemoveDirectoryW(LPCWSTR path) {
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    return rmdir(p) == 0;
}

BOOL RemoveDirectoryA(LPCSTR path) { return rmdir(path) == 0; }

DWORD GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *file_part) {
    char resolved[4096];
    if (!realpath(path, resolved)) {
        if (cap) snprintf(out, cap, "%s", path);
        if (file_part) {
            char *slash = strrchr(out, '/');
            *file_part = slash ? slash + 1 : out;
        }
        return (DWORD)strlen(out);
    }
    if (cap) snprintf(out, cap, "%s", resolved);
    if (file_part) {
        char *slash = strrchr(out, '/');
        *file_part = slash ? slash + 1 : out;
    }
    return (DWORD)strlen(out);
}

void GetLocalTime(SYSTEMTIME *st) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    st->wYear = (WORD)(tm.tm_year + 1900);
    st->wMonth = (WORD)(tm.tm_mon + 1);
    st->wDayOfWeek = (WORD)tm.tm_wday;
    st->wDay = (WORD)tm.tm_mday;
    st->wHour = (WORD)tm.tm_hour;
    st->wMinute = (WORD)tm.tm_min;
    st->wSecond = (WORD)tm.tm_sec;
    st->wMilliseconds = 0;
}

NTSTATUS BCryptGenRandom(void *alg, PUCHAR buf, ULONG len, ULONG flags) {
    (void)alg;
    (void)flags;
    ssize_t n = getrandom(buf, len, 0);
    return n == (ssize_t)len ? 0 : (NTSTATUS)-1;
}

int GetUserDefaultLocaleName(wchar_t *name, int cap) {
    if (!name || cap < 2) return 0;
    setlocale(LC_ALL, "");
    const char *loc = setlocale(LC_CTYPE, NULL);
    if (!loc || !*loc) loc = "en_US.UTF-8";
    char tag[LOCALE_NAME_MAX_LENGTH];
    size_t i = 0;
    for (; loc[i] && loc[i] != '.' && loc[i] != '@' && i + 1 < sizeof tag; i++) {
        char c = loc[i];
        tag[i] = c == '_' ? '-' : c;
    }
    tag[i] = 0;
    if (!i) snprintf(tag, sizeof tag, "en-US");
    return utf8_to_wide(tag, name, cap);
}

void _get_timezone(long *sec) {
    tzset();
    if (sec) *sec = timezone;
}

errno_t localtime_s(struct tm *tm, const time_t *t) {
    if (!tm || !t) return EINVAL;
    if (!localtime_r(t, tm)) return EINVAL;
    return 0;
}

int _fseeki64(FILE *f, int64_t off, int whence) { return fseeko(f, (off_t)off, whence); }

int64_t _ftelli64(FILE *f) { return (int64_t)ftello(f); }

#endif
