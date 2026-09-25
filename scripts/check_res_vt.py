import pefile
import struct
import capstone

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

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# CStrategicResource Vtable
# In res_db + 0x08, array of CStrategicResource*
# Let's inspect the vtable from PE
# In disasm_add_monthly_fields.py:
# 0x1D10970: mov rax, qword ptr [r14]; call qword ptr [rax + 0x38]
# Let's find CStrategicResource vtable
vt_rva = 0x2370A60 # or find it from binary
# Let's read from memory via inspect_strat_vt.py
