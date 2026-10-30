// Host operating system API. On Windows this includes the real headers; on Linux it declares the subset the runtime
// uses and implements them in os_linux.c.
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <shellapi.h>

#else

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "platform/platform.h"

typedef int BOOL;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int64_t LONG64;
typedef int64_t LONGLONG;
typedef uint32_t ULONG;
typedef uint16_t WORD;
typedef uint64_t ULONG_PTR;
typedef uint64_t SIZE_T;
typedef uint32_t UINT;
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef int64_t LRESULT;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef char CHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef unsigned char *LPBYTE;
typedef unsigned char BYTE;
typedef wchar_t WCHAR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;
typedef void *HINSTANCE;
typedef void *HWND;
typedef void *HMONITOR;
typedef void *HMODULE;
typedef void *HKEY;
typedef void *HDC;
typedef void *HGLRC;
typedef DWORD *LPDWORD;
typedef SIZE_T *PSIZE_T;
typedef ULONG_PTR *PULONG_PTR;
typedef uint64_t ULONGLONG;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef union {
    struct { DWORD LowPart, HighPart; };
    struct { DWORD dwLowDateTime, dwHighDateTime; };
    uint64_t QuadPart;
} ULARGE_INTEGER;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { LONG x, y; } POINT;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;

#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define WAIT_OBJECT_0 0
#define MAXIMUM_WAIT_OBJECTS 64

#define INFINITE 0xFFFFFFFFu
#define MAX_PATH 4096
#define ERROR_SUCCESS 0
#define ERROR_TIMEOUT 1460
#define ERROR_INVALID_PARAMETER 87
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_SHARING_VIOLATION 32
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_DIRECTORY 267
#define ERROR_FILE_EXISTS 80
#define ERROR_NOACCESS 998
#define ERROR_HANDLE_EOF 38

#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8

typedef void *PVOID;
typedef struct {
    ULONG_PTR Internal, InternalHigh;
    union {
        struct { DWORD Offset, OffsetHigh; };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED;

typedef struct { volatile LONG state; } INIT_ONCE;
typedef INIT_ONCE *PINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {0}
typedef BOOL (*PINIT_ONCE_FN)(PINIT_ONCE once, PVOID param, PVOID *ctx);
BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID param, PVOID *ctx);
BOOL InitOnceBeginInitialize(PINIT_ONCE once, DWORD flags, BOOL *pending, void **ctx);
BOOL InitOnceComplete(PINIT_ONCE once, DWORD flags, void *ctx);

typedef struct { LARGE_INTEGER EndOfFile; } FILE_END_OF_FILE_INFO;
typedef enum { FileEndOfFileInfo = 6 } FILE_INFO_BY_HANDLE_CLASS;
BOOL SetFileInformationByHandle(HANDLE h, FILE_INFO_BY_HANDLE_CLASS cls, void *info, DWORD len);

typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreation, ftLastAccess, ftLastWriteTime;
    DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks, nFileIndexHigh, nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;
BOOL GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *info);

DWORD GetTempPathW(DWORD cap, LPWSTR out);
DWORD GetTempPathA(DWORD cap, LPSTR out);
UINT GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out);
DWORD GetCurrentProcessId(void);
void *_aligned_malloc(size_t size, size_t align);
void _aligned_free(void *p);

BOOL CreateHardLinkW(LPCWSTR target, LPCWSTR link, void *sec);
void GetSystemTime(SYSTEMTIME *st);

#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
#define MB_PRECOMPOSED 1

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100

#define FILE_ATTRIBUTE_REPARSE_POINT 0x400

#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_TIME_CRITICAL 15

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define MEM_RESERVE 0x2000
#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define MEM_DECOMMIT 0x4000
#define MEM_WRITE_WATCH 0x200000
#define WRITE_WATCH_FLAG_RESET 1
#define PAGE_GUARD 0x100

#define CONTEXT_CONTROL 0x100001
#define CONTEXT_INTEGER 0x100002
typedef uint64_t DWORD64;
typedef struct {
    DWORD64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    DWORD ContextFlags;
    DWORD64 Rip;
    DWORD64 Rsp;
} CONTEXT;

