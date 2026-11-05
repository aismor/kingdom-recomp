"""Memory available to new worker processes (stdlib only, Windows)."""
import ctypes
import ctypes.wintypes as wt
import sys

JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
LIMIT_PROCESS_MEMORY = 0x100
LIMIT_JOB_MEMORY = 0x200


class _MemStatus(ctypes.Structure):
    _fields_ = [('dwLength', wt.DWORD), ('dwMemoryLoad', wt.DWORD)] + \
               [(n, ctypes.c_ulonglong) for n in ('total', 'avail', 'tpf', 'apf', 'tv', 'av', 'aev')]


class _JobLimits(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                ('LimitFlags', wt.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD),
                ('r_ops', ctypes.c_ulonglong), ('w_ops', ctypes.c_ulonglong), ('o_ops', ctypes.c_ulonglong),
                ('r_bytes', ctypes.c_ulonglong), ('w_bytes', ctypes.c_ulonglong), ('o_bytes', ctypes.c_ulonglong),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def _job_headroom():
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.QueryInformationJobObject.argtypes = [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p]
    info = _JobLimits()
    if not k32.QueryInformationJobObject(None, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(info),
                                         ctypes.sizeof(info), None):
        return None
    if info.LimitFlags & LIMIT_JOB_MEMORY:
        return max(0, info.JobMemoryLimit - info.PeakJobMemoryUsed)
    if info.LimitFlags & LIMIT_PROCESS_MEMORY:
        return info.ProcessMemoryLimit
    return None


def available_ram():
    if sys.platform != 'win32':
        return None
    status = _MemStatus()
    status.dwLength = ctypes.sizeof(status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None
    headroom = _job_headroom()
    return status.avail if headroom is None else min(status.avail, headroom)
