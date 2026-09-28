import ctypes

kernel32 = ctypes.windll.kernel32
pid = 112456
h_proc = kernel32.OpenProcess(0x1F0FFF, False, pid)
base = 0x7FF63B3E0000

target = b"Couldn't find command "
chunk_sz = 1024 * 1024 * 4
buf = (ctypes.c_char * chunk_sz)()
n = ctypes.c_size_t()

found_str = []
for offset in range(0, 80 * 1024 * 1024, chunk_sz):
    addr = base + offset
    if kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, chunk_sz, ctypes.byref(n)):
        data = bytes(buf[:n.value])
        idx = 0
        while True:
            idx = data.find(target, idx)
            if idx == -1: break
            str_va = addr + idx
            found_str.append(str_va)
            print(f'Found string at VA {hex(str_va)} (RVA {hex(str_va - base)})')
            idx += len(target)

if found_str:
    for s_va in found_str:
        for offset in range(0, 50 * 1024 * 1024, chunk_sz):
            addr = base + offset
            if kernel32.ReadProcessMemory(h_proc, ctypes.c_void_p(addr), buf, chunk_sz, ctypes.byref(n)):
                data = bytes(buf[:n.value])
                for i in range(len(data) - 7):
                    if data[i] == 0x48 and data[i+1] == 0x8d:
                        disp = int.from_bytes(data[i+3:i+7], byteorder='little', signed=True)
                        target_addr = addr + i + 7 + disp
                        if target_addr == s_va:
                            print(f'Reference found at VA {hex(addr + i)} (RVA {hex(addr + i - base)})')