typedef struct {
    DWORD nLength;
    void *lpSecurityDescriptor;
    int bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct {
    DWORD cb;
    LPWSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;

typedef struct {
    HANDLE hProcess, hThread;
    DWORD dwProcessId, dwThreadId;
} PROCESS_INFORMATION;

#define STARTF_USESTDHANDLES 0x00000100
#define HANDLE_FLAG_INHERIT 0x00000001
#define STD_INPUT_HANDLE ((DWORD)-10)

#define MAPVK_VK_TO_CHAR 2

BOOL CreatePipe(HANDLE *r, HANDLE *w, SECURITY_ATTRIBUTES *sec, DWORD size);
BOOL SetHandleInformation(HANDLE h, DWORD mask, DWORD flags);
LPWSTR GetCommandLineW(void);
LPWSTR _wcsdup(LPCWSTR s);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
BOOL CreateProcessW(LPCWSTR app, LPWSTR cmdline, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, LPCWSTR dir,
                   STARTUPINFOW *si, PROCESS_INFORMATION *pi);
BOOL GetThreadContext(HANDLE h, CONTEXT *c);
BOOL SetThreadContext(HANDLE h, const CONTEXT *c);
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD tid);
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_SET_CONTEXT 0x0010
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800

BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *mod);
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
BOOL GetModuleInformation(HANDLE proc, HMODULE mod, void *info, DWORD sz);
typedef struct { LPVOID lpBaseOfDll; DWORD SizeOfImage, EntryPoint; } MODULEINFO;
BOOL GetExitCodeProcess(HANDLE proc, DWORD *code);
BOOL GetThreadTimes(HANDLE thr, FILETIME *c, FILETIME *e, FILETIME *k, FILETIME *u);
BOOL ConvertFiberToThread(void);
void YieldProcessor(void);

#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001D
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096
#define EXCEPTION_STACK_OVERFLOW 0xC00000FD
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006

BOOL ClientToScreen(HWND h, POINT *p);

#define WM_CREATE 0x0001
#define WM_INPUT 0x00FF
#define WM_SETCURSOR 0x0020
#define WM_SYSCOMMAND 0x0112
#define SC_KEYMENU 0xF100
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_SIZE 0x0005
#define WM_MOVE 0x0003
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define HTCLIENT 1
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))

UINT MapVirtualKeyW(UINT code, UINT map);
BOOL IsIconic(HWND h);
HANDLE GetStdHandle(DWORD n);

#define FIBER_FLAG_FLOAT_SWITCH 0x1
typedef void (WINAPI *LPFIBER_START_ROUTINE)(void *param);
void *ConvertThreadToFiberEx(void *param, DWORD flags);
void *CreateFiberEx(SIZE_T stackCommit, SIZE_T stackReserve, DWORD flags, LPFIBER_START_ROUTINE start, void *param);
void SwitchToFiber(void *fiber);
void DeleteFiber(void *fiber);

#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define MEM_RESERVE 0x2000
#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define MEM_DECOMMIT 0x4000
#define MEM_WRITE_WATCH 0x200000
#define WRITE_WATCH_FLAG_RESET 1

#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_READONLY 0x01
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define CREATE_SUSPENDED 0x4
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x10000
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#define TIMER_ALL_ACCESS 0x001F0003

#define EXCEPTION_ACCESS_VIOLATION 0xC0000005
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_ESCAPE 0x1B
#define VK_RETURN 0x0D
#define VK_SPACE 0x20
#define VK_TAB 0x09
#define VK_BACK 0x08
#define VK_DELETE 0x2E
#define VK_INSERT 0x2D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_CAPITAL 0x14
#define VK_NUMPAD0 0x60
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_MULTIPLY 0x6A
#define VK_DIVIDE 0x6F
#define VK_DECIMAL 0x6E
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PLUS 0xBB
#define VK_OEM_4 0xDB
#define VK_OEM_6 0xDD
#define VK_OEM_1 0xBA
#define VK_OEM_7 0xDE
#define VK_OEM_COMMA 0xBC
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_5 0xDC
#define VK_OEM_3 0xC0
#define VK_UP 0x26
#define VK_DOWN 0x28
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_HOME 0x24
#define VK_END 0x23
#define VK_PRIOR 0x21
#define VK_NEXT 0x22

