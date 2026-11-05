"""scan_all.py <substring>...: every enclosing function (Linux decompile) that contains each substring, with the hit
count per function, sorted by count."""
import re
import sys
from collections import Counter

SRC = r"D:\stellarismcp\source\stellaris_4.5_source.cpp"
pats = [p.encode() for p in sys.argv[1:]]
hdr_re = re.compile(rb"^[A-Za-z_].*\(")
last_hdr = (0, b"")
hits = {p: Counter() for p in pats}
with open(SRC, "rb") as f:
    for no, line in enumerate(f, 1):
        if hdr_re.match(line):
            last_hdr = (no, line.rstrip()[:140])
        for p in pats:
            if p in line and not hdr_re.match(line):
                hits[p][last_hdr] += 1
for p in pats:
    print(f"=== {p.decode()}: {sum(hits[p].values())} hits in {len(hits[p])} functions")
    for (hno, hline), n in sorted(hits[p].items(), key=lambda kv: -kv[1])[:80]:
        print(f"  {n:>3}  @{hno}: {hline.decode('latin1')}")
