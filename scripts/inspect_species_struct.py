import sys
import os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import ctypes
import struct

pid = 87664
base = 0x7FF777BD0000
hProc = ctypes.windll.kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    r = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(r)):
        return buf.raw
    return None

def read_u64(addr):
    raw = read_bytes(addr, 8)
    return struct.unpack('<Q', raw)[0] if raw else 0

def read_u32(addr):
    raw = read_bytes(addr, 4)
    return struct.unpack('<I', raw)[0] if raw else 0

pSpecies = 0x200E6B98580

def read_pdx_string(addr):
    sz = read_u64(addr + 16)
    cap = read_u64(addr + 24)
    if sz == 0 or sz > 1024:
        return ""
    if cap < 16:
        raw = read_bytes(addr, 16)
    else:
        ptr = read_u64(addr)
        raw = read_bytes(ptr, sz)
    if raw:
        return raw.split(b'\x00')[0].decode('latin-1', errors='ignore')
    return ""

import win32process, win32api
hProc = win32api.OpenProcess(0x1F0FFF, False, 87664)

def r64(addr):
    try:
        raw = win32process.ReadProcessMemory(hProc, addr, 8)
        return struct.unpack('<Q', raw)[0]
    except: return 0

def r32(addr):
    try:
        raw = win32process.ReadProcessMemory(hProc, addr, 4)
        return struct.unpack('<I', raw)[0]
    except: return 0

def rbytes(addr, sz):
    try:
        return win32process.ReadProcessMemory(hProc, addr, sz)
    except: return None

def rpdx(addr):
    sz = r64(addr + 16)
    cap = r64(addr + 24)
    if sz == 0 or sz > 1024: return ''
    if cap < 16:
        raw = rbytes(addr, 16)
        if raw: return raw[:sz].decode('latin-1', errors='ignore')
    else:
        ptr = r64(addr)
        if ptr:
            raw = rbytes(ptr, sz)
            if raw: return raw[:sz].decode('latin-1', errors='ignore')
    return ''

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    bin_data = f.read()

import pefile, capstone
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
text_sec = pe.sections[0]
rdata_sec = pe.sections[1]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
code_off = text_sec.PointerToRawData + (0xC95E00 - text_sec.VirtualAddress)
code = bin_data[code_off : code_off + 0x600]

for insn in md.disasm(code, 0xC95E00):
    if insn.mnemonic == 'lea' and 'rip +' in insn.op_str:
        try:
            disp = int(insn.op_str.split('rip + ')[1].rstrip(']'), 16)
            dest = insn.address + insn.size + disp
            if rdata_sec.VirtualAddress <= dest < rdata_sec.VirtualAddress + rdata_sec.Misc_VirtualSize:
                off = rdata_sec.PointerToRawData + (dest - rdata_sec.VirtualAddress)
                s = bin_data[off:off+40].split(b'\x00')[0]
                if any(32 <= b < 127 for b in s) and len(s) >= 3:
                    print(f"0x{insn.address:X}: string '{s.decode('latin-1')}'")
        except: pass















