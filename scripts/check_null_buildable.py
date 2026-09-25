import ctypes
import win32process
import win32api
import psutil
import struct

pid = None
for p in psutil.process_iter(['pid', 'name']):
    if p.info['name'] and p.info['name'].lower() == 'stellaris.exe':
        pid = p.info['pid']
        break

h_process = win32api.OpenProcess(0x0400 | 0x0010, False, pid)
base = win32process.EnumProcessModules(h_process)[0]
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_bytes(addr, size):
    buf = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    kernel32.ReadProcessMemory(int(h_process), ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    return buf.raw[:read.value]

# In CAddBuildableToQueueCommand::ExecuteLocal:
# TPdxNullObject<CNullBuildable>::_pInstance
# Let's search for references to 0x154D80 or CNullBuildable methods (like 0x159E00)
# Or let's search for CNullBuildable::CalcCost in .rdata
# In CNullBuildable, CalcCost is an empty function that returns!
# Let's find TPdxNullObject<CNullBuildable>::_pInstance
# In ExecuteLocal (line 2736087):
# cmp plStack_18, TPdxNullObject<CNullBuildable>::_pInstance

# Let's disasm ExecuteLocal around 0xACC540
import capstone
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Where was ExecuteLocal? Let's check cmd_vt[9] or cmd_vt[10]
# cmd_vt[9] was 0xACC4C0
code = read_bytes(base + 0xACC4C0, 0x100)
print("cmd_vt[9] disasm:")
for i in cs.disasm(code, base + 0xACC4C0):
    print(f"0x{i.address - base:X}: {i.mnemonic:8s} {i.op_str}")

