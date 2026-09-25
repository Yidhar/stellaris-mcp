import pefile
import struct

exe_path = r"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe"
pe = pefile.PE(exe_path, fast_load=True)
data = open(exe_path, "rb").read()

def rva_to_offset(rva):
    for s in pe.sections:
        va = s.VirtualAddress
        sz = s.Misc_VirtualSize
        if va <= rva < va + sz:
            return rva - va + s.PointerToRawData
    return None

def get_rtti_type(rva):
    off = rva_to_offset(rva)
    if off is None: return "Invalid RVA"
    col_ptr = struct.unpack_from("<Q", data, off - 8)[0]
    col_rva = col_ptr - pe.OPTIONAL_HEADER.ImageBase
    col_off = rva_to_offset(col_rva)
    if col_off is None: return "Invalid COL RVA"
    td_rva = struct.unpack_from("<I", data, col_off + 12)[0]
    td_off = rva_to_offset(td_rva)
    if td_off is None: return "Invalid TD RVA"
    name = data[td_off+16:td_off+100].split(b'\0')[0].decode('ascii', errors='ignore')
    return name

targets = [
    (0x2390F30, "0x2390F30 (CBuildableClearDepositBlocker?)"),
    (0x238F7E8, "0x238F7E8 (db1 slot 0)"),
    (0x23D8BC8, "0x23D8BC8 (db2 slot 0)"),
    (0x23970A8, "0x23970A8 (db3 slot 0)"),
    (0x23D9198, "0x23D9198 (planets slot 0)"),
    (0x23466C0, "0x23466C0 (db1 mgr)"),
    (0x2347790, "0x2347790 (db2 mgr)"),
    (0x23465E0, "0x23465E0 (db3 mgr)"),
    (0x23477E8, "0x23477E8 (planet mgr)"),
    (0x2347720, "0x2347720 (solar system mgr)"),
]

for rva, desc in targets:
    print(f"{desc} -> {get_rtti_type(rva)}")

