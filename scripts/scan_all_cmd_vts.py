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

common_fn = 0x141BEE990
p = struct.pack('<Q', common_fn)

# Scan all .rdata
rdata_sec = pe.sections[1]
pos = rdata_sec.PointerToRawData
end_pos = pos + rdata_sec.SizeOfRawData

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

results = []
while pos < end_pos:
    idx = data.find(p, pos, end_pos)
    if idx == -1:
        break
    vt_base_off = idx - 7 * 8
    vt_base_rva = offset_to_rva(vt_base_off)
    
    get_type_ptr = struct.unpack('<Q', data[vt_base_off + 10*8 : vt_base_off + 11*8])[0]
    get_type_rva = get_type_ptr - 0x140000000
    get_type_off = rva_to_offset(get_type_rva)
    
    cmd_id = None
    if get_type_off and 0 <= get_type_off < len(data) - 16:
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
    
    if cmd_id in [0x4073, 0x4074, 0x4075, 0x4076, 0x4292, 0x4005, 0x4070, 0x4084, 0x4279]:
        print(f"FOUND TARGET! Vtable RVA: 0x{vt_base_rva:X} | Cmd ID: 0x{cmd_id:04X} ({cmd_id}) | Exec RVA: 0x{exec_rva:X}")
    elif cmd_id is not None and (0x4000 <= cmd_id <= 0x4300):
        results.append((cmd_id, vt_base_rva, exec_rva))
    pos = idx + 8

print(f"\nTotal commands in 0x4000..0x4300 range: {len(results)}")
for cid, vt, ex in sorted(results):
    print(f"  Cmd ID: 0x{cid:04X} ({cid:5d}) | Vtable: 0x{vt:X} | Exec: 0x{ex:X}")

