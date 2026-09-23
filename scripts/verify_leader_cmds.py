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

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

def disasm_func(rva, max_len=64):
    off = rva_to_offset(rva)
    code = data[off : off + max_len]
    print(f"\nDisassembly of RVA 0x{rva:X}:")
    for insn in md.disasm(code, 0x140000000 + rva):
        print(f"  0x{insn.address:X}:  {insn.mnemonic:8s} {insn.op_str}")

# Find GetType functions for 0x4074 (dismiss_leader) and 0x4076 (assign_leader)
# 0x4074: B8 74 40 00 00 C3
# 0x4076: B8 76 40 00 00 C3
for name, cid in [("hire_leader", 0x4073), ("dismiss_leader", 0x4074), ("assign_leader", 0x4076)]:
    pattern = struct.pack('<BI B', 0xB8, cid, 0xC3)
    pos = 0
    get_type_rvas = []
    while True:
        idx = data.find(pattern, pos)
        if idx == -1: break
        rva = offset_to_rva(idx)
        get_type_rvas.append(rva)
        pos = idx + 1
    print(f"\n=== {name} (0x{cid:04X}) ===")
    print(f"GetType function RVAs: {[hex(x) for x in get_type_rvas]}")
    
    for g_rva in get_type_rvas:
        # Find vtable pointing to this GetType function
        g_va = 0x140000000 + g_rva
        p = struct.pack('<Q', g_va)
        vt_idx = data.find(p)
        if vt_idx != -1:
            vt_entry_rva = offset_to_rva(vt_idx)
            # vt_entry_rva is vfunc[10]
            vt_start_rva = vt_entry_rva - 10 * 8
            print(f"Found Vtable for {name}:")
            print(f"  vfunc[10] RVA: 0x{vt_entry_rva:X}")
            print(f"  Vtable base RVA: 0x{vt_start_rva:X}")
            
            # Print vfunc[8] and vfunc[9] (Execute)
            exec_off = rva_to_offset(vt_start_rva + 9 * 8)
            exec_va = struct.unpack('<Q', data[exec_off : exec_off + 8])[0]
            exec_rva = exec_va - 0x140000000
            print(f"  Execute func: VA 0x{exec_va:X} (RVA 0x{exec_rva:X})")
            disasm_func(exec_rva, 64)

