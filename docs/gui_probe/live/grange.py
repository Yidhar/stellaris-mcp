"""grange.py <start hex> <end hex> [jt <table hex> <count>]: linear disassembly of an RVA range (no function-end logic), annotated."""
import runpy
import struct
import sys

args = sys.argv[1:]
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
ib = im.ib

if args and args[0] == "jt":
    table, count = int(args[1], 16), int(args[2])
    for i in range(count):
        off = struct.unpack_from("<I", im.img, table + i * 4)[0]
        print(f"case {i}: 0x{off:X}")
    sys.exit(0)

start, end = int(args[0], 16), int(args[1], 16)


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


for ins in im.md.disasm(im.img[start:end], ib + start):
    rva = ins.address - ib
    extra = annotate(ins)
    if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
        extra = f"   ; -> 0x{int(ins.op_str, 16) - ib:X}"
    print(f"{rva:8X}  {ins.mnemonic:6s} {ins.op_str}{extra}")
