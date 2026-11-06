#include "host.h"
#include "os.h"

#ifdef TOTK_PGO_GEN
int __llvm_profile_dump(void);
int __llvm_profile_write_file(void);
void __llvm_profile_reset_counters(void);

static DWORD WINAPI pgo_flush_thread(void *arg) {
    (void)arg;
    for (;;) {
        Sleep(60 * 1000);
        if (__llvm_profile_write_file() == 0) __llvm_profile_reset_counters();
    }
}

__attribute__((constructor)) static void pgo_flush_start(void) {
    HANDLE h = CreateThread(NULL, 0, pgo_flush_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

void pgo_dump(void) { __llvm_profile_dump(); }
#else
void pgo_dump(void) {}
#endif
