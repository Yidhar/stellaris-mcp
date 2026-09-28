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
    # --- events: CEventWindow::Setup / GetToolTip are found by their GUI strings; the engine
    # functions they call are picked by position relative to an anchor instruction.
    "CEvent_GetTitle": {
        "linux": "CEvent::GetTitle(CEventScope const&) const",
        # MSVC: rcx = event, rdx = CString* out, r8 = scope; returns out
        "signature": "void* (*)(void* event, void* out_cstring, void* scope)",
        "call_near": {"strings": ["event_option_offset", "event_window_option_entry", "description_frame"],
                      "anchor": '"Title"', "pick": "last_call_before"},
    },
    "NEventWindowUtil_GetEventWindowDesc": {
        "linux": "NEventWindowUtil::GetEventWindowDesc(CEvent const&, CEventScope const&)",
        "signature": "void* (*)(void* out_cstring, void* event, void* scope)",
        "call_near": {"strings": ["event_option_offset", "event_window_option_entry", "description_frame"],
                      "anchor": '"description_frame"', "pick": "last_call_before"},
    },
    "CEventOption_GetDescForOptionAtIndex": {
        "linux": "CEventOption::GetDescForOptionAtIndex(CEventScope const&, bool, CPdxArray<CEventOption const*> const&, int, CEffect const*, bool)",
        # the effect tooltip of one event option (CEvent::GetEffectDescription is inlined)
        "signature": "void* (*)(void* out_cstring, void* scope, bool, void* options_array, int index, void* effect, bool)",
        "call_near": {"strings": ["event_option_button", "coop_event_option_selected_by_other"],
                      "anchor": r"lea r9, \[rax \+ 0x5e0\]", "pick": "first_call_after"},
    },
    "CEventWindow_PostEventOptionSelection": {
        "linux": "CEventWindow::PostEventOptionSelection(int)",
        # rcx = event window (as listed at [idler + 0x170]); forwards to
        # COpenPlayerEvent::PostEventOptionSelection, then logs the "selected_event_option" analytics event
        "signature": "void (*)(void* event_window, int option_index)",
        "strings": ["selected_event_option"],
        # several event views post option selections; this one looks the open player event up by id
        "require": [r"cmp dword ptr \[r\w+ \+ 0x180\], eax"],
    },
    "COutlinerPlanetStatusController_ShouldShowStatusFrame": {
        "linux": "COutlinerPlanetStatusController::ShouldShowStatusFrame(EOutlinerPlanetStatusFrame, CColony const&, TPdxRef<CCountry>, EPlanetBuildingOwnerType)",
        # rcx = controller (only frame 1 touches it: a growth-data map at +0x10), edx = frame,
        # r8 = colony, r9d = country id, [rsp+0x28] = owner type. Frames: 0 construction slot,
        # 1 clearable blocker, 2 capital upgrade, 5 unemployment, 6 excess civilians,
        # 7 overcrowding, 10 low stability.
        "signature": "bool (*)(void* controller, int frame, void* colony, uint32_t country, int owner_type)",
        # a 15-way jump table whose unemployment case tests colony flag bit 1 (+0x1088)
        "pattern": [(r"^cmp edx", "0xe"), (r"^jmp", "rdx"), (r"^mov eax, dword ptr \[rdi", "0x1088]")],
        "prefilter": [0x1088, 0xFD4],
        "window": 40,
    },
    "PdxLocalize_OneParam": {
        "linux": "PdxLocalize<char const(&)[N], CString const&>(key, param_name, value)",
        # rcx = CString* out, rdx = key view {const char*, int32 len, u8}, r8 = param name
        # (strlen'd at run time, so any name works), r9 = CString const& value
        "signature": "void* (*)(void* out_cstring, const void* key_view, const char* param, const void* value_cstring)",
        "call_near": {"strings": ["OUTLINER_PLANET_BLOCKADED", "OUTLINER_PLANET_OCCUPIED"],
                      "anchor": '"BLOCKADER"', "pick": "first_call_after"},
    },
    "CPersistentName_BuildString": {
        "linux": "CPersistentName::BuildString() const",
        # MSVC: rcx = CPersistentName (e.g. CFleet::name), rdx = CString* out; returns out.
        # Found through CFleet::GetLocalizedName (a CFleet vtable thunk): add rcx, <name - base>;
        # mov rbx, rdx; call BuildString.
        "signature": "void* (*)(const void* persistent_name, void* out_cstring)",
        "pattern": [(r"^add rcx", "0x3a0"), (r"^mov rbx", "rdx"), (r"^call", "")],
        "prefilter": [0x3A0],
        "window": 6,
        "follow_call": True,
    },
    "CArmy_CalcMilitaryPower": {
        "linux": "CArmy::CalcMilitaryPower() const",
        # MSVC: rcx = army, rdx = CFixedPoint* out (x100000); returns out
        "signature": "int64_t* (*)(const void* army, int64_t* out)",
        # CArmy::GetToolTip: power, then its formatting, then "ARMY_MILITARY_POWER_TIP"
        "call_near": {"strings": ["ARMY_MILITARY_POWER_TIP", "ARMY_NO_MORALE", "ARMY_NO_POP_LIMIT"],
                      "anchor": '"ARMY_MILITARY_POWER_TIP"', "pick": "last_call_before", "skip": 1},
    },
    "CArmyType_CalcMorale": {
        "linux": "CArmyType::CalcMorale(CModifier const*) const",
        # MSVC: rcx = army type, rdx = CFixedPoint* out, r8 = the army's CModifier; the army's
        # maximum morale (CArmy::CalcMaxMorale passes CArmy's modifier)
        "signature": "int64_t* (*)(const void* army_type, int64_t* out, const void* modifier)",
        # CArmy::GetToolTip: after the has_morale test, a formatting call, then CalcMorale
        "call_near": {"strings": ["ARMY_MILITARY_POWER_TIP", "ARMY_NO_MORALE", "ARMY_NO_POP_LIMIT"],
                      "anchor": r"cmp byte ptr \[rax \+ 0x355\], 0", "occurrence": 0,
                      "pick": "first_call_after", "skip": 1},
    },
    "CFleet_BuildOrdersString": {
        "linux": "CFleet::BuildOrdersString(bool, bool) const",
        # MSVC: rcx = fleet, rdx = CString* out, r8b, r9b (the outliner fleet entry passes 1, 0);
        # the fleet's current orders as the game describes them ("NO_ORDERS" when idle)
        "signature": "void* (*)(const void* fleet, void* out_cstring, bool, bool)",
        "strings": ["NO_ORDERS", "FLEETORDER_TOP_LEVEL_TEXT", "ORDERS_LEFT"],
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
    found = set()
    for f in set.intersection(*sets):
        entry = aligned_entry(im, f)
        if entry is None:
            continue
        if spec.get("require"):
            text = " | ".join(f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(entry, 0x800))
            if not all(re.search(rx, text) for rx in spec["require"]):
                continue
        found.add(entry)
    return sorted(found)


def aligned_entry(im, f, reach=0x400):
    """MSVC splits functions into chunks; a string reference can land in a chunk that directly
    follows its 16-byte-aligned entry. Walk back to the nearest known aligned function start
    whose straight-line instructions reach `f`."""
    if f % 16 == 0:
        return f
    starts = im.fstarts if not callable(im.fstarts) else im.fstarts()
    for g in sorted((g for g in starts if f - reach < g < f and g % 16 == 0), reverse=True):
        addrs = [(i.address - im.ib if i.address > im.ib else i.address) for i in im.disasm_fn(g, f - g + 16)]
        if f in addrs:
            return g
    return None


def match_call_near(im, spec):
    """The direct call target nearest to an anchor inside the one function that references the
    given strings. The anchor is a regex over 'mnemonic op_str' plus a '; "string"' annotation
    for rip-relative loads of C strings."""
    cn = spec["call_near"]
    fns = match_by_strings(im, {"strings": cn["strings"]})
    if len(fns) != 1:
        return []
    lines = []
    for i in im.disasm_fn(fns[0], 0x4000):
        text = f"{i.mnemonic} {i.op_str}"
        m = re.search(r"rip \+ (0x[0-9a-f]+)\]", i.op_str)
        if m:
            addr = (i.address - im.ib if i.address > im.ib else i.address) + i.size + int(m.group(1), 16)
            if im.rdata0 <= addr < im.rdata1:
                text += f' ; "{im.cstr(addr, 80)}"'
        lines.append((i, text))
    idx = [k for k, (_, t) in enumerate(lines) if re.search(cn["anchor"], t)]
    # The linear sweep can run past the function into its neighbours; "occurrence" picks the
    # n-th hit (the function's own one comes first), otherwise the anchor must be unique.
    if "occurrence" in cn:
        if len(idx) <= cn["occurrence"]:
            return []
        k = idx[cn["occurrence"]]
    elif len(idx) != 1:
        return []
    else:
        k = idx[0]
    order = range(k + 1, len(lines)) if cn["pick"] == "first_call_after" else range(k - 1, -1, -1)
    skip = cn.get("skip", 0)
    for j in order:
        i, _ = lines[j]
        if i.mnemonic == "call" and i.op_str.startswith("0x"):
            if skip:
                skip -= 1
                continue
            t = int(i.op_str, 16)
            return [t - im.ib if t > im.ib else t]
    return []


def main():
    im = Image(EXE)
    text = im.img[im.text0:im.text1]
    result, failures = {}, []
    for name, spec in FUNCTIONS.items():
        if "mnemonics" in spec or "strings" in spec or "call_near" in spec:
            matches = (match_by_mnemonics(im, spec) if "mnemonics" in spec else
                       match_by_strings(im, spec) if "strings" in spec else match_call_near(im, spec))
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
        if spec.get("follow_call"):
            # the fingerprint matched a thin wrapper; the function wanted is what it calls
            targets = set()
            for f in matches:
                for i in im.disasm_fn(f, 0x40):
                    if i.mnemonic == "call" and i.op_str.startswith("0x"):
                        t = int(i.op_str, 16)
                        targets.add(t - im.ib if t > im.ib else t)
                        break
            matches = sorted(targets)
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
