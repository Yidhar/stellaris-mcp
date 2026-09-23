import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

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

# Dump vtables around 0x2530E00 - 0x2531200
# Each command vtable has ~16-20 entries
# Let's find each occurrence of 0x141BEE990 (vfunc[7] common destructor/cleanup)
common_fn = 0x141BEE990
p = struct.pack('<Q', common_fn)

pos = rva_to_offset(0x2530000)
end_pos = rva_to_offset(0x2532000)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

while pos < end_pos:
    idx = data.find(p, pos, end_pos)
    if idx == -1:
        break
    # vfunc[7] is at idx
    # vtable base is idx - 7 * 8
    vt_base_off = idx - 7 * 8
    vt_base_rva = offset_to_rva(vt_base_off)
    
    # Check vfunc[10] (GetType)
    get_type_ptr = struct.unpack('<Q', data[vt_base_off + 10*8 : vt_base_off + 11*8])[0]
    get_type_rva = get_type_ptr - 0x140000000
    get_type_off = rva_to_offset(get_type_rva)
    
    cmd_id = None
    if get_type_off:
        code = data[get_type_off : get_type_off + 16]
        for insn in md.disasm(code, get_type_ptr):
            if insn.mnemonic == 'mov' and insn.op_str.startswith('eax, '):
                try:
                    cmd_id = int(insn.op_str.split(', ')[1], 16)
                except Exception:
                    pass
                break
    
    exec_ptr = struct.unpack('<Q', data[vt_base_off + 9*8 : vt_base_off + 10*8])[0]
    exec_rva = exec_ptr - 0x140000000
    
    print(f"Vtable RVA: 0x{vt_base_rva:X} | Cmd ID: {hex(cmd_id) if cmd_id else 'None'} ({cmd_id}) | Exec RVA: 0x{exec_rva:X}")
    pos = idx + 8