#define WM_ACTIVATEAPP 0x001C
#define WM_MOUSEACTIVATE 0x0021
#define MK_LBUTTON 0x0001
#define HTCLIENT 1
#define HTCAPTION 2
#define MAKELPARAM(l, h) ((LPARAM)((((uint32_t)(h)) << 16) | ((uint32_t)(l) & 0xffff)))

#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_SIZE 0x0005
#define WM_CLOSE 0x0010
#define WM_APP 0x8000
#define SIZE_MINIMIZED 1
#define SWP_NOMOVE 0x0002
#define SWP_NOSIZE 0x0001
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_NOOWNERZORDER 0x0200

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; WCHAR szDevice[32]; } MONITORINFOEXW;
typedef struct { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { DWORD cbSize; int showCmd; POINT ptMinPosition; POINT ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *arg);
typedef struct {
    LPVOID lpBaseAddress;
    LPVOID lpAllocationBase;
    DWORD dwAllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;
#define BaseAddress lpBaseAddress
#define AllocationBase lpAllocationBase
typedef struct { DWORD cb; SIZE_T PeakWorkingSetSize; SIZE_T PrivateUsage; } PROCESS_MEMORY_COUNTERS;
typedef PROCESS_MEMORY_COUNTERS PROCESS_MEMORY_COUNTERS_EX;
#define K32GetProcessMemoryInfo GetProcessMemoryInfo
typedef struct { DWORD dwLength; DWORD dwMemoryLoad; uint64_t ullTotalPhys, ullAvailPhys; } MEMORYSTATUSEX;
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

typedef struct { DWORD ExceptionCode; DWORD NumberParameters; ULONG_PTR ExceptionInformation[15]; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; CONTEXT *ContextRecord; } EXCEPTION_POINTERS;

typedef PlatMutex SRWLOCK;
#define SRWLOCK_INIT PLAT_MUTEX_INIT
static inline void InitializeSRWLock(SRWLOCK *l) { plat_mutex_init(l); }
static inline void AcquireSRWLockExclusive(SRWLOCK *l) { plat_mutex_lock(l); }
static inline void ReleaseSRWLockExclusive(SRWLOCK *l) { plat_mutex_unlock(l); }
static inline void AcquireSRWLockShared(SRWLOCK *l) { plat_mutex_lock(l); }
static inline void ReleaseSRWLockShared(SRWLOCK *l) { plat_mutex_unlock(l); }
static inline int TryAcquireSRWLockExclusive(SRWLOCK *l) { return plat_mutex_trylock(l); }

typedef PlatMutex CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *l) { plat_mutex_init(l); }
static inline void EnterCriticalSection(CRITICAL_SECTION *l) { plat_mutex_lock(l); }
static inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *l) { return plat_mutex_trylock(l) ? TRUE : FALSE; }
static inline void LeaveCriticalSection(CRITICAL_SECTION *l) { plat_mutex_unlock(l); }
static inline void DeleteCriticalSection(CRITICAL_SECTION *l) { (void)l; }

typedef PlatCond CONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT PLAT_COND_INIT
static inline void InitializeConditionVariable(CONDITION_VARIABLE *c) { plat_cond_init(c); }
static inline void WakeConditionVariable(CONDITION_VARIABLE *c) { plat_cond_signal(c); }
static inline void WakeAllConditionVariable(CONDITION_VARIABLE *c) { plat_cond_broadcast(c); }
static inline BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *c, SRWLOCK *l, DWORD ms, ULONG flags) {
    (void)flags;
    if (ms == INFINITE) {
        plat_cond_wait(c, l);
        return TRUE;
    }
    return plat_cond_wait_ns(c, l, (uint64_t)ms * 1000000) ? TRUE : FALSE;
}

