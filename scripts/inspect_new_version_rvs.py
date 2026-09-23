import pefile
import capstone
import struct
import ctypes
from ctypes import wintypes

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
data = open(exe_path, "rb").read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# 1. Connect to live game PID 99112 to read runtime initialized state
pid = 99112
h_proc = ctypes.windll.kernel32.OpenProcess(0x0010, False, pid) # PROCESS_VM_READ

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wintypes.DWORD),
        ('th32ModuleID', wintypes.DWORD),
        ('th32ProcessID', wintypes.DWORD),
        ('GlblcntUsage', wintypes.DWORD),
        ('ProccntUsage', wintypes.DWORD),
        ('modBaseAddr', ctypes.c_void_p),
        ('modBaseSize', wintypes.DWORD),
        ('hModule', wintypes.HMODULE),
        ('szModule', ctypes.c_char * 256),
        ('szExePath', ctypes.c_char * 260)
    ]

h_snap = ctypes.windll.kernel32.CreateToolhelp32Snapshot(0x00000008, pid)
me = MODULEENTRY32()
me.dwSize = ctypes.sizeof(MODULEENTRY32)
live_base = 0
if ctypes.windll.kernel32.Module32First(h_snap, ctypes.byref(me)):
    live_base = me.modBaseAddr
ctypes.windll.kernel32.CloseHandle(h_snap)

print(f"Live Process PID {pid} Base Address: 0x{live_base:X}")

def read_live_u64(addr):
    buf = ctypes.c_uint64()
    read = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), ctypes.byref(buf), 8, ctypes.byref(read)):
        return buf.value
    return 0

def read_live_bytes(addr, sz):
    buf = (ctypes.c_char * sz)()
    read = ctypes.c_size_t()
    if ctypes.windll.kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, sz, ctypes.byref(read)):
        return bytes(buf)
    return b""

# Parse registered commands
# Patterns in .text around 0x180000 - 0x1B0000:
# lea r8, [rip + string]
# mov edx, <cmd_id>
# lea rcx, [rip + desc]
# call register_fn

text_sec = pe.sections[0]
text_start = text_sec.PointerToRawData
text_rva = text_sec.VirtualAddress

start_rva = 0x180000
length = 0x30000
start_off = text_start + (start_rva - text_rva)
code = data[start_off:start_off + length]

cur_str = None
cur_name_rva = None
commands = {}

for insn in md.disasm(code, start_rva):
    if insn.mnemonic == 'lea' and insn.op_str.startswith('r8, [rip + '):
        disp = int(insn.op_str.split(' + ')[1].rstrip(']'), 16)
        str_rva = insn.address + insn.size + disp
        cur_name_rva = str_rva
        for s in pe.sections:
            if s.VirtualAddress <= str_rva < s.VirtualAddress + s.SizeOfRawData:
                off = s.PointerToRawData + (str_rva - s.VirtualAddress)
                cur_str = data[off:off+100].split(b'\x00')[0].decode('latin-1', errors='ignore')
                break
    elif insn.mnemonic == 'mov' and insn.op_str.startswith('edx, '):
        try:
            cmd_id = int(insn.op_str.split(', ')[1], 16)
            commands[insn.address] = {'id': cmd_id, 'name': cur_str, 'name_rva': cur_name_rva}
            cur_str = None
        except:
            pass
    elif insn.mnemonic == 'lea' and insn.op_str.startswith('rcx, [rip + '):
        disp = int(insn.op_str.split(' + ')[1].rstrip(']'), 16)
        desc_rva = insn.address + insn.size + disp
        # Look for the last command registered
        for addr in sorted(commands.keys(), reverse=True):
            if addr < insn.address and 'desc_rva' not in commands[addr]:
                commands[addr]['desc_rva'] = desc_rva
                break

print(f"Parsed {len(commands)} commands.")

# Filter target commands
targets = [
    'hire_leader', 'dismiss_leader', 'assign_leader_command',
    'set_situation_approach_command', 'early_finish_agenda_command',
    'activate_tradition_command', 'add_edict_command', 'remove_edict_command',
    'cancel_research_technology_command', 'pausegame', 'pauselocked'
]

for addr, info in sorted(commands.items()):
    name = info.get('name')
    if name in targets:
        cid = info['id']
        desc_rva = info.get('desc_rva')
        desc_str = f"0x{desc_rva:X}" if desc_rva else "None"
        print(f"\nTarget Command: '{name}' | ID: 0x{cid:04X} ({cid}) | Desc RVA: {desc_str}")
        if desc_rva and live_base:
            desc_va = live_base + desc_rva
            raw = read_live_bytes(desc_va, 64)
            for i in range(0, len(raw), 8):
                q = struct.unpack('<Q', raw[i:i+8])[0]
                q_rva = q - live_base if q > live_base else q
                print(f"    +{hex(i)}: 0x{q:016X} (RVA: 0x{q_rva:X})")

