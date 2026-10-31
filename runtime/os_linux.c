// Linux implementations of the Win32-shaped API declared in os.h.
#define _GNU_SOURCE
#define OS_LINUX_IMPL
#include "os.h"

#ifndef _WIN32

#include <sched.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <locale.h>
#include <poll.h>
#include <spawn.h>
#include <wchar.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <ucontext.h>

// The kernel write watch (ww_kernel_*) needs Linux 6.7 UAPI. The AppImage builds against the Steam Runtime's 5.10
// headers, which lack it: the values below are the kernel's (include/uapi/linux/fs.h and userfaultfd.h). An older
// running kernel refuses the features at UFFDIO_API, and the region falls back to the software write watch.
#ifndef PAGEMAP_SCAN
struct page_region {
    __u64 start;
    __u64 end;
    __u64 categories;
};
struct pm_scan_arg {
    __u64 size;
    __u64 flags;
    __u64 start;
    __u64 end;
    __u64 walk_end;
    __u64 vec;
    __u64 vec_len;
    __u64 max_pages;
    __u64 category_inverted;
    __u64 category_mask;
    __u64 category_anyof_mask;
    __u64 return_mask;
};
#define PAGEMAP_SCAN _IOWR('f', 16, struct pm_scan_arg)
#define PAGE_IS_WRITTEN (1 << 1)
#define PM_SCAN_WP_MATCHING (1 << 0)
#endif
#ifndef UFFD_FEATURE_WP_UNPOPULATED
#define UFFD_FEATURE_WP_UNPOPULATED (1 << 13)
#endif
#ifndef UFFD_FEATURE_WP_ASYNC
#define UFFD_FEATURE_WP_ASYNC (1 << 15)
#endif
#ifndef UFFD_USER_MODE_ONLY
#define UFFD_USER_MODE_ONLY 1
#endif


#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define FILE_BEGIN 0
#define GetFileExInfoStandard 0
#define QDC_ONLY_ACTIVE_PATHS 0

// ---- virtual memory
// Win32 reservations on mmap. A region keeps one state byte per 4 KiB page: the Win32 protection of a committed page
// (PG_* below, 0 = reserved) and, in a MEM_WRITE_WATCH region, the write-watch bits. Write watch is done in software,
// as Linux has no per-range equivalent of GetWriteWatch: a read-write page is mapped read-only until its first write
// (WW_ARMED); that write faults, ww_fault (ahead of the vectored handlers) marks the page written (WW_DIRTY) and maps it
// read-write. GetWriteWatch reports the written pages and, with WRITE_WATCH_FLAG_RESET, arms the reported ones again.
// A kernel write into an armed page fails with EFAULT instead of faulting, so ReadFile disarms its buffer first
// (ww_prepare_write). Regions are prepended and never unlinked (a release zeroes the size), so the fault handler walks
// the list without a lock.
// Where the kernel has it (Linux 6.7+), the kernel keeps the write watch instead (ww_kernel_*): a userfaultfd in async
// write-protect mode clears a page's protection on its first write, a minor fault with no signal and no mprotect, and
// PAGEMAP_SCAN reports the written pages and protects them again in one call. Those regions never arm a page.
#define PG 4096ull
#define MEM_FREE 0x10000
#define MEM_PRIVATE 0x20000
enum { PG_RESERVED = 0, PG_NOACCESS = 1, PG_READONLY = 2, PG_READWRITE = 3, PG_PROT = 7, WW_DIRTY = 0x10, WW_ARMED = 0x20 };

typedef struct Region {
    uint64_t base;
    volatile uint64_t size;   // 0 once released
    int write_watch;
    int kernel_ww;            // write watch kept by the kernel (ww_kernel_*), not by WW_ARMED
    uint8_t *pg;              // size / PG state bytes
    struct Region *next;
} Region;

static Region *regions;
static pthread_mutex_t region_lock = PTHREAD_MUTEX_INITIALIZER;   // region list changes
static PlatMutex ww_lock = PLAT_MUTEX_INIT;                       // write-watch bits and the mapping of armed pages
static _Thread_local DWORD last_error;

#define MAX_VEH 8
static PVECTORED_EXCEPTION_HANDLER g_veh[MAX_VEH];   // called in order; read by the signal handler without a lock
static volatile int g_veh_n;
static pthread_mutex_t veh_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_veh_installed;

DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD e) { last_error = e; }

uint64_t GetTickCount64(void) { return plat_now_ns() / 1000000ull; }
void Sleep(DWORD ms) { plat_sleep_ns((uint64_t)ms * 1000000); }
BOOL SwitchToThread(void) { sched_yield(); return TRUE; }
void ExitProcess(UINT code) { _exit((int)code); }
HANDLE GetCurrentProcess(void) { return (HANDLE)(intptr_t)getpid(); }

static Region *find_region(uint64_t a) {
    for (Region *r = __atomic_load_n(&regions, __ATOMIC_ACQUIRE); r; r = r->next) {
        uint64_t n = r->size;
        if (a >= r->base && a - r->base < n) return r;
    }
    return NULL;
}

// The region holding all of [a, a + n), or NULL.
static Region *find_range(uint64_t a, uint64_t n) {
    Region *r = find_region(a);
    return r && n <= r->size - (a - r->base) ? r : NULL;
}