static inline LONG InterlockedIncrement(volatile LONG *p) { return __sync_add_and_fetch(p, 1); }
static inline LONG InterlockedDecrement(volatile LONG *p) { return __sync_sub_and_fetch(p, 1); }
static inline LONG InterlockedExchange(volatile LONG *p, LONG v) { return __sync_lock_test_and_set(p, v); }
static inline LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG c) {
    return __sync_val_compare_and_swap(p, c, v);
}
static inline LONG64 InterlockedExchange64(volatile LONG64 *p, LONG64 v) { return __sync_lock_test_and_set(p, v); }
static inline LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 v, LONG64 c) {
    return __sync_val_compare_and_swap(p, c, v);
}
static inline LONG InterlockedExchangeAdd(volatile LONG *p, LONG v) { return __sync_fetch_and_add(p, v); }
static inline LONG InterlockedAdd(volatile LONG *p, LONG v) { return __sync_fetch_and_add(p, v); }
static inline LONG64 InterlockedIncrement64(volatile LONG64 *p) { return __sync_add_and_fetch(p, 1); }
static inline LONG64 InterlockedDecrement64(volatile LONG64 *p) { return __sync_sub_and_fetch(p, 1); }
static inline LONG64 InterlockedAdd64(volatile LONG64 *p, LONG64 v) { return __sync_fetch_and_add(p, v); }
static inline LONG64 InterlockedOr64(volatile LONG64 *p, LONG64 v) { return __sync_fetch_and_or(p, v); }

DWORD GetLastError(void);
void SetLastError(DWORD e);
uint64_t GetTickCount64(void);
void Sleep(DWORD ms);
BOOL SwitchToThread(void);
void ExitProcess(UINT code);

void *VirtualAlloc(void *addr, SIZE_T size, DWORD type, DWORD prot);
BOOL VirtualFree(void *size, SIZE_T n, DWORD op);
BOOL VirtualProtect(void *addr, SIZE_T size, DWORD prot, DWORD *old);
SIZE_T VirtualQuery(void *addr, MEMORY_BASIC_INFORMATION *mbi, SIZE_T len);
UINT GetWriteWatch(DWORD flags, void *addr, SIZE_T size, void **pages, PULONG_PTR count, DWORD *gran);

BOOL ReadProcessMemory(HANDLE proc, LPCVOID addr, void *out, SIZE_T n, SIZE_T *got);
HANDLE GetCurrentProcess(void);
BOOL GetProcessMemoryInfo(HANDLE proc, PROCESS_MEMORY_COUNTERS *pmc, DWORD sz);
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *s);

HANDLE CreateThread(void *sec, SIZE_T stack, LPTHREAD_START_ROUTINE fn, void *arg, DWORD flags, DWORD *tid);
BOOL CloseHandle(HANDLE h);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL wait_all, DWORD ms);
DWORD ResumeThread(HANDLE h);
DWORD SuspendThread(HANDLE h);
BOOL SetThreadAffinityMask(HANDLE h, ULONG_PTR mask);
DWORD GetCurrentThreadId(void);
HANDLE GetCurrentThread(void);
BOOL SetThreadPriority(HANDLE h, int priority);
int GetThreadPriority(HANDLE h);
BOOL QueryPerformanceCounter(LARGE_INTEGER *t);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f);

HANDLE CreateEventW(void *sec, BOOL manual, BOOL init, LPCWSTR name);
HANDLE CreateEventA(void *sec, BOOL manual, BOOL init, LPCSTR name);
BOOL SetEvent(HANDLE h);
BOOL ResetEvent(HANDLE h);
HANDLE CreateWaitableTimerExW(void *a, void *b, DWORD flags, DWORD access);
BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER *due, LONG period, void *cb, void *arg, void *x, ULONG y);
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, void *cb, void *arg, BOOL resume);
BOOL AssignProcessToJobObject(HANDLE job, HANDLE proc);

