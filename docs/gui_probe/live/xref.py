"""xref.py <string> [<string> ...]: for each string in the Windows image, list the functions that lea it (RVA, function start)."""
import re
import runpy
import sys

args = sys.argv[1:]
sys.argv = sys.argv[:1]  # win_extract reads argv[1] as the exe path
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
lea = im.lea_index()
for s in args:
    needle = s.encode()
    pos = [m.start() for m in re.finditer(re.escape(needle), im.img)]
    print(f"== {s!r}: {len(pos)} occurrence(s)")
    for p in pos:
        start = im.img.rfind(b"\0", 0, p) + 1
        for ref in lea.get(start, []):
            f = im.primary(im.fn_of(ref))
            print(f"   string@0x{start:X} <- lea@0x{ref:X} in fn 0x{f:X}")
