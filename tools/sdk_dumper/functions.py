"""Stage 2c: locate engine functions that the bridge calls directly, by structural fingerprint.

Some values are computed rather than stored (e.g. an agenda's cost scales with empire size), so
the bridge has to call the engine function. Each entry below describes the function the way the
Linux decompile shows it: an ordered list of instruction patterns that must appear within the
first `window` instructions of a 16-byte-aligned function. A fingerprint must match exactly one
function, otherwise the stage fails instead of guessing.

Usage: python tools/sdk_dumper/functions.py   -> out/functions.json
"""
import json
import re
import runpy
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"

wx = runpy.run_path(str(HERE / "win_extract.py"), run_name="sdk_functions")
Image, EXE = wx["Image"], wx["EXE"]

FUNCTIONS = {
    "CCouncilAgenda_GetCost": {
        "linux": "CCouncilAgenda::GetCost(CCountry const*, CString*) const",
        # MSVC: rcx = this, rdx = CFixedPoint* result, r8 = country, r9 = CString* reason
        "signature": "int64_t* (*)(void* agenda, int64_t* out_cost, void* country, void* reason)",
        # empire size * 100000, then modifier scan for 0x13B (size base) and 0x13C (cost mult)
        "pattern": [(r"^imul ", "0x186a0"), (r"^cmp ", "0x13b"), (r"^cmp ", "0x13c")],
        "prefilter": [0x186A0, 0x13B, 0x13C],
        "window": 60,
    },
    "CSpeciesRightBase_IsPotential": {
        "linux": "CSpeciesRightBase::IsPotential(CCountry const*, CSpecies const*) const",
        # this = species right type + 0x40
        "signature": "bool (*)(void* right_base, void* country, void* species)",
        # script-profiler scope name "species_right." + key + ".potential"
        "strings": ["species_right.", ".potential"],
    },
    "CSpeciesRightBase_IsAllowed": {
        "linux": "CSpeciesRightBase::IsAllowed(CCountry const*, CSpecies const*, CString*) const",
        "signature": "bool (*)(void* right_base, void* country, void* species, void* reason_cstring)",
        "strings": ["species_right.", ".allow"],
    },
    "CRT_purecall": {
        "linux": "__cxa_pure_virtual (MSVC: _purecall)",
        "signature": "void (*)()  -- calls the registered purecall handler, then abort()",
        # the static CRT's _purecall: fill for every pure slot in abstract vtables
        "mnemonics": ["sub", "call", "test", "je", "call", "call"],
        "min_rdata_refs": 100,
    },
}


def match_by_mnemonics(im, spec):
    """Functions referenced from .rdata at least min_rdata_refs times whose first
    instructions have exactly the given mnemonics."""
    counts = {}
    rd = im.img[im.rdata0:im.rdata1]
    for off in range(0, len(rd) - 8, 8):
        v = struct.unpack_from("<Q", rd, off)[0] - im.ib
        if im.text0 <= v < im.text1:
            counts[v] = counts.get(v, 0) + 1
    out = []
    want = spec["mnemonics"]
    for f, n in counts.items():
        if n < spec["min_rdata_refs"] or f % 16:
            continue
        ins = im.disasm_fn(f, 0x40)[:len(want)]
        if [i.mnemonic for i in ins] == want:
            out.append(f)
    return out


def match_by_strings(im, spec):
    """Functions that reference every given C string through a rip-relative operand."""
    import numpy as np
    t = np.frombuffer(im.img[im.text0:im.text1], dtype=np.uint8)
    n = len(t) - 4
    disp = (t[:n].astype(np.int64) | (t[1:n + 1].astype(np.int64) << 8) |
            (t[2:n + 2].astype(np.int64) << 16) | (t[3:n + 3].astype(np.int64) << 24))
    disp = np.where(disp >= 2 ** 31, disp - 2 ** 32, disp)
    # a disp32 at the end of the instruction (lea/mov reg, [rip+x]) points at insn_end + disp
    target = np.arange(n, dtype=np.int64) + im.text0 + 4 + disp
    sets = []
    for s in spec["strings"]:
        rvas = [m.start() for m in re.finditer(re.escape(s.encode() + b"\x00"), im.img)
                if im.rdata0 <= m.start() < im.rdata1]
        hits = np.nonzero(np.isin(target, rvas))[0]
        sets.append({im.fn_of(im.text0 + int(h)) for h in hits} - {None})
    return sorted(set.intersection(*sets))


def main():
    im = Image(EXE)
    text = im.img[im.text0:im.text1]
    result, failures = {}, []
    for name, spec in FUNCTIONS.items():
        if "mnemonics" in spec or "strings" in spec:
            matches = match_by_mnemonics(im, spec) if "mnemonics" in spec else match_by_strings(im, spec)
            if len(matches) == 1:
                result[name] = {"rva": matches[0], "linux": spec["linux"], "signature": spec["signature"]}
                print(f"{name}: 0x{matches[0]:X}")
            else:
                failures.append(name)
                print(f"{name}: {len(matches)} matches {[hex(m) for m in matches]} -- fingerprint needs updating")
            continue
        # functions containing every prefilter constant as an immediate
        sets = []
        for imm in spec["prefilter"]:
            fns = {im.fn_of(im.text0 + m.start()) for m in re.finditer(re.escape(struct.pack("<I", imm)), text)}
            sets.append(fns - {None})
        cands = set.intersection(*sets)
        matches = []
        for f in sorted(cands):
            if f % 16:
                continue  # MSVC function entries are 16-byte aligned; others are split fragments
            ins = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(f, 0x800)[:spec["window"]]]
            k = 0
            for s in ins:
                rx, needle = spec["pattern"][k]
                if re.match(rx, s) and s.endswith(needle):
                    k += 1
                    if k == len(spec["pattern"]):
                        matches.append(f)
                        break
        if len(matches) == 1:
            result[name] = {"rva": matches[0], "linux": spec["linux"], "signature": spec["signature"]}
            print(f"{name}: 0x{matches[0]:X}")
        else:
            failures.append(name)
            print(f"{name}: {len(matches)} matches {[hex(m) for m in matches]} -- fingerprint needs updating")
    (OUT / "functions.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
