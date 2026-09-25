import sys
sys.path.append(r'D:\stellarismcp\scripts')
import capstone, ctypes, reload_dll, inject

pid = inject.find_stellaris_pid()
base = reload_dll.find_module(pid, 'stellaris.exe')
kernel32 = ctypes.windll.kernel32
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def read_str_at(addr):
    buf = (ctypes.c_char * 64)()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, 64, None)
    return bytes(buf).split(b'\x00')[0].decode('utf-8', errors='ignore')

targets = {
    0: 0x14D8662,
    1: 0x14D8AD6,
    2: 0x14D8746,
    3: 0x14D7F9F,
    4: 0x14D83B6,
    5: 0x14D882A,
    6: 0x14D890E,
    7: 0x14D89F2,
    10: 0x14D8BBA,
    11: 0x14D81EE,
    12: 0x14D82D2,
    13: 0x14D849A,
    14: 0x14D857E
}

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for case_idx, target_rva in targets.items():
    buf = (ctypes.c_char * 120)()
    kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(base + target_rva), buf, 120, None)
    print(f"=== Case {case_idx} (0x{target_rva:X}) ===")
    for insn in md.disasm(bytes(buf), base + target_rva):
        if 'rip +' in insn.op_str:
            parts = insn.op_str.split('rip +')
            val = parts[1].split(']')[0].strip()
            disp = int(val, 16)
            str_addr = insn.address + insn.size + disp
            s = read_str_at(str_addr)
            if s and len(s) >= 2:
                print(f"  0x{insn.address-base:X}: {insn.mnemonic} {insn.op_str} -> '{s}'")
