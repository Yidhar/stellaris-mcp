"""Find functions that read io.WantCaptureMouse/Keyboard (byte at +0x38C/+0x38D) and call GetIO (0x1E95470)."""
import re
import runpy
import sys
import collections

sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
text = im.img[im.text0:im.text1]
hits = collections.defaultdict(list)
# cmp byte ptr [reg+0x38c], 0  ->  80 /7 disp32 imm8 ; also movzx/mov byte loads
for m in re.finditer(rb"\x80[\xB8-\xBF]\x8C\x03\x00\x00\x00|\x80[\xB8-\xBF]\x8D\x03\x00\x00\x00|\x0F\xB6[\x80-\xBF]\x8C\x03\x00\x00|\x0F\xB6[\x80-\xBF]\x8D\x03\x00\x00|\x8A[\x80-\xBF]\x8C\x03\x00\x00|\x8A[\x80-\xBF]\x8D\x03\x00\x00", text):
    rva = im.text0 + m.start()
    f = im.primary(im.fn_of(rva))
    hits[f].append((rva, text[m.start():m.end()].hex()))
print(len(hits), "functions read +0x38C/+0x38D")
for f, v in sorted(hits.items()):
    calls_getio = False
    for ins in im.disasm_fn(f, 0x1200):
        if ins.mnemonic == "call" and ins.op_str.startswith("0x") and int(ins.op_str, 16) - im.ib == 0x1E95470:
            calls_getio = True
            break
    print(f"fn 0x{f:X}  sites={[hex(r) for r, _ in v][:4]}  calls GetIO={calls_getio}")
