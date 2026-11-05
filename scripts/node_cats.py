"""node_cats.py: EModifierNodeCategory number -> owner classes, from the CModifierNode constructor instantiations in the
Linux decompile (the std::function handler names wrap over lines)."""
import mmap
import re
from collections import defaultdict

SRC = r"D:\stellarismcp\source\stellaris_4.5_source.cpp"
pat = re.compile(rb"\(EModifierNodeCategory\)(\d+), NModifierNode::SDependencies<EModifierNodeCategory>\s*>::CModifierNode<(C\w+)")
cats = defaultdict(set)
with open(SRC, "rb") as f:
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    for m in pat.finditer(mm):
        cats[int(m.group(1))].add(m.group(2).decode())
    # the node members declared per class: "CModifierNode<..., (EModifierNodeCategory)N, ...> CX::..." are not in a decompile;
    # also look at GetModifier-style accessors: "(EModifierNodeCategory)N" near a class name in a comment header
    pat2 = re.compile(rb"/\* (C\w+)::\w+\([^)]*\(EModifierNodeCategory\)(\d+)")
    for m in pat2.finditer(mm):
        cats[int(m.group(2))].add("?" + m.group(1).decode())
for c in sorted(cats):
    print(c, sorted(cats[c]))