static Region *add_region(uint64_t base, uint64_t size, int ww, int kernel_ww) {
    Region *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    void *pg = mmap(NULL, size / PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (pg == MAP_FAILED) {
        free(r);
        return NULL;
    }
    r->base = base;
    r->size = size;
    r->write_watch = ww;
    r->kernel_ww = kernel_ww;
    r->pg = pg;
    pthread_mutex_lock(&region_lock);
    r->next = regions;
    __atomic_store_n(&regions, r, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&region_lock);
    return r;
}

static int prot_code(DWORD prot) {
    switch (prot & ~PAGE_GUARD) {
    case PAGE_NOACCESS: return PG_NOACCESS;
    case PAGE_READONLY: return PG_READONLY;
    default: return PG_READWRITE;
    }
}

static DWORD prot_win(int code) {
    return code == PG_NOACCESS ? PAGE_NOACCESS : code == PG_READONLY ? PAGE_READONLY : code == PG_READWRITE ? PAGE_READWRITE : 0;
}

static int host_prot(uint8_t s) {
    switch (s & PG_PROT) {
    case PG_READWRITE: return s & WW_ARMED ? PROT_READ : PROT_READ | PROT_WRITE;
    case PG_READONLY: return PROT_READ;
    default: return PROT_NONE;
    }
}

// Maps pages [i0, i1) of r as their state bytes say, one mprotect per run of equal host protection.
static int apply_host(Region *r, uint64_t i0, uint64_t i1) {
    int ok = 1;
    while (i0 < i1) {
        int p = host_prot(r->pg[i0]);
        uint64_t j = i0 + 1;
        while (j < i1 && host_prot(r->pg[j]) == p) j++;
        if (mprotect((void *)(uintptr_t)(r->base + i0 * PG), (j - i0) * PG, p)) ok = 0;
        i0 = j;
    }
    return ok;
}

// The Win32 protection of pages [i0, i1) becomes code (PG_RESERVED decommits); written pages keep WW_DIRTY, the other
// read-write pages of a watched region are armed.
static void set_pages(Region *r, uint64_t i0, uint64_t i1, int code) {
    for (uint64_t i = i0; i < i1; i++) {
        uint8_t s = code == PG_RESERVED ? 0 : (uint8_t)(code | (r->pg[i] & WW_DIRTY));
        if (r->write_watch && !r->kernel_ww && code == PG_READWRITE && !(s & WW_DIRTY)) s |= WW_ARMED;
        r->pg[i] = s;
    }
}

// A write fault at a: an armed page becomes written and writable. 1 when the write can retry.
static int ww_fault(uint64_t a) {
    Region *r = find_region(a);
    if (!r || !r->write_watch) return 0;
    uint64_t i = (a - r->base) / PG;
    plat_mutex_lock(&ww_lock);
    uint8_t s = r->pg[i];
    int ok = (s & PG_PROT) == PG_READWRITE;
    if (ok && (s & WW_ARMED)) {
        r->pg[i] = (uint8_t)((s & ~WW_ARMED) | WW_DIRTY);
        ok = mprotect((void *)(uintptr_t)(r->base + i * PG), PG, PROT_READ | PROT_WRITE) == 0;
    }
    plat_mutex_unlock(&ww_lock);
    return ok;
}

// Before a kernel write into [a, a + n): its armed pages become written and writable.
static void ww_prepare_write(const void *p, uint64_t n) {
    uint64_t a = (uint64_t)(uintptr_t)p;
    if (!n) return;
    Region *r = find_region(a);
    if (!r || !r->write_watch) return;
    uint64_t end = a + n < r->base + r->size ? a + n : r->base + r->size;
    uint64_t i0 = (a - r->base) / PG, i1 = (end - r->base + PG - 1) / PG;
    plat_mutex_lock(&ww_lock);
    for (uint64_t i = i0; i < i1; i++)
        if ((r->pg[i] & PG_PROT) == PG_READWRITE && (r->pg[i] & WW_ARMED)) {
            r->pg[i] = (uint8_t)((r->pg[i] & ~WW_ARMED) | WW_DIRTY);
            mprotect((void *)(uintptr_t)(r->base + i * PG), PG, PROT_READ | PROT_WRITE);
        }
    plat_mutex_unlock(&ww_lock);
}

// A device pins imported host memory for writing, which an armed (read-only) page refuses: vk_memory.c import_pool.
void os_prepare_host_import(const void *p, size_t n) { ww_prepare_write(p, n); }

// ---- kernel write watch. A page the kernel never populated counts as written until UFFDIO_WRITEPROTECT marks it, so
// pages are marked when they leave the reserved state (ww_kernel_commit) and only committed pages are scanned: a
// decommit drops the mark with the page.
static int ww_ufd = -1, ww_pagemap = -1;
static pthread_once_t ww_kernel_once = PTHREAD_ONCE_INIT;

static void ww_kernel_init(void) {
    int fd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
    if (fd < 0) return;
    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED};
    int pm = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (pm < 0 || ioctl(fd, UFFDIO_API, &api)) {
        if (pm >= 0) close(pm);
        close(fd);
        return;
    }
    ww_ufd = fd;
    ww_pagemap = pm;
}

// A new write-watched reservation. 0: the kernel cannot keep its write watch, the software one does.
static int ww_kernel_register(uint64_t base, uint64_t size) {
    pthread_once(&ww_kernel_once, ww_kernel_init);
    if (ww_ufd < 0) return 0;
    struct uffdio_register reg = {.range = {.start = base, .len = size}, .mode = UFFDIO_REGISTER_MODE_WP};
    return ioctl(ww_ufd, UFFDIO_REGISTER, &reg) == 0;
}

// Before pages [i0, i1) of r are committed (ww_lock held): the reserved ones are marked unwritten. Pages already
// committed keep their state, as a Win32 commit of committed pages keeps their contents.
static int ww_kernel_commit(Region *r, uint64_t i0, uint64_t i1) {
    if (!r->kernel_ww) return 1;
    int ok = 1;
    while (i0 < i1) {
        if (r->pg[i0] & PG_PROT) {
            i0++;
            continue;
        }
        uint64_t j = i0 + 1;
        while (j < i1 && !(r->pg[j] & PG_PROT)) j++;
        struct uffdio_writeprotect wp = {.range = {.start = r->base + i0 * PG, .len = (j - i0) * PG},
                                         .mode = UFFDIO_WRITEPROTECT_MODE_WP};
        if (ioctl(ww_ufd, UFFDIO_WRITEPROTECT, &wp)) ok = 0;
        i0 = j;
    }
    return ok;
}

// GetWriteWatch over pages [i, i1) of a kernel-watched region (ww_lock held): one PAGEMAP_SCAN per run of committed
// pages; with reset, the scan protects the pages it reports again. 0 on success, as GetWriteWatch.
static UINT ww_kernel_query(Region *r, uint64_t i, uint64_t i1, int reset, void **pages, uint64_t max, uint64_t *count) {
    struct page_region v[64];
    uint64_t n = 0;
    const uint64_t prot8 = PG_PROT * 0x0101010101010101ull;
    while (i < i1 && n < max) {
        if (!(i & 7) && i + 8 <= i1) {
            uint64_t w;
            memcpy(&w, r->pg + i, 8);
            if (!(w & prot8)) {
                i += 8;
                continue;
            }
        }
        if (!(r->pg[i] & PG_PROT)) {
            i++;
            continue;
        }
        uint64_t j = i + 1;
        while (j < i1 && (r->pg[j] & PG_PROT)) j++;
        uint64_t a = r->base + i * PG, end = r->base + j * PG;
        while (a < end && n < max) {
            struct pm_scan_arg arg = {.size = sizeof arg, .flags = reset ? PM_SCAN_WP_MATCHING : 0, .start = a,
                                      .end = end, .vec = (uint64_t)(uintptr_t)v, .vec_len = sizeof v / sizeof *v,
                                      .max_pages = max - n, .category_mask = PAGE_IS_WRITTEN,
                                      .return_mask = PAGE_IS_WRITTEN};
            long got = ioctl(ww_pagemap, PAGEMAP_SCAN, &arg);
            if (got < 0) return 1;
            for (long k = 0; k < got; k++)
                for (uint64_t p = v[k].start; p < v[k].end && n < max; p += PG) pages[n++] = (void *)(uintptr_t)p;
            if (arg.walk_end <= a) break;
            a = arg.walk_end;
        }
        i = j;
    }
    *count = n;
    return 0;
}

