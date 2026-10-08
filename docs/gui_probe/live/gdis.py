"""dis.py <rva hex> [limit hex]: disassemble a function of the Windows image with string/call annotations."""
import re
import runpy
import sys

args = sys.argv[1:]
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
start = int(args[0], 16)
limit = int(args[1], 16) if len(args) > 1 else 0x1000
ib = im.ib


def annotate(ins):
    t = im.rip_target(ins)
    if t is None:
        return ""
    if ins.mnemonic == "lea" and 0 < t < len(im.img):
        s = im.img[t:t + 80].split(b"\0")[0]
        if len(s) >= 4 and all(32 <= c < 127 or c >= 128 for c in s):
            return f'   ; "{s.decode("latin1")}"'
        return f"   ; data 0x{t:X}"
    return f"   ; [0x{t:X}]"


for ins in im.disasm_fn(start, limit):
    rva = ins.address - ib
    extra = annotate(ins)
    if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
        extra = f"   ; -> fn 0x{int(ins.op_str, 16) - ib:X}"
    print(f"{rva:8X}  {ins.mnemonic:6s} {ins.op_str}{extra}")
