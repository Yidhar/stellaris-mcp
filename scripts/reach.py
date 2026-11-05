"""reach.py <rva>...: every way the image reaches each RVA: direct `call rel32` (E8) and `jmp rel32` (E9) sites in .text
with their containing function, and every 8-byte occurrence of the absolute address anywhere in the image (vtable slots),
with the slot index guessed by walking back over pointers into .text."""
import runpy
import struct
import sys

ARGS = sys.argv[1:]
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
ib = im.ib
img = im.img
lo, hi = im.text0, im.text1
targets = {int(a, 16): {"call": [], "jmp": [], "abs": []} for a in ARGS}
for op, key in ((b"\xe8", "call"), (b"\xe9", "jmp")):
    pos = img.find(op, lo)
    while pos != -1 and pos < hi - 5:
        rel = struct.unpack_from("<i", img, pos + 1)[0]
        t = pos + 5 + rel
        if t in targets:
            targets[t][key].append(pos)
        pos = img.find(op, pos + 1)
for t in targets:
    needle = struct.pack("<Q", ib + t)
    pos = img.find(needle)
    while pos != -1:
        targets[t]["abs"].append(pos)
        pos = img.find(needle, pos + 1)


def is_code(v):
    return ib + lo <= v < ib + hi


for t, d in targets.items():
    print(f"=== {t:#x}")
    for s in d["call"]:
        print(f"    call site {s:#x} in function {im.fn_of(s):#x}")
    for s in d["jmp"]:
        print(f"    jmp site {s:#x} in function {im.fn_of(s):#x}")
    for p in d["abs"]:
        # walk back while the previous qwords are code pointers
        start = p
        while start - 8 >= 0 and is_code(struct.unpack_from("<Q", img, start - 8)[0]):
            start -= 8
        slot = (p - start) // 8
        # count forward
        end = p
        while end + 8 <= len(img) and is_code(struct.unpack_from("<Q", img, end + 8)[0]):
            end += 8
        n = (end - start) // 8 + 1
        rtti = struct.unpack_from("<Q", img, start - 8)[0] if start >= 8 else 0
        print(f"    abs at {p:#x}: vtable {start:#x} slot {slot} of {n}; qword before vtable {rtti:#x}")
        for k in range(max(0, slot - 2), min(n, slot + 3)):
            v = struct.unpack_from("<Q", img, start + k * 8)[0]
            print(f"        [{k}] {v - ib:#x}")
