"""rtti_vt.py <class name substring> [slots]: the vtables of every class whose MSVC RTTI type descriptor name contains the
substring (Windows 4.5.2 exe), found through the complete object locators, with the first slots as RVAs."""
import re
import runpy
import struct
import sys

ARGS = sys.argv[1:]
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
ib = im.ib
img = im.img
sub = ARGS[0].encode()
nslots = int(ARGS[1]) if len(ARGS) > 1 else 8
lo, hi = im.text0, im.text1


def is_code(v):
    return ib + lo <= v < ib + hi


for m in re.finditer(rb"\.\?AV[A-Za-z0-9_@?$]*", img):
    name = m.group(0)
    if sub not in name:
        continue
    td = m.start() - 0x10  # TypeDescriptor: vtable ptr, spare, name
    # complete object locators referencing this descriptor (x64: RVAs); signature 1
    needle = struct.pack("<I", td)
    pos = img.find(needle)
    found = []
    while pos != -1:
        col = pos - 12
        if col >= 0 and struct.unpack_from("<I", img, col)[0] == 1 and struct.unpack_from("<I", img, col + 20)[0] == col:
            found.append(col)
        pos = img.find(needle, pos + 1)
    for col in found:
        off = struct.unpack_from("<I", img, col + 4)[0]
        # the vtable: the qword before it holds the absolute address of the COL
        vneedle = struct.pack("<Q", ib + col)
        vpos = img.find(vneedle)
        while vpos != -1:
            vt = vpos + 8
            slots = []
            for i in range(nslots):
                v = struct.unpack_from("<Q", img, vt + i * 8)[0]
                if not is_code(v):
                    break
                slots.append(v - ib)
            print(f"{name.decode()}: vtable {vt:#x} (offset {off}) slots " + " ".join(f"[{i}]={s:#x}" for i, s in enumerate(slots)))
            vpos = img.find(vneedle, vpos + 1)
