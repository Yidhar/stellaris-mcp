import pefile, struct

pe = pefile.PE(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", fast_load=True)
data = open(r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe", "rb").read()

def offset_to_va(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return 0x140000000 + sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

def find_str_refs(s):
    pos = 0
    str_vas = []
    bs = s.encode('ascii') + b'\0'
    while True:
        pos = data.find(bs, pos)
        if pos == -1: break
        va = offset_to_va(pos)
        if va: str_vas.append(va)
        pos += 1
    
    text_sec = pe.sections[0]
    text_data = data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
    
    refs = []
    for sva in str_vas:
        for i in range(len(text_data) - 7):
            if text_data[i] == 0x48 and text_data[i+1] == 0x8d: # lea reg, [rip + disp]
                disp = struct.unpack('<i', text_data[i+3:i+7])[0]
                insn_va = 0x140000000 + text_sec.VirtualAddress + i
                dest_va = insn_va + 7 + disp
                if dest_va == sva:
                    refs.append(insn_va)
    return str_vas, refs

for s in ['create_or_update_ship_design_chain', 'ship_designer', 'auto_ship_designs']:
    vas, refs = find_str_refs(s)
    print(f'String "{s}": string VAs: {[hex(v) for v in vas]}, refs in .text: {[hex(r) for r in refs]}')
