import capstone
import struct

with open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb") as f:
    data = f.read()

def va_to_offset(va):
    rva = va - 0x140000000
    # in PE, .text RVA 0x1000 -> file offset 0x400
    # Let's check sections or exact conversion:
    # 0x1401A1D13 -> file offset 0x1A1113
    return rva - 0xC00

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble from RVA 0x1A1000 to 0x1A4000
# File offset: 0x1A1000 - 0xC00 = 0x1A0400
file_start = 0x1A0400
file_len = 0x5000
code = data[file_start : file_start + file_len]

curr_cmd_id = None
curr_cmd_name = None
curr_rcx = None

commands = []

for insn in md.disasm(code, 0x140000000 + 0x1A1000):
    # mov edx, <cmd_id>
    if insn.mnemonic == 'mov' and insn.op_str.startswith('edx, '):
        try:
            curr_cmd_id = int(insn.op_str.split(', ')[1], 16)
        except Exception:
            pass
    elif insn.mnemonic == 'lea' and insn.op_str.startswith('r8, '):
        # rip-relative string
        target_va = insn.address + insn.size + int(insn.op_str.split('rip + ')[1].rstrip(']'), 16)
        # read string from data
        str_off = target_va - 0x140000000 - 0x1400 # let's check .rdata mapping
        # Let's search string around target_va or use pefile
        curr_cmd_name = target_va
    elif insn.mnemonic == 'lea' and insn.op_str.startswith('rcx, '):
        target_va = insn.address + insn.size + int(insn.op_str.split('rip + ')[1].rstrip(']'), 16)
        curr_rcx = target_va
    elif insn.mnemonic == 'call' and '1d36b40' in insn.op_str:
        commands.append((curr_cmd_id, curr_rcx, curr_cmd_name, insn.address))
        curr_cmd_id = None

print(f"Parsed {len(commands)} registered commands.")

# Resolve strings using pefile
import pefile
pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)

def va_to_data(va):
    rva = va - 0x140000000
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            off = sec.PointerToRawData + (rva - sec.VirtualAddress)
            raw = data[off:off+100]
            s = raw.split(b'\x00')[0]
            return s.decode('latin-1', errors='ignore')
    return ""

def va_to_bytes(va, sz):
    rva = va - 0x140000000
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            off = sec.PointerToRawData + (rva - sec.VirtualAddress)
            return data[off:off+sz]
    return None

for cid, rcx_va, name_va, insn_addr in commands:
    name_str = va_to_data(name_va) if name_va else ""
    if any(k in name_str.lower() for k in ['lead', 'hire', 'dismiss', 'assign', 'fire']):
        # Inspect what is at rcx_va
        raw_rcx = va_to_bytes(rcx_va, 64)
        print(f"Command ID: 0x{cid:04X} ({cid:5d}) | Name: '{name_str:25s}' | RCX: 0x{rcx_va:X}")
        if raw_rcx:
            for i in range(0, len(raw_rcx), 8):
                q = struct.unpack('<Q', raw_rcx[i:i+8])[0]
                print(f"    +{hex(i)}: 0x{q:016X}")