typedef LONG (*PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *ep);
PVECTORED_EXCEPTION_HANDLER AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER fn);
ULONG RemoveVectoredExceptionHandler(PVECTORED_EXCEPTION_HANDLER fn);

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attr, HANDLE tmpl);
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attr, HANDLE tmpl);
BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov);
BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov);
BOOL CloseHandle(HANDLE h);
BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER *sz);
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER at, LARGE_INTEGER *out, DWORD method);
BOOL SetEndOfFile(HANDLE h);
BOOL FlushFileBuffers(HANDLE h);
BOOL DeleteFileW(LPCWSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL SetFileAttributesW(LPCWSTR path, DWORD attr);
BOOL RemoveDirectoryW(LPCWSTR path);
BOOL RemoveDirectoryA(LPCSTR path);
DWORD GetFullPathNameA(LPCSTR path, DWORD cap, LPSTR out, LPSTR *file_part);
void GetLocalTime(SYSTEMTIME *st);
BOOL CreateDirectoryW(LPCWSTR path, void *sec);
BOOL CreateDirectoryA(LPCSTR path, void *sec);
#define ALL_PROCESSOR_GROUPS ((WORD)0xffff)
WORD GetActiveProcessorCount(WORD group);
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetFileAttributesA(LPCSTR path);
BOOL GetFileAttributesExW(LPCWSTR path, int level, void *data);
typedef struct { DWORD dwFileAttributes; uint64_t ftCreation, ftAccess, ftWrite; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;

typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreation, ftLastAccess, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;
HANDLE FindFirstFileW(LPCWSTR pat, WIN32_FIND_DATAW *fd);
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd);
BOOL FindClose(HANDLE h);

#define FindExInfoBasic 0
#define FindExSearchNameMatch 0
#define FIND_FIRST_EX_LARGE_FETCH 0x00000002
typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreation, ftLastAccess, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA;
HANDLE FindFirstFileA(LPCSTR pat, WIN32_FIND_DATAA *fd);
HANDLE FindFirstFileExA(LPCSTR pat, int level, WIN32_FIND_DATAA *fd, int filter, LPCSTR name, DWORD flags);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);

int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int srclen, LPWSTR dst, int dstcap);
int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int srclen, LPSTR dst, int dstcap, const char *def, BOOL *used);
DWORD GetModuleFileNameW(HMODULE mod, LPWSTR out, DWORD cap);
DWORD GetModuleFileNameA(HMODULE mod, LPSTR out, DWORD cap);
HMODULE GetModuleHandleW(LPCWSTR name);
HMODULE LoadLibraryW(LPCWSTR name);
HMODULE LoadLibraryA(LPCSTR name);
void *GetProcAddress(HMODULE mod, LPCSTR name);
BOOL FreeLibrary(HMODULE mod);

short GetAsyncKeyState(int vk);
HWND GetForegroundWindow(void);
BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l);
LRESULT SendMessageW(HWND h, UINT m, WPARAM w, LPARAM l);
BOOL GetClientRect(HWND h, RECT *r);
BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL ShowWindow(HWND h, int cmd);
BOOL ClipCursor(const RECT *r);
HWND SetCapture(HWND h);
BOOL ReleaseCapture(void);

void timeBeginPeriod(UINT ms);
void timeEndPeriod(UINT ms);

void rt_sha256(const void *p, size_t n, uint8_t out[32]);

char *_strdup(const char *s);
int _stricmp(const char *a, const char *b);
int _strnicmp(const char *a, const char *b, size_t n);
char *strtok_s(char *str, const char *delim, char **ctx);
int _putenv_s(const char *name, const char *value);

#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

#define LOCALE_NAME_MAX_LENGTH 85
int GetUserDefaultLocaleName(wchar_t *name, int cap);

typedef unsigned char *PUCHAR;
typedef long NTSTATUS;
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002L
NTSTATUS BCryptGenRandom(void *alg, PUCHAR buf, ULONG len, ULONG flags);

#define _tzset() tzset()
void _get_timezone(long *sec);
typedef int errno_t;
errno_t localtime_s(struct tm *tm, const time_t *t);

int _fseeki64(FILE *f, int64_t off, int whence);
int64_t _ftelli64(FILE *f);

struct SDL_Window;
typedef struct SDL_Window SDL_Window;

#endif
