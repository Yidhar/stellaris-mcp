from dump_sit_window import *

h = kernel32.OpenProcess(0x10, False, 73956)
base = 0x7FF75ED50000
idler = r64(base + 0x3287900)
sit_view = r64(idler + 0xD30)
ui_win = r64(sit_view + 0x78)
c_arr = r64(ui_win + 0x878)
c_cnt = r32(ui_win + 0x884)
print(f"UI Win 0x{ui_win:X} has {c_cnt} children:")
for j in range(c_cnt):
    child = r64(c_arr + j * 8)
    name = extract_str(child + 0x18)
    vis = r8(child + 0x41)
    print(f"  Child[{j}]: 0x{child:X} name='{name}' vis={vis}")
