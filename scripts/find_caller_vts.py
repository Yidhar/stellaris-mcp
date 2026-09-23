import pefile, struct, capstone

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def offset_to_rva(offset):
    for sec in pe.sections:
        if sec.PointerToRawData <= offset < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (offset - sec.PointerToRawData)
    return None

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

target_rva = 0x648970
text_sec = pe.sections[0]
code = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Look for call rel32 to target_rva
callers = []
for i in range(len(code) - 5):
    if code[i] == 0xE8: # call rel32
        disp = struct.unpack('<i', code[i+1:i+5])[0]
        curr_rva = text_sec.VirtualAddress + i + 5
        if curr_rva + disp == target_rva:
            callers.append(curr_rva - 5)

print(f"Total callers: {len(callers)}")

# Find any callers where a vtable in .rdata (RVA 0x24D0000 - 0x2700000) is loaded via lea/mov
found_cmds = []
for c_rva in callers:
    off = rva_to_offset(c_rva)
    block_start = max(0, off - text_sec.PointerToRawData - 120)
    block_end = off - text_sec.PointerToRawData
    block = code[block_start : block_end]
    
    insns = list(md.disasm(block, 0x140000000 + text_sec.VirtualAddress + block_start))
    vt_loaded = None
    for ins in insns:
        if ins.mnemonic == 'lea' and 'rip +' in ins.op_str:
            disp = int(ins.op_str.split('rip + ')[1].rstrip(']'), 16)
            target_va = ins.address + ins.size + disp
            target_rva = target_va - 0x140000000
            if 0x24D0000 <= target_rva <= 0x2700000:
                vt_loaded = target_rva
    if vt_loaded:
        found_cmds.append((c_rva, vt_loaded))

print(f"Found {len(found_cmds)} callers with identified command vtable:")
# Deduplicate by vtable
seen_vts = set()
for c_rva, vt_rva in found_cmds:
    if vt_rva not in seen_vts:
        seen_vts.add(vt_rva)
        # Check vfunc[10] of this vtable to get command ID
        vt_off = rva_to_offset(vt_rva)
        if vt_off:
            gt_ptr = struct.unpack('<Q', data[vt_off + 10*8 : vt_off + 11*8])[0]
            gt_rva = gt_ptr - 0x140000000
            gt_off = rva_to_offset(gt_rva)
            cmd_id = None
            if gt_off and gt_off < len(data) - 16:
                for ins in md.disasm(data[gt_off:gt_off+16], gt_ptr):
                    if ins.mnemonic == 'mov' and ins.op_str.startswith('eax, '):
                        try:
                            cmd_id = int(ins.op_str.split(', ')[1], 16)
                        except Exception:
                            pass
                        break
            # Print if cmd_id matches any of our leader keywords or is around 0x4000
            cid_str = f"0x{cmd_id:04X}" if cmd_id else "None"
            print(f"  Caller RVA: 0x{c_rva:X} | Vtable RVA: 0x{vt_rva:X} | Cmd ID: {cid_str}")

