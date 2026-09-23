import ctypes
from ctypes import wintypes
import struct
import subprocess

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
OpenProcess = kernel32.OpenProcess
OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
OpenProcess.restype = wintypes.HANDLE

ReadProcessMemory = kernel32.ReadProcessMemory
ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
ReadProcessMemory.restype = wintypes.BOOL

def get_pid():
    out = subprocess.check_output('tasklist /FI "IMAGENAME eq stellaris.exe" /FO CSV /NH', shell=True).decode()
    for line in out.strip().split('\n'):
        parts = [p.strip(' "\r') for p in line.split(',')]
        if len(parts) >= 2 and parts[0].lower() == 'stellaris.exe':
            return int(parts[1])
    return None

def read_mem(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    ok = ReadProcessMemory(h, addr, buf, size, ctypes.byref(read))
    return buf.raw if ok else None

def read_u64(h, addr):
    data = read_mem(h, addr, 8)
    return struct.unpack('<Q', data)[0] if data else None

def read_u32(h, addr):
    data = read_mem(h, addr, 4)
    return struct.unpack('<I', data)[0] if data else None

def read_pdx_str(h, addr):
    data = read_mem(h, addr, 32)
    if not data: return ""
    size = struct.unpack('<Q', data[16:24])[0]
    cap = struct.unpack('<Q', data[24:32])[0]
    if size == 0 or size > 1024: return ""
    if cap < 16:
        return data[:size].decode('utf-8', errors='ignore')
    else:
        ptr = struct.unpack('<Q', data[:8])[0]
        if ptr and 0x10000 < ptr < 0x7FFFFFFFFFFF:
            heap_data = read_mem(h, ptr, min(size, 256))
            if heap_data:
                return heap_data[:min(size, 256)].decode('utf-8', errors='ignore')
    return ""

def main():
    pid = get_pid()
    print(f"stellaris.exe PID: {pid}")
    h = OpenProcess(0x10, False, pid)
    base = 0x7FF75ED50000
    
    idler = read_u64(h, base + 0x3287900)
    print(f"InGameIdler: 0x{idler:X}")
    
    sit_view = read_u64(h, idler + 0xD30)
    print(f"CSituationLogView: 0x{sit_view:X}")
    if sit_view:
        vt = read_u64(h, sit_view)
        print(f"  vtable: 0x{vt:X} (RVA 0x{vt - base:X})")
        # Let's inspect pointers in CSituationLogView
        for off in range(0, 0x300, 8):
            val = read_u64(h, sit_view + off)
            if val and val > 0x10000:
                # check if val points to a vtable or heap object
                val_vt = read_u64(h, val)
                vt_rva_str = f" (target vt RVA 0x{val_vt - base:X})" if val_vt and base < val_vt < base + 0x3000000 else ""
                # check if val points to a pdx string
                s = read_pdx_str(h, sit_view + off)
                str_info = f" str='{s}'" if s else ""
                print(f"  +0x{off:03X}: 0x{val:X}{vt_rva_str}{str_info}")

if __name__ == '__main__':
    main()
