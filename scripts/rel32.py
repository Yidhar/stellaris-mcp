"""rel32.py <rva>...: every rel32 reference in .text that resolves to the RVA (call, jmp, lea/mov rip-relative with the
displacement as the last 4 bytes of the instruction), with the containing function and the bytes before it."""
import runpy
import sys

import numpy as np

ARGS = sys.argv[1:]
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
img = im.img
lo, hi = im.text0, im.text1
buf = np.frombuffer(img[lo:hi], dtype=np.uint8)
n = len(buf) - 4
# little-endian int32 at every byte offset
v = (buf[0:n].astype(np.int64) | (buf[1:n + 1].astype(np.int64) << 8) | (buf[2:n + 2].astype(np.int64) << 16) |
     (buf[3:n + 3].astype(np.int64) << 24))
v = np.where(v >= 1 << 31, v - (1 << 32), v)
pos = np.arange(n, dtype=np.int64) + lo
resolved = pos + 4 + v
for a in ARGS:
    t = int(a, 16)
    hits = np.nonzero(resolved == t)[0]
    print(f"=== {t:#x}: {len(hits)} rel32 references")
    for h in hits[:40]:
        p = int(pos[h])
        before = " ".join(f"{b:02X}" for b in img[p - 8:p])
        after = " ".join(f"{b:02X}" for b in img[p:p + 6])
        print(f"    disp at {p:#x} in function {im.fn_of(p):#x}: ...{before} | {after}")