void *VirtualAlloc(void *addr, SIZE_T size, DWORD type, DWORD prot) {
    uint64_t at = (uint64_t)(uintptr_t)addr & ~(PG - 1);
    int ww = (type & MEM_WRITE_WATCH) != 0;
    type &= ~MEM_WRITE_WATCH;
    if (!size) {
        last_error = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    if (type == MEM_COMMIT && at) {
        uint64_t end = ((uint64_t)(uintptr_t)addr + size + PG - 1) & ~(PG - 1);
        Region *r = find_range(at, end - at);
        if (!r) {
            last_error = ERROR_INVALID_ADDRESS;
            return NULL;
        }
        uint64_t i0 = (at - r->base) / PG, i1 = (end - r->base) / PG;
        plat_mutex_lock(&ww_lock);
        int ok = ww_kernel_commit(r, i0, i1);
        set_pages(r, i0, i1, prot_code(prot));
        ok = apply_host(r, i0, i1) && ok;
        plat_mutex_unlock(&ww_lock);
        if (!ok) {
            last_error = ERROR_NOT_ENOUGH_MEMORY;
            return NULL;
        }
        return (void *)(uintptr_t)at;
    }
    // MEM_RESERVE, or MEM_COMMIT without an address, which Windows reserves and commits in one go.
    size = (size + PG - 1) & ~(PG - 1);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    if (at) flags |= MAP_FIXED_NOREPLACE;
    void *p = mmap((void *)(uintptr_t)at, size, PROT_NONE, flags, -1, 0);
    if (p == MAP_FAILED) {
        last_error = errno == EEXIST ? ERROR_INVALID_ADDRESS : ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if (at && p != (void *)(uintptr_t)at) {
        munmap(p, size);
        last_error = ERROR_INVALID_ADDRESS;
        return NULL;
    }
    int kernel_ww = ww && ww_kernel_register((uint64_t)(uintptr_t)p, size);
    Region *r = add_region((uint64_t)(uintptr_t)p, size, ww, kernel_ww);
    if (!r) {
        munmap(p, size);
        last_error = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if (type & MEM_COMMIT) {
        plat_mutex_lock(&ww_lock);
        ww_kernel_commit(r, 0, size / PG);
        set_pages(r, 0, size / PG, prot_code(prot));
        apply_host(r, 0, size / PG);
        plat_mutex_unlock(&ww_lock);
    }
    return p;
}

BOOL VirtualFree(void *addr, SIZE_T size, DWORD op) {
    uint64_t at = (uint64_t)(uintptr_t)addr;
    if (op == MEM_RELEASE) {
        Region *r = find_region(at);
        if (!r || r->base != at) {
            last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
        pthread_mutex_lock(&region_lock);
        uint64_t n = r->size;
        r->size = 0;
        pthread_mutex_unlock(&region_lock);
        munmap((void *)(uintptr_t)at, n);
        munmap(r->pg, n / PG);
        return TRUE;
    }
    if (op == MEM_DECOMMIT) {
        uint64_t a = at & ~(PG - 1);
        uint64_t end = (at + size + PG - 1) & ~(PG - 1);
        Region *r = find_range(a, end - a);
        if (!r) {
            last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
        // Windows drops decommitted pages, so a later commit reads zeros; MADV_DONTNEED does that for private
        // anonymous memory.
        plat_mutex_lock(&ww_lock);
        int ok = !madvise((void *)(uintptr_t)a, end - a, MADV_DONTNEED) &&
                 !mprotect((void *)(uintptr_t)a, end - a, PROT_NONE);
        set_pages(r, (a - r->base) / PG, (end - r->base) / PG, PG_RESERVED);
        plat_mutex_unlock(&ww_lock);
        return ok;
    }
    last_error = ERROR_INVALID_PARAMETER;
    return FALSE;
}

BOOL VirtualProtect(void *addr, SIZE_T size, DWORD prot, DWORD *old) {
    uint64_t a = (uint64_t)(uintptr_t)addr & ~(PG - 1);
    uint64_t end = ((uint64_t)(uintptr_t)addr + size + PG - 1) & ~(PG - 1);
    Region *r = find_range(a, end - a);
    if (!r) {
        // Memory that VirtualAlloc did not map (heap, image): plain mprotect.
        int p = prot == PAGE_READWRITE ? PROT_READ | PROT_WRITE : prot == PAGE_READONLY ? PROT_READ : PROT_NONE;
        if (mprotect((void *)(uintptr_t)a, end - a, p)) return FALSE;
        if (old) *old = PAGE_READWRITE;
        return TRUE;
    }
    uint64_t i0 = (a - r->base) / PG, i1 = (end - r->base) / PG;
    plat_mutex_lock(&ww_lock);
    for (uint64_t i = i0; i < i1; i++)
        if (!(r->pg[i] & PG_PROT)) {
            plat_mutex_unlock(&ww_lock);
            last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
    if (old) *old = prot_win(r->pg[i0] & PG_PROT);
    set_pages(r, i0, i1, prot_code(prot));
    int ok = apply_host(r, i0, i1);
    plat_mutex_unlock(&ww_lock);
    return ok;
}

static uint64_t hex_field(const char **s) {
    uint64_t v = 0;
    for (;; (*s)++) {
        char ch = **s;
        int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (d < 0) return v;
        v = v << 4 | (uint64_t)d;
    }
}

// Memory VirtualAlloc did not map (the image, heap, stacks, libraries): the /proc/self/maps line holding a, as
// Windows answers for any mapping. Only open and read, so the crash handler may ask (main.c readable).
static SIZE_T query_host(uint64_t a, MEMORY_BASIC_INFORMATION *mbi) {
    mbi->State = MEM_FREE;
    mbi->Protect = PAGE_NOACCESS;
    mbi->RegionSize = PG;
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return sizeof *mbi;
    char buf[8192];
    size_t have = 0;
    for (;;) {
        ssize_t got = read(fd, buf + have, sizeof buf - 1 - have);
        if (got <= 0) break;
        have += (size_t)got;
        buf[have] = 0;
        char *line = buf, *nl;
        while ((nl = strchr(line, '\n'))) {
            const char *s = line;
            uint64_t lo = hex_field(&s);
            s++;
            uint64_t hi = hex_field(&s);
            s++;
            if (a >= lo && a < hi) {
                mbi->lpAllocationBase = (void *)(uintptr_t)lo;
                mbi->RegionSize = hi - a;
                mbi->State = MEM_COMMIT;
                mbi->Type = MEM_PRIVATE;
                mbi->Protect = s[0] != 'r' ? PAGE_NOACCESS : s[1] == 'w' ? PAGE_READWRITE : PAGE_READONLY;
                close(fd);
                return sizeof *mbi;
            }
            line = nl + 1;
        }
        have = (size_t)(buf + have - line);
        memmove(buf, line, have);
    }
    close(fd);
    return sizeof *mbi;
}

SIZE_T VirtualQuery(void *addr, MEMORY_BASIC_INFORMATION *mbi, SIZE_T len) {
    if (len < sizeof *mbi) return 0;
    uint64_t a = (uint64_t)(uintptr_t)addr & ~(PG - 1);
    Region *r = find_region(a);
    memset(mbi, 0, sizeof *mbi);
    mbi->lpBaseAddress = (void *)(uintptr_t)a;
    if (!r) return query_host(a, mbi);
    // The run of pages from a with the same state and protection, compared eight state bytes at a time.
    uint64_t i = (a - r->base) / PG, n = r->size / PG, j = i + 1;
    uint8_t code = r->pg[i] & PG_PROT;
    const uint64_t mask = 0x0707070707070707ull, want = code * 0x0101010101010101ull;
    while (j < n && (j & 7)) {
        if ((r->pg[j] & PG_PROT) != code) goto done;
        j++;
    }
    while (j + 8 <= n) {
        uint64_t w;
        memcpy(&w, r->pg + j, 8);
        if ((w & mask) != want) break;
        j += 8;
    }
    while (j < n && (r->pg[j] & PG_PROT) == code) j++;
done:
    mbi->lpAllocationBase = (void *)(uintptr_t)r->base;
    mbi->dwAllocationProtect = PAGE_NOACCESS;
    mbi->RegionSize = (j - i) * PG;
    mbi->State = code ? MEM_COMMIT : MEM_RESERVE;
    mbi->Protect = prot_win(code);
    mbi->Type = MEM_PRIVATE;
    return sizeof *mbi;
}

UINT GetWriteWatch(DWORD flags, void *addr, SIZE_T size, void **pages, PULONG_PTR count, DWORD *gran) {
    uint64_t a = (uint64_t)(uintptr_t)addr & ~(PG - 1);
    uint64_t end = ((uint64_t)(uintptr_t)addr + size + PG - 1) & ~(PG - 1);
    Region *r = find_range(a, end - a);
    if (!r || !r->write_watch || !count) return 1;
    if (gran) *gran = (DWORD)PG;
    uint64_t max = *count, n = 0;
    uint64_t i = (a - r->base) / PG, i1 = (end - r->base) / PG;
    int reset = (flags & WRITE_WATCH_FLAG_RESET) != 0;
    const uint64_t dirty8 = WW_DIRTY * 0x0101010101010101ull;
    plat_mutex_lock(&ww_lock);
    if (r->kernel_ww) {
        UINT e = ww_kernel_query(r, i, i1, reset, pages, max, &n);
        plat_mutex_unlock(&ww_lock);
        *count = n;
        return e;
    }
    while (i < i1 && n < max) {
        if (!(i & 7) && i + 8 <= i1) {
            uint64_t w;
            memcpy(&w, r->pg + i, 8);
            if (!(w & dirty8)) {
                i += 8;
                continue;
            }
        }
        if (!(r->pg[i] & WW_DIRTY)) {
            i++;
            continue;
        }
        // A run of written pages: report it and, on reset, arm it again. A write before the mprotect is still seen by
        // the caller, which reads the reported pages afterwards; one after it faults and waits for ww_lock.
        uint64_t j = i;
        while (j < i1 && n < max && (r->pg[j] & WW_DIRTY)) pages[n++] = (void *)(uintptr_t)(r->base + j++ * PG);
        if (reset) {
            for (uint64_t k = i; k < j; k++) {
                r->pg[k] &= (uint8_t)~WW_DIRTY;
                if ((r->pg[k] & PG_PROT) == PG_READWRITE) r->pg[k] |= WW_ARMED;
            }
            apply_host(r, i, j);
        }
        i = j;
    }
    plat_mutex_unlock(&ww_lock);
    *count = n;
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
#define OS_KIND_PROC 0x50524f43u

typedef struct OsFile {
    uint32_t kind;
    int fd;
} OsFile;

// GetStdHandle's handles; CloseHandle leaves them alone.
static OsFile g_std[3] = {{OS_KIND_FILE, 0}, {OS_KIND_FILE, 1}, {OS_KIND_FILE, 2}};

typedef struct OsProc {
    uint32_t kind;
    pid_t pid;
    int exited;
    DWORD code;
} OsProc;

#define OS_KIND_MAP 0x4d415020u

// A file mapping: its own descriptor, so the view outlives both the mapping handle and the file handle.
typedef struct OsMap {
    uint32_t kind;
    int fd;
    int writable;
} OsMap;

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
        if (f >= g_std && f < g_std + 3) return TRUE;
        if (f->fd >= 0) close(f->fd);
        free(f);
        return TRUE;
    }
    if (handle_is_kind(h, OS_KIND_PROC)) {
        free(h);
        return TRUE;
    }
    if (handle_is_kind(h, OS_KIND_MAP)) {
        OsMap *m = h;
        close(m->fd);
        free(m);
        return TRUE;
    }
    return FALSE;
}

// Reaps the child if it has exited; with block set, waits for that. 1 once p->code holds the exit code.
static int proc_reap(OsProc *p, int block) {
    if (p->exited) return 1;
    int st;
    pid_t r;
    do r = waitpid(p->pid, &st, block ? 0 : WNOHANG);
    while (r < 0 && errno == EINTR);
    if (r != p->pid) return 0;
    if (WIFEXITED(st)) {
        p->code = (DWORD)WEXITSTATUS(st);
    } else {
        // A child killed by a fault reports the Win32 exception code its Windows counterpart would exit with.
        int sig = WTERMSIG(st);
        p->code = sig == SIGSEGV || sig == SIGBUS ? EXCEPTION_ACCESS_VIOLATION
                : sig == SIGILL                   ? EXCEPTION_ILLEGAL_INSTRUCTION
                : sig == SIGFPE                   ? EXCEPTION_INT_DIVIDE_BY_ZERO
                                                  : (DWORD)(128 + sig);
    }
    p->exited = 1;
    return 1;
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
    if (handle_is_kind(h, OS_KIND_PROC)) {
        OsProc *p = h;
        if (ms == INFINITE) return proc_reap(p, 1) ? 0 : WAIT_FAILED;
        uint64_t end = plat_now_ns() + (uint64_t)ms * 1000000ull;
        while (!proc_reap(p, 0)) {
            if (plat_now_ns() >= end) return WAIT_TIMEOUT;
            usleep(1000);
        }
        return 0;
    }
    return WAIT_FAILED;
}

DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL wait_all, DWORD ms) {
    if (!hs || !n) return WAIT_FAILED;
    if (n > MAXIMUM_WAIT_OBJECTS) return WAIT_FAILED;
    uint64_t deadline = ms == INFINITE ? 0 : plat_now_ns() + (uint64_t)ms * 1000000ull;
    /* Wait-all one handle after another with what is left of the timeout. Exact for the handles callers pass here
       (threads: gfx_shader.c warm and prepare workers), which stay signalled once signalled. */
    if (wait_all) {
        for (DWORD i = 0; i < n; i++) {
            DWORD left = INFINITE;
            if (ms != INFINITE) {
                uint64_t now = plat_now_ns();
                left = now >= deadline ? 0 : (DWORD)((deadline - now) / 1000000ull);
            }
            DWORD r = WaitForSingleObject(hs[i], left);
            if (r != WAIT_OBJECT_0) return r;
        }
        return WAIT_OBJECT_0;
    }
    for (;;) {
        for (DWORD i = 0; i < n; i++) {
            DWORD r = WaitForSingleObject(hs[i], 0);
            if (r == 0) return WAIT_OBJECT_0 + i;
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
// Cached per thread: EnterCriticalSection asks on every call.
DWORD GetCurrentThreadId(void) {
    static _Thread_local DWORD tid;
    if (!tid) tid = (DWORD)syscall(SYS_gettid);
    return tid;
}
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

// Hardware faults reach the vectored handlers as Win32 exceptions. A write to an armed write-watch page is resolved
// first (ww_fault). The CONTEXT carries Rip and Rsp, written back when a handler continues execution. With no handler
// taking the fault the default action runs: the signal is unblocked and re-raised on return.
static void fault_handler(int sig, siginfo_t *info, void *ctx) {
    ucontext_t *uc = ctx;
    greg_t *gr = uc->uc_mcontext.gregs;
    uint64_t a = (uint64_t)(uintptr_t)info->si_addr;
    // x86 page-fault error code: bit 1 set for a write, bit 4 for an instruction fetch.
    uint64_t err = (uint64_t)gr[REG_ERR];
    int write = sig == SIGSEGV && (err & 2);
    if (write && ww_fault(a)) return;
    DWORD code = sig == SIGILL ? EXCEPTION_ILLEGAL_INSTRUCTION
               : sig == SIGFPE ? EXCEPTION_INT_DIVIDE_BY_ZERO
                               : EXCEPTION_ACCESS_VIOLATION;
    EXCEPTION_RECORD er = {.ExceptionCode = code};
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        er.NumberParameters = 2;
        er.ExceptionInformation[0] = sig == SIGSEGV && (err & 16) ? 8 : write ? 1 : 0;
        er.ExceptionInformation[1] = (ULONG_PTR)a;
    }
    CONTEXT c = {.ContextFlags = CONTEXT_CONTROL, .Rip = (DWORD64)gr[REG_RIP], .Rsp = (DWORD64)gr[REG_RSP]};
    EXCEPTION_POINTERS ep = {.ExceptionRecord = &er, .ContextRecord = &c};
    int n = g_veh_n;
    for (int i = 0; i < n; i++) {
        PVECTORED_EXCEPTION_HANDLER h = g_veh[i];
        if (h && h(&ep) == EXCEPTION_CONTINUE_EXECUTION) {
            gr[REG_RIP] = (greg_t)c.Rip;
            gr[REG_RSP] = (greg_t)c.Rsp;
            return;
        }
    }
    signal(sig, SIG_DFL);
    if (info->si_code <= 0) raise(sig);
}

PVECTORED_EXCEPTION_HANDLER AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER fn) {
    pthread_mutex_lock(&veh_lock);
    if (g_veh_n == MAX_VEH) {
        pthread_mutex_unlock(&veh_lock);
        return NULL;
    }
    if (first) {
        for (int i = g_veh_n; i > 0; i--) g_veh[i] = g_veh[i - 1];
        g_veh[0] = fn;
    } else {
        g_veh[g_veh_n] = fn;
    }
    __atomic_store_n(&g_veh_n, g_veh_n + 1, __ATOMIC_RELEASE);
    if (!g_veh_installed) {
        struct sigaction sa = {.sa_sigaction = fault_handler, .sa_flags = SA_SIGINFO | SA_ONSTACK};
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        g_veh_installed = 1;
    }
    pthread_mutex_unlock(&veh_lock);
    return fn;
}

ULONG RemoveVectoredExceptionHandler(PVECTORED_EXCEPTION_HANDLER fn) {
    pthread_mutex_lock(&veh_lock);
    int n = g_veh_n, found = 0;
    for (int i = 0; i < n; i++) {
        if (!found && g_veh[i] == fn) found = 1;
        if (found) g_veh[i] = i + 1 < n ? g_veh[i + 1] : NULL;
    }
    if (found) __atomic_store_n(&g_veh_n, n - 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&veh_lock);
    return found;
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

// Paths: the runtime builds them the Windows way, with '\' separators (it cuts at the last '\' of the exe path, which
// GetModuleFileName hands out that way, and appends "\name"). Every call that passes a path to the kernel turns '\'
// into '/' first, here and in os_host_path.
static void path_wide_to_utf8(LPCWSTR w, char *out, size_t cap) {
    wide_to_utf8(w, out, (int)cap);
    for (char *p = out; *p; p++)
        if (*p == '\\') *p = '/';
}

// The Win32 error a file call reports for errno e (fs_host.c win_result turns these into nn::fs results).
static DWORD win_error(int e) {
    switch (e) {
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
    case EISDIR: return ERROR_ACCESS_DENIED;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case ENOSPC: return 112;   // ERROR_DISK_FULL
    case EBUSY: case ETXTBSY: return ERROR_SHARING_VIOLATION;
    default: return ERROR_ACCESS_DENIED;
    }
}

void os_host_path(const char *in, char *out, size_t cap) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < cap; i++) out[i] = in[i] == '\\' ? '/' : in[i];
    if (cap) out[i] = 0;
}

FILE *os_fopen(const char *path, const char *mode) {
    char p[4096];
    if (!path) return NULL;
    os_host_path(path, p, sizeof p);
    return fopen(p, mode);
}

int os_remove(const char *path) {
    char p[4096];
    if (!path) return -1;
    os_host_path(path, p, sizeof p);
    return remove(p);
}

static int access_ok(const char *p) { return access(p, F_OK) == 0; }

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attr, HANDLE tmpl) {
    (void)share; (void)sec; (void)attr; (void)tmpl;
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    int flags = O_CLOEXEC;
    if (access & GENERIC_WRITE) flags |= access & GENERIC_READ ? O_RDWR : O_WRONLY;
    else flags |= O_RDONLY;
    switch (disp) {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;   // OPEN_EXISTING
    }
    // CREATE_ALWAYS and OPEN_ALWAYS report ERROR_ALREADY_EXISTS when the file was there, as on Windows.
    int existed = (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS) && access_ok(p);
    int fd = open(p, flags, 0644);
    if (fd < 0) {
        SetLastError(win_error(errno));
        return INVALID_HANDLE_VALUE;
    }
    SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
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
    ww_prepare_write(buf, n);
    ssize_t r = o ? pread(fd, buf, n, (off_t)(((uint64_t)o->OffsetHigh << 32) | o->Offset)) : read(fd, buf, n);
    if (r < 0) {
        SetLastError(win_error(errno));
        return FALSE;
    }
    if (got) *got = (DWORD)r;
    if (o && o->hEvent) SetEvent(o->hEvent);
    return TRUE;
}

// File mappings of whole files (sizes 0, no name): a view is a MAP_SHARED mmap; UnmapViewOfFile finds its length in
// a short list of the live views.
typedef struct MapView {
    const void *base;
    size_t size;
    struct MapView *next;
} MapView;
static MapView *g_views;
static pthread_mutex_t views_lock = PTHREAD_MUTEX_INITIALIZER;

HANDLE CreateFileMappingW(HANDLE file, void *sec, DWORD prot, DWORD size_hi, DWORD size_lo, LPCWSTR name) {
    (void)sec;
    int fd = os_file_fd(file);
    if (fd < 0 || size_hi || size_lo || name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    OsMap *m = calloc(1, sizeof *m);
    if (!m || (m->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0)) < 0) {
        free(m);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    m->kind = OS_KIND_MAP;
    m->writable = prot == PAGE_READWRITE;
    return m;
}

void *MapViewOfFile(HANDLE map, DWORD access, DWORD off_hi, DWORD off_lo, SIZE_T n) {
    if (!handle_is_kind(map, OS_KIND_MAP)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    OsMap *m = map;
    off_t off = (off_t)(((uint64_t)off_hi << 32) | off_lo);
    struct stat st;
    if (fstat(m->fd, &st) || off > st.st_size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!n) n = (SIZE_T)(st.st_size - off);
    int prot = PROT_READ | (m->writable && access != FILE_MAP_READ ? PROT_WRITE : 0);
    void *v = mmap(NULL, n, prot, MAP_SHARED, m->fd, off);
    MapView *rec = v == MAP_FAILED ? NULL : malloc(sizeof *rec);
    if (!rec) {
        if (v != MAP_FAILED) munmap(v, n);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    rec->base = v;
    rec->size = n;
    pthread_mutex_lock(&views_lock);
    rec->next = g_views;
    g_views = rec;
    pthread_mutex_unlock(&views_lock);
    return v;
}

BOOL UnmapViewOfFile(const void *view) {
    pthread_mutex_lock(&views_lock);
    for (MapView **pp = &g_views, *r; (r = *pp); pp = &r->next)
        if (r->base == view) {
            *pp = r->next;
            pthread_mutex_unlock(&views_lock);
            munmap((void *)r->base, r->size);
            free(r);
            return TRUE;
        }
    pthread_mutex_unlock(&views_lock);
    return FALSE;
}

BOOL GetOverlappedResult(HANDLE h, OVERLAPPED *ov, DWORD *got, BOOL wait) {
    (void)wait;
    if (!ov || !got) {
        SetLastError(EINVAL);
        return FALSE;
    }
    *got = 0;
    return TRUE;
}

BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov) {
    int fd = os_file_fd(h);
    if (fd < 0) return FALSE;
    OVERLAPPED *o = ov;
    ssize_t r = o ? pwrite(fd, buf, n, (off_t)(((uint64_t)o->OffsetHigh << 32) | o->Offset)) : write(fd, buf, n);
    if (r < 0) {
        SetLastError(win_error(errno));
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

// Win32 order: the new link's name first, then the existing file.
BOOL CreateHardLinkW(LPCWSTR link_path, LPCWSTR existing, void *sec) {
    (void)sec;
    char e[4096], l[4096];
    path_wide_to_utf8(existing, e, sizeof e);
    path_wide_to_utf8(link_path, l, sizeof l);
    if (link(e, l) == 0) return TRUE;
    SetLastError(win_error(errno));
    return FALSE;
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
    if (unlink(p) == 0) return TRUE;
    SetLastError(win_error(errno));
    return FALSE;
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
    if (!(flags & MOVEFILE_REPLACE_EXISTING)) {
        if (renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE) == 0) return TRUE;
        SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : win_error(errno));
        return FALSE;
    }
    // Windows refuses to replace a file with the read-only attribute (plat_file_set_readonly clears the write bits);
    // rename would not.
    struct stat st;
    if (stat(b, &st) == 0 && S_ISREG(st.st_mode) && !(st.st_mode & S_IWUSR)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (rename(a, b) == 0) return TRUE;
    SetLastError(win_error(errno));
    return FALSE;
}

BOOL CreateDirectoryW(LPCWSTR path, void *sec) {
    (void)sec;
    char p[4096];
    path_wide_to_utf8(path, p, sizeof p);
    if (mkdir(p, 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : win_error(errno));
    return FALSE;
}

BOOL CreateDirectoryA(LPCSTR path, void *sec) {
    (void)sec;
    char p[4096];
    os_host_path(path, p, sizeof p);
    if (mkdir(p, 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : win_error(errno));
    return FALSE;
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
    if (stat(p, &st)) {
        SetLastError(win_error(errno));
        return INVALID_FILE_ATTRIBUTES;
    }
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

typedef struct { DIR *dir; char pattern[512]; } FindCtx;

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
    if (!c->dir) {
        SetLastError(win_error(errno));
        free(c);
        return INVALID_HANDLE_VALUE;
    }
    if (!FindNextFileW(c, fd)) {
        FindClose(c);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return c;
}

// Like Windows, "." and ".." are listed (callers skip them; an empty directory still finds them), and each entry
// carries the attributes, size and write time of what it names.
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd) {
    FindCtx *c = h;
    struct dirent *de;
    while ((de = readdir(c->dir))) {
        if (strcmp(c->pattern, "*") && fnmatch(c->pattern, de->d_name, 0)) continue;
        struct stat st;
        int ok = !fstatat(dirfd(c->dir), de->d_name, &st, 0);
        memset(fd, 0, sizeof *fd);
        utf8_to_wide(de->d_name, fd->cFileName, MAX_PATH);
        if (ok ? S_ISDIR(st.st_mode) : de->d_type == DT_DIR) fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        else if (ok && !(st.st_mode & 0222)) fd->dwFileAttributes = FILE_ATTRIBUTE_READONLY;
        else fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (ok) {
            fd->nFileSizeLow = (DWORD)st.st_size;
            fd->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
            uint64_t ft = ((uint64_t)st.st_mtim.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)st.st_mtim.tv_nsec / 100;
            fd->ftLastWriteTime.dwLowDateTime = (DWORD)ft;
            fd->ftLastWriteTime.dwHighDateTime = (DWORD)(ft >> 32);
        }
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
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
    for (char *c = p; *c; c++)
        if (*c == '/') *c = '\\';
    return (DWORD)utf8_to_wide(p, out, (int)cap);
}

// The exe path with '\' separators, as on Windows: callers cut it at the last '\' (see path_wide_to_utf8).
DWORD GetModuleFileNameA(HMODULE mod, LPSTR out, DWORD cap) {
    (void)mod;
    char p[4096];
    ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n < 0) return 0;
    p[n] = 0;
    for (char *c = p; *c; c++)
        if (*c == '/') *c = '\\';
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

// The key and mouse button state and the focused window, as the SDL window's events report them (gfx_window_sdl.c
// dispatch_winmsg): Linux has no system-wide equivalent of GetAsyncKeyState.
static volatile uint8_t g_key_down[256];
static HWND volatile g_foreground;

void os_key_state(int vk, int down) {
    if (vk > 0 && vk < 256) __atomic_store_n(&g_key_down[vk], (uint8_t)(down != 0), __ATOMIC_RELAXED);
}

void os_foreground(HWND h) {
    __atomic_store_n(&g_foreground, h, __ATOMIC_RELAXED);
    if (!h)   // no key-up arrives for keys released while the window is in the background
        for (int i = 0; i < 256; i++) __atomic_store_n(&g_key_down[i], 0, __ATOMIC_RELAXED);
}

short GetAsyncKeyState(int vk) {
    return vk > 0 && vk < 256 && __atomic_load_n(&g_key_down[vk], __ATOMIC_RELAXED) ? (short)0x8000 : 0;
}

HWND GetForegroundWindow(void) { return __atomic_load_n(&g_foreground, __ATOMIC_RELAXED); }

// PostMessageW, SendMessageW and GetClientRect act on the SDL window: gfx_window_sdl.c.

BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    (void)h; (void)after; (void)x; (void)y; (void)cx; (void)cy; (void)flags;
    return TRUE;
}

BOOL ShowWindow(HWND h, int cmd) { (void)h; (void)cmd; return TRUE; }

BOOL ClipCursor(const RECT *r) { (void)r; return TRUE; }
HWND SetCapture(HWND h) { return h; }
BOOL ReleaseCapture(void) { return TRUE; }

HANDLE GetStdHandle(DWORD n) {
    return n == STD_INPUT_HANDLE ? &g_std[0] : n == STD_OUTPUT_HANDLE ? &g_std[1] : &g_std[2];
}
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

// The process arguments joined with the Windows quoting rules, which cmdline_split undoes: callers hand the string
// back to CreateProcessW to re-run the process.
static wchar_t g_cmdline_w[4096];
static char g_argv0[4096];
LPWSTR GetCommandLineW(void) {
    if (!g_cmdline_w[0]) {
        char raw[4096] = {0}, out[8192];
        ssize_t n = 0;
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            n = read(fd, raw, sizeof raw - 1);
            close(fd);
        }
        snprintf(g_argv0, sizeof g_argv0, "%s", raw);
        size_t o = 0;
        for (ssize_t i = 0; i < n && o + 4 < sizeof out;) {
            const char *a = raw + i;
            size_t len = strlen(a);
            int quote = !len || strpbrk(a, " \t\"") != NULL;
            if (o) out[o++] = ' ';
            if (quote) out[o++] = '"';
            size_t bs = 0;
            for (size_t k = 0; k < len && o + 4 < sizeof out; k++) {
                if (a[k] == '\\') {
                    bs++;
                } else {
                    // Backslashes before a quote are doubled and the quote escaped.
                    if (a[k] == '"') for (size_t b = 0; b < bs + 1 && o + 4 < sizeof out; b++) out[o++] = '\\';
                    bs = 0;
                }
                out[o++] = a[k];
            }
            if (quote) {
                for (size_t b = 0; b < bs && o + 4 < sizeof out; b++) out[o++] = '\\';
                out[o++] = '"';
            }
            i += (ssize_t)len + 1;
        }
        out[o] = 0;
        utf8_to_wide(out, g_cmdline_w, 4096);
    }
    return g_cmdline_w;
}

// Splits a Windows command line in place into at most cap - 1 arguments (CommandLineToArgvW rules for quotes and
// backslashes); argv ends with NULL. The argument count, or -1 when there are more.
static int cmdline_split(char *s, char **argv, int cap) {
    int n = 0;
    char *w = s;
    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (n + 1 >= cap) return -1;
        argv[n++] = w;
        int inq = 0;
        while (*s && (inq || (*s != ' ' && *s != '\t'))) {
            if (*s == '\\') {
                size_t bs = 0;
                while (*s == '\\') bs++, s++;
                if (*s == '"') {
                    for (size_t b = 0; b < bs / 2; b++) *w++ = '\\';
                    if (bs & 1) *w++ = *s++;
                    else inq = !inq, s++;
                } else {
                    for (size_t b = 0; b < bs; b++) *w++ = '\\';
                }
            } else if (*s == '"') {
                inq = !inq;
                s++;
            } else {
                *w++ = *s++;
            }
        }
        int end = !*s;
        *w++ = 0;
        if (end) break;
        s++;
    }
    argv[n] = NULL;
    return n;
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

// posix_spawn of the command line (the program is app, or the first argument; the first argument naming this process
// runs /proc/self/exe). The child inherits every descriptor and the environment; a custom environment block is not
// supported. STARTF_USESTDHANDLES maps the given handles onto descriptors 0 to 2. hThread is NULL.
BOOL CreateProcessW(LPCWSTR app, LPWSTR cmdline, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, LPCWSTR dir,
                   STARTUPINFOW *si, PROCESS_INFORMATION *pi) {
    (void)pa;
    (void)ta;
    (void)inherit;
    (void)flags;
    if (env || !pi || (!app && !cmdline)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    static char line[8192];
    static SRWLOCK spawn_lock = SRWLOCK_INIT;
    char path[4096], cwd[4096];
    char *argv[256];
    AcquireSRWLockExclusive(&spawn_lock);
    GetCommandLineW();
    path_wide_to_utf8(cmdline ? cmdline : app, line, sizeof line);
    int argc = cmdline_split(line, argv, 256);
    if (argc <= 0) {
        ReleaseSRWLockExclusive(&spawn_lock);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (app) path_wide_to_utf8(app, path, sizeof path);
    else snprintf(path, sizeof path, "%s", strcmp(argv[0], g_argv0) ? argv[0] : "/proc/self/exe");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (si && (si->dwFlags & STARTF_USESTDHANDLES)) {
        HANDLE std[3] = {si->hStdInput, si->hStdOutput, si->hStdError};
        for (int i = 0; i < 3; i++) {
            int fd = os_file_fd(std[i]);
            if (fd >= 0 && fd != i) posix_spawn_file_actions_adddup2(&fa, fd, i);
        }
    }
    if (dir) {
        path_wide_to_utf8(dir, cwd, sizeof cwd);
        posix_spawn_file_actions_addchdir_np(&fa, cwd);
    }
    extern char **environ;
    pid_t pid;
    int e = strchr(path, '/') ? posix_spawn(&pid, path, &fa, NULL, argv, environ)
                              : posix_spawnp(&pid, path, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    ReleaseSRWLockExclusive(&spawn_lock);
    if (e) {
        SetLastError(e == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    OsProc *p = calloc(1, sizeof *p);
    if (!p) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    p->kind = OS_KIND_PROC;
    p->pid = pid;
    pi->hProcess = p;
    pi->hThread = NULL;
    pi->dwProcessId = (DWORD)pid;
    pi->dwThreadId = 0;
    return TRUE;
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
    info->dwVolumeSerialNumber = (DWORD)st.st_dev;
    info->nFileIndexLow = (DWORD)st.st_ino;
    info->nFileIndexHigh = (DWORD)(st.st_ino >> 32);
    info->ftLastWriteTime.dwLowDateTime = (DWORD)st.st_mtime;
    info->ftCreation.dwLowDateTime = (DWORD)st.st_ctime;
    info->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    return TRUE;
}

// As on Windows, the temp path ends with a separator: callers append a bare file name.
DWORD GetTempPathW(DWORD cap, LPWSTR out) {
    char t[4096];
    GetTempPathA(sizeof t, t);
    if (!out || !cap) return (DWORD)strlen(t);
    utf8_to_wide(t, out, (int)cap);
    return (DWORD)wcslen(out);
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
    char path[4096], d[4096];
    os_host_path(dir, d, sizeof d);
    snprintf(path, sizeof path, "%s/%sXXXXXX", d, prefix);
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
    if (!code) return FALSE;
    if (handle_is_kind(proc, OS_KIND_PROC)) {
        OsProc *p = proc;
        *code = proc_reap(p, 0) ? p->code : 259;   // STILL_ACTIVE
        return TRUE;
    }
    *code = 0;
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
    if (rmdir(p) == 0) return TRUE;
    SetLastError(win_error(errno));
    return FALSE;
}

BOOL RemoveDirectoryA(LPCSTR path) {
    char p[4096];
    os_host_path(path, p, sizeof p);
    return rmdir(p) == 0;
}

DWORD GetFullPathNameA(LPCSTR path_in, DWORD cap, LPSTR out, LPSTR *file_part) {
    char resolved[4096], path[4096];
    os_host_path(path_in, path, sizeof path);
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

// The MSVC range: 1970-01-01 through 3000-12-31 23:59:59 UTC; earlier or later times are EINVAL.
errno_t localtime_s(struct tm *tm, const time_t *t) {
    if (!tm || !t || *t < 0 || *t > 32535244799) return EINVAL;
    if (!localtime_r(t, tm)) return EINVAL;
    return 0;
}

int _fseeki64(FILE *f, int64_t off, int whence) { return fseeko(f, (off_t)off, whence); }

int64_t _ftelli64(FILE *f) { return (int64_t)ftello(f); }

FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    char p[4096], m[16];
    if (!path || !mode || !wide_to_utf8(path, p, sizeof p)) return NULL;
    os_host_path(p, p, sizeof p);
    size_t i = 0;
    for (; mode[i] && i + 1 < sizeof m; i++) m[i] = (char)(mode[i] < 0x80 ? mode[i] : 'b');
    m[i] = 0;
    return fopen(p, m);
}

#endif
