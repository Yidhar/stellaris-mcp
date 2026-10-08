"""Stage 2d: addresses derived from already-located code rather than from commands.

Some engine data is never touched by a command method, so globals.py cannot vote on it. Each
entry here starts from something already found (a buildable's token getter, a function from
functions.json, a database from globals.json) and follows the code from there:

  * buildable vtables: the class whose token getter (`mov eax, <token>; ret`) sits in the
    GetToken slot; the slot index is the one where every buildable vtable starts with a
    scalar deleting destructor.
  * buildable vtable slots: CalcProgressionTimeNeeded is the slot calling CBuildingType::
    CalcBuildTime (base build time +0x54, build speed modifier 0x9D); by declaration order
    CalcProgressionSpeed and CalcCost are the two slots before it.
  * TGameDatabase<CBuildingTypeDatabase>: the first database load in CBuildingType::CalcBuildSpeed
    (the CalcProgressionSpeed slot's callee).
  * TPdxRef<CZone>: in CBuildableBuilding's zone-aware slots, the ref database loaded right after
    the colony database.
  * CStrategicResourceDatabase: in CCountry::CanChangeGovernment, the global whose +0x70 (unity)
    is read.
  * script databases by folder: each database's loader references its "common/<folder>" path
    (out/linux_anchors.json names the Linux function). A constructor `CX::CX` is inlined into
    TGameDatabase<CX>::CreateInstance on Windows, which stores the fresh allocation into
    `_pInstance` right away; an `Init`/`InitInstance` loader reads its own `CX::_pInstance` first.
  * runtime fields: CGalacticObject's cached owner is the TPdxRef<CCountry> that
    CCountry::HasAutoSurveyedSystem compares with the country (`mov r8d, [system + X]` after it
    loads the country database).
  * the engine's Dear ImGui (`imgui_internals`): GImGui is the first global ImGui::NewFrame loads; the allocator pair
    and the user data are what every inlined ImGui::MemAlloc / MemFree site agrees on (`mov R, [GImGui]; test; je;
    inc|dec [R + MetricsActiveAllocations]; mov rdx, [user data]; call [alloc|free func]`); sizeof(ImGuiContext) is
    the one allocation above 0x2000 bytes; io.ImeWindowHandle / io.BackendPlatformUserData come from the wrapper's
    test before the Win32 NewFrame and that function's first load through GetIO(). A plugin compiled against ImGui
    1.85 static_asserts its own offsetof / sizeof against these (sdk::rt::ImGui*).
  * TPdxRef<X> databases no command touches: CGameStateDatabase constructs its ref databases in
    the same order on both platforms and Windows inlines each constructor down to its
    `_pDatabase` store. Known databases anchor the two sequences; a gap between two anchors is
    named only when both sides have the same length.

Every lookup in REQUIRED must produce exactly one answer or the stage fails.

Usage: python tools/sdk_dumper/anchors.py   -> out/anchors.json
"""
import collections
import json
import re
import runpy
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"

wx = runpy.run_path(str(HERE / "win_extract.py"), run_name="sdk_anchors")
Image, EXE, token_names, find_calls_to = wx["Image"], wx["EXE"], wx["token_names"], wx["find_calls_to"]

BUILDABLES = {
    "CBuildableBuilding": "buildable_planet_building",
    "CBuildableUpgradeBuilding": "buildable_planet_upgrade_building",
    "CBuildableClearDepositBlocker": "buildable_clear_deposit_blocker",
    "CBuildableArmy": "buildable_army",
    "CBuildableZone": "buildable_zone",
    "CBuildableDistrict": "buildable_district",
}


# symbols the bridge uses; the folder / ref-order rules name many more, best effort
REQUIRED = [
    "TGameDatabase<CTraditionTypeDatabase>::_pInstance",
    "TGameDatabase<CDecisionsDatabase>::_pInstance",
    "TGameDatabase<CAstralActionsDatabase>::_pInstance",
    "TGameDatabase<CArtifactActionsDatabase>::_pInstance",
    "TGameDatabase<CSubSpeciesIntegrationTypeDatabase>::_pInstance",
    "TGameDatabase<CSlaveryTypeDatabase>::_pInstance",
    "TGameDatabase<CPurgeTypeDatabase>::_pInstance",
    "TGameDatabase<CPopulationControlDatabase>::_pInstance",
    "TGameDatabase<CMilitaryServiceTypeDatabase>::_pInstance",
    "TGameDatabase<CMigrationControlDatabase>::_pInstance",
    "TGameDatabase<CLivingStandardDatabase>::_pInstance",
    "TGameDatabase<CColonizationControlDatabase>::_pInstance",
    "TGameDatabase<CCitizenshipTypeDatabase>::_pInstance",
    "TGameDatabase<CArmyTypeDatabase>::_pInstance",
    "TGameDatabase<CBuildingTypeDatabase>::_pInstance",
    "CTerraformDatabase::_pInstance",
    "CModifier::_Definitions",
    "CTraitDatabase::_pInstance",
    "CShipDesignTemplatesDatabase::_pInstance",
    "TPdxRef<CDeposit>::_pDatabase",
    "TPdxRef<CPopJob>::_pDatabase",
    "TPdxRef<CShipDesign>::_pDatabase",
    "TPdxRef<CDistrict>::_pDatabase",
    "TPdxRef<CSituation>::_pDatabase",
    "CConsole::_pInstance",
    "g_bFrameSmoothing",
    # the engine's Dear ImGui, for plugins that draw into its context
    "GImGui",
    "GImAllocatorAllocFunc",
    "GImAllocatorFreeFunc",
    "GImAllocatorUserData",
]


class Fail(Exception):
    pass


def rva(im, i):
    return i.address - im.ib if i.address > im.ib else i.address


def rip_target(im, i):
    m = re.search(r"rip \+ (0x[0-9a-f]+)\]", i.op_str)
    return rva(im, i) + i.size + int(m.group(1), 16) if m else None


def rbp_off(op):
    """Signed displacement of the first [rbp +/- d] operand, or None."""
    m = re.search(r"\[rbp(?: ([+-]) (0x[0-9a-f]+|\d+))?\]", op)
    return None if not m else (0 if not m.group(1) else int(m.group(2), 0) * (1 if m.group(1) == "+" else -1))


def rsp_off(op):
    """Displacement of the first [rsp + d] operand, or None."""
    m = re.search(r"\[rsp(?: \+ (0x[0-9a-f]+|\d+))?\]", op)
    return None if not m else int(m.group(1) or "0", 0)


def data_range(im):
    s = next(s for s in im.pe.sections if s.Name.startswith(b".data"))
    return s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize


def is_deleting_dtor(im, fn):
    """MSVC's deleting destructor tests the flags argument (edx, or the register it was saved in) for 1."""
    ins = im.disasm_fn(fn, 0x40)[:10]
    flag_regs = {"dl"}
    for i in ins:
        m = re.match(r"^(e\w\w|r\d+d), edx$", i.op_str)
        if i.mnemonic == "mov" and m:
            r = m.group(1)
            flag_regs.add({"ebx": "bl", "esi": "sil", "edi": "dil", "ecx": "cl", "eax": "al"}.get(r, r.replace("d", "b")))
    return any(i.mnemonic == "test" and any(i.op_str == f"{r}, 1" for r in flag_regs) for i in ins)


def buildable_vtables(im, tokens):
    text = im.img[im.text0:im.text1]
    refs = {}
    for cls, tok_name in BUILDABLES.items():
        tok = tokens.get(tok_name)
        if tok is None:
            raise Fail(f"token {tok_name} not registered")
        getters = [im.text0 + m.start() for m in re.finditer(re.escape(b"\xB8" + struct.pack("<I", tok) + b"\xC3"), text)]
        ptrs = set()
        for g in getters:
            ptrs |= {m.start() for m in re.finditer(re.escape(struct.pack("<Q", im.ib + g)), im.img)
                     if im.rdata0 <= m.start() < im.rdata1}
        refs[cls] = ptrs
    # the GetToken slot: the index at which every buildable's vtable begins with a deleting dtor
    for slot in range(4, 40):
        vts = {}
        for cls, ptrs in refs.items():
            cands = [p - slot * 8 for p in ptrs if is_deleting_dtor(im, im.q(p - slot * 8) - im.ib)]
            if len(cands) != 1:
                break
            vts[cls] = cands[0]
        else:
            return slot, vts
    raise Fail("no GetToken slot index fits every buildable vtable")


def slot_fn(im, vt, k):
    return im.q(vt + k * 8) - im.ib


def calls_of(im, fn, n=200):
    return [int(i.op_str, 16) - im.ib for i in im.disasm_fn(fn, 0x800)[:n] if i.mnemonic == "call" and i.op_str.startswith("0x")]


def is_calc_build_time(im, fn):
    j = " | ".join(f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(fn, 0x300)[:60])
    return "+ 0x54]" in j and "0x9d" in j


def db_loads(im, fn, n=400):
    """(global, shape) for each .data global loaded into a register: 'ref' for a TPdxRefDatabase
    (capacity at +0x20 compared), 'game' for a TGameDatabase (+0x50 items / +0x5c count)."""
    d0, d1 = data_range(im)
    ins = im.disasm_fn(fn, 0x1000)[:n]
    out = []
    for k, i in enumerate(ins):
        if i.mnemonic != "mov" or "rip + " not in i.op_str:
            continue
        t = rip_target(im, i)
        if not (d0 <= t < d1):
            continue
        reg = i.op_str.split(",")[0].strip()
        win = " | ".join(f"{x.mnemonic} {x.op_str}" for x in ins[k + 1:k + 30])
        shape = ("ref" if f"[{reg} + 0x20]" in win else
                 "game" if f"[{reg} + 0x50]" in win or f"[{reg} + 0x5c]" in win else "")
        out.append((t, shape))
    return out


def used_as_game_db(im, g, limit=400):
    """Whether some rip-relative load of global g is followed by a TGameDatabase access."""
    text = im.img[im.text0:im.text1]
    hits = 0
    for m in re.finditer(rb"\x48\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]|\x4C\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
        p = im.text0 + m.start()
        if p + 7 + struct.unpack_from("<i", im.img, p + 3)[0] != g:
            continue
        hits += 1
        if hits > limit:
            break
        ins = list(im.md.disasm(im.img[p:p + 80], im.ib + p))
        if not ins:
            continue
        reg = ins[0].op_str.split(",")[0].strip()
        win = " | ".join(i.op_str for i in ins[1:12])
        if f"[{reg} + 0x5c]" in win or f"[{reg} + 0x50]" in win:
            return True
    return False


def string_xrefs(im, strings):
    """{string: {function start}} for rip-relative references (lea / mov / movups) to each C string."""
    import numpy as np
    t = np.frombuffer(im.img[im.text0:im.text1], dtype=np.uint8)
    n = len(t) - 4
    disp = (t[:n].astype(np.int64) | (t[1:n + 1].astype(np.int64) << 8) |
            (t[2:n + 2].astype(np.int64) << 16) | (t[3:n + 3].astype(np.int64) << 24))
    disp = np.where(disp >= 2 ** 31, disp - 2 ** 32, disp)
    target = np.arange(n, dtype=np.int64) + im.text0 + 4 + disp
    at = {}
    for s in strings:
        for m in re.finditer(re.escape(s.encode() + b"\x00"), im.img[im.rdata0:im.rdata1]):
            if im.img[im.rdata0 + m.start() - 1] == 0:  # whole string, not a suffix of a longer one
                at[im.rdata0 + m.start()] = s
    out = {s: set() for s in strings}
    for h in np.nonzero(np.isin(target, list(at)))[0]:
        f = im.fn_of(im.text0 + int(h))
        if f is not None:
            out[at[int(target[h])]].add(f)
    return out


def alloc_store(im, fn, n=16):
    """CreateInstance shape: the first .data store in fn is `mov [rip+G], rax|rbx` of the result
    of a call before it (the allocation). A singleton's creator may construct it at length
    first; pass a larger n for those."""
    d0, d1 = data_range(im)
    called = False
    for i in im.disasm_fn(fn, 0x1000)[:n]:
        if i.mnemonic == "call":
            called = True
        if i.mnemonic == "mov" and i.op_str.startswith("qword ptr [rip + "):
            g = rip_target(im, i)
            if d0 <= g < d1:
                return g if called and i.op_str.endswith(("rax", "rbx")) else None
    return None


def stores_to(im, g):
    text = im.img[im.text0:im.text1]
    for m in re.finditer(rb"[\x48\x4C]\x89[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
        p = im.text0 + m.start()
        if p + 7 + struct.unpack_from("<i", im.img, p + 3)[0] == g:
            yield p


def first_data_load(im, fn):
    d0, d1 = data_range(im)
    for i in im.disasm_fn(fn, 0x3000)[:400]:
        if i.mnemonic == "mov" and ", qword ptr [rip + " in i.op_str:
            g = rip_target(im, i)
            if d0 <= g < d1:
                return g
    return None


def databases_by_folder(im, linux):
    """{symbol: global} for every "common/<folder>" whose Linux loader is unique."""
    paths = {p: fns[0] for p, fns in linux.get("db_paths", {}).items() if len(fns) == 1}
    xrefs = string_xrefs(im, paths)
    # a registry function that names every folder is not a loader
    per_fn = {}
    for fns in xrefs.values():
        for f in fns:
            per_fn[f] = per_fn.get(f, 0) + 1
    xrefs = {p: {f for f in fns if per_fn[f] <= 3} for p, fns in xrefs.items()}
    out = {}
    for p, lfn in paths.items():
        cls, meth = lfn.rsplit("::", 1)
        if not re.fullmatch(r"C\w+", cls):
            continue
        if meth == cls:
            sym = f"TGameDatabase<{cls}>::_pInstance"
            hits = {alloc_store(im, f) for f in xrefs[p]} - {None}
        elif meth in ("Init", "InitInstance"):
            # the loader reads its own instance first; a singleton is allocated and stored somewhere
            sym = f"{cls}::_pInstance"
            hits = {g for g in (first_data_load(im, f) for f in xrefs[p])
                    if g and any(alloc_store(im, im.fn_of(q), 200) == g for q in stores_to(im, g))}
        else:
            continue
        if len(hits) == 1:
            out[sym] = hits.pop()
    return out


def game_state_refs(im, linux, known):
    """TPdxRef<X>::_pDatabase by aligning Windows' inlined CGameStateDatabase constructor stores
    with the Linux constructor order. known: {symbol: rva} already located."""
    order = linux.get("game_state_ref_order", [])
    by_rva = {v: k for k, v in known.items()}
    colony = known.get("TPdxRef<CColony>::_pDatabase")
    if not order or colony is None:
        raise Fail("no Linux CGameStateDatabase order or no TPdxRef<CColony>")
    d0, d1 = data_range(im)
    best = []
    for p in stores_to(im, colony):
        seq = []
        for i in im.disasm_fn(im.fn_of(p), 0x8000):
            if i.mnemonic == "mov" and i.op_str.startswith("qword ptr [rip + "):
                g = rip_target(im, i)
                if d0 <= g < d1 and g not in seq:
                    seq.append(g)
        if len(seq) > len(best):
            best = seq
    lin = [f"TPdxRef<{x}>::_pDatabase" for x in order]
    # MSVC emits the members in a rotated order: start the Linux list at the first Windows anchor
    first = next((by_rva[g] for g in best if by_rva.get(g) in lin), None)
    if first is None:
        raise Fail("no known database among the CGameStateDatabase stores")
    k0 = lin.index(first)
    lin = lin[k0:] + lin[:k0]
    pairs = [(lin.index(by_rva[g]), j) for j, g in enumerate(best) if by_rva.get(g) in lin]
    if len(pairs) < 10 or any(b[0] <= a[0] or b[1] <= a[1] for a, b in zip(pairs, pairs[1:])):
        raise Fail(f"CGameStateDatabase store order does not follow Linux ({len(pairs)} anchors)")
    out = {}
    for (i0, j0), (i1, j1) in zip(pairs, pairs[1:]):
        if i1 - i0 == j1 - j0:
            for k in range(1, i1 - i0):
                out[lin[i0 + k]] = best[j0 + k]
    return out


def top_vote(votes, min_votes=3):
    """The key of a Counter that wins clearly (every other key has at most a tenth of its votes), else None."""
    ranked = votes.most_common(2)
    if not ranked or ranked[0][1] < min_votes or (len(ranked) > 1 and ranked[1][1] * 10 > ranked[0][1]):
        return None
    return ranked[0][0]


def imgui_internals(im, funcs):
    """The engine's Dear ImGui: its global context pointer, the allocator globals and the layout constants a plugin
    compiled against ImGui 1.85 checks itself against (all of ImGui is compiled into stellaris.exe).

    ImGui::MemAlloc / MemFree are inlined at every site as
        mov R, [GImGui]; test R, R; je +; inc|dec dword ptr [R + MetricsActiveAllocations]; mov rdx, [GImAllocatorUserData];
        [mov ecx, size]; call qword ptr [GImAllocatorAllocFunc | GImAllocatorFreeFunc]
    so the globals are what hundreds of such sites agree on; GImGui itself is the first global ImGui::NewFrame loads.
    The one allocation of more than 0x2000 bytes is IM_NEW(ImGuiContext) in CreateContext.
    Returns (globals, fields, problems)."""
    gl, fl, bad = {}, {}, []
    new_frame = funcs.get("ImGui_NewFrame", {}).get("rva")
    wrapper = funcs.get("NImGuiWrapper_ImGuiNewFrame", {}).get("rva")
    win32 = funcs.get("ImGui_ImplWin32_NewFrame", {}).get("rva")
    if not (new_frame and wrapper and win32):
        return gl, fl, ["ImGui_NewFrame / NImGuiWrapper_ImGuiNewFrame / ImGui_ImplWin32_NewFrame not in functions.json"]
    d0, d1 = data_range(im)
    g = next((rip_target(im, i) for i in im.disasm_fn(new_frame, 0x100)[:12]
              if i.mnemonic == "mov" and ", qword ptr [rip + " in i.op_str and d0 <= rip_target(im, i) < d1), None)
    if g is None:
        return gl, fl, ["GImGui: ImGui::NewFrame loads no global"]

    allocs, frees, users, sizes, counter = (collections.Counter() for _ in range(5))
    text = im.img[im.text0:im.text1]
    for m in re.finditer(rb"[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
        at = im.text0 + m.start()
        if at + 7 + struct.unpack_from("<i", im.img, at + 3)[0] != g:
            continue
        ins = list(im.md.disasm(im.img[at:at + 0x40], im.ib + at))[:10]
        ops = [f"{i.mnemonic} {i.op_str}" for i in ins]
        head = re.match(r"^mov (r\w+), qword ptr \[rip \+ ", ops[0]) if ops else None
        if not head or len(ops) < 6 or ops[1] != f"test {head.group(1)}, {head.group(1)}" or not ops[2].startswith("je "):
            continue
        bump = re.match(rf"^(inc|dec) dword ptr \[{head.group(1)} \+ (0x[0-9a-f]+)\]$", ops[3])
        if not bump:
            continue
        user = call = size = None
        for i, o in zip(ins[4:], ops[4:]):
            if user is None and o.startswith("mov rdx, qword ptr [rip + "):
                user = rip_target(im, i)
            mc = re.match(r"^mov ecx, (0x[0-9a-f]+)$", o)
            if mc and call is None:
                size = int(mc.group(1), 16)
            if o.startswith(("call qword ptr [rip + ", "jmp qword ptr [rip + ")):
                call = rip_target(im, i)
                break
        if user is None or call is None:
            continue
        counter[int(bump.group(2), 16)] += 1
        users[user] += 1
        (allocs if bump.group(1) == "inc" else frees)[call] += 1
        if bump.group(1) == "inc" and size is not None and size > 0x2000:
            sizes[size] += 1

    alloc, free, user, counter_off, ctx_size = top_vote(allocs), top_vote(frees), top_vote(users), top_vote(counter), top_vote(sizes, 1)
    for what, v, votes in (("GImAllocatorAllocFunc", alloc, allocs), ("GImAllocatorFreeFunc", free, frees),
                           ("GImAllocatorUserData", user, users), ("ImGuiContext io.MetricsActiveAllocations", counter_off, counter),
                           ("ImGuiContext size", ctx_size, sizes)):
        if v is None:
            bad.append(f"{what}: no clear winner {dict(votes)}")
    if alloc is not None and alloc == free:
        bad.append("GImAllocatorAllocFunc == GImAllocatorFreeFunc")
    if not bad:
        gl.update({"GImGui": g, "GImAllocatorAllocFunc": alloc, "GImAllocatorFreeFunc": free, "GImAllocatorUserData": user})
        fl["ImGuiContext_io_MetricsActiveAllocations"] = counter_off
        fl["ImGuiContext_sizeof"] = ctx_size
        print(f"ImGui: GImGui 0x{g:X}, allocator 0x{alloc:X} / 0x{free:X} / user data 0x{user:X} "
              f"({sum(allocs.values())} alloc, {sum(frees.values())} free sites), sizeof(ImGuiContext) 0x{ctx_size:X}, "
              f"MetricsActiveAllocations at ctx+0x{counter_off:X}")

    # io.ImeWindowHandle: the wrapper tests it before calling the Win32 backend's NewFrame
    ops = [(i, f"{i.mnemonic} {i.op_str}") for i in im.disasm_fn(wrapper, 0x400)]
    ime = set()
    for k in range(len(ops) - 2):
        c = re.match(r"^cmp qword ptr \[r\w+ \+ (0x[0-9a-f]+)\], r\w+$", ops[k][1])
        nxt = ops[k + 2][0]
        if (c and ops[k + 1][1].startswith("je ") and nxt.mnemonic == "call" and nxt.op_str.startswith("0x")
                and int(nxt.op_str, 16) - im.ib == win32):
            ime.add(int(c.group(1), 16))
    # io.BackendPlatformUserData: the first member the Win32 NewFrame loads through GetIO()
    bd = {int(mm.group(1), 16) for i in im.disasm_fn(win32, 0x100)[:20]
          if (mm := re.match(r"^mov r\w+, qword ptr \[rax \+ (0x[0-9a-f]+)\]$", f"{i.mnemonic} {i.op_str}"))}
    if len(ime) != 1:
        bad.append(f"ImGuiIO ImeWindowHandle: {sorted(ime)}")
    if len(bd) != 1:
        bad.append(f"ImGuiIO BackendPlatformUserData: {sorted(bd)}")
    if len(ime) == 1 and len(bd) == 1:
        fl["ImGuiIO_ImeWindowHandle"] = ime.pop()
        fl["ImGuiIO_BackendPlatformUserData"] = bd.pop()
        print(f"ImGuiIO: ImeWindowHandle +0x{fl['ImGuiIO_ImeWindowHandle']:X}, "
              f"BackendPlatformUserData +0x{fl['ImGuiIO_BackendPlatformUserData']:X}")
    return gl, fl, bad


def main():
    im = Image(EXE)
    r = token_names(im, set())
    names = next(x for x in r if isinstance(x, dict))
    tokens = {v: k for k, v in names.items()}
    globs = json.loads(((OUT / "globals_verified.json") if (OUT / "globals_verified.json").exists()
                        else (OUT / "globals.json")).read_text(encoding="utf-8"))
    funcs = json.loads((OUT / "functions.json").read_text(encoding="utf-8"))
    result = {"vtables": {}, "slots": {}, "globals": {}, "fields": {}}
    failures = []

    try:
        token_slot, vts = buildable_vtables(im, tokens)
        result["slots"]["CBuildableBase_GetToken"] = token_slot
        for cls, vt in vts.items():
            result["vtables"][cls] = vt
            print(f"vtable {cls}: 0x{vt:X}")
    except Fail as e:
        failures.append(str(e))
        vts = {}

    bvt = vts.get("CBuildableBuilding")
    if bvt:
        time_slots = [k for k in range(token_slot) if any(is_calc_build_time(im, c) for c in calls_of(im, slot_fn(im, bvt, k), 40))]
        if len(time_slots) != 1:
            failures.append(f"CalcProgressionTimeNeeded slot: {time_slots}")
        else:
            t = time_slots[0]
            result["slots"].update({"CBuildableBase_CalcProgressionTimeNeeded": t,
                                    "CBuildableBase_CalcProgressionSpeed": t - 1,
                                    "CBuildableBase_CalcCost": t - 2})
            print(f"slots: CalcCost {t - 2}, CalcProgressionSpeed {t - 1}, CalcProgressionTimeNeeded {t}")
            # CBuildingType::CalcBuildSpeed (the speed slot's callee) loads the database first
            # (it reads a per-type table at +0x90); confirm the global is used as a
            # TGameDatabase (+0x50 items / +0x5c count) somewhere in the image.
            speed_calls = calls_of(im, slot_fn(im, bvt, t - 1), 40)
            first = [loads[0][0] for loads in (db_loads(im, c, 20) for c in speed_calls) if loads]
            bdb = [g for g in dict.fromkeys(first) if used_as_game_db(im, g)]
            if len(set(bdb)) != 1:
                failures.append(f"TGameDatabase<CBuildingTypeDatabase>: {[hex(x) for x in bdb]}")
            else:
                result["globals"]["TGameDatabase<CBuildingTypeDatabase>::_pInstance"] = bdb[0]
                print(f"TGameDatabase<CBuildingTypeDatabase>: 0x{bdb[0]:X}")

        # TPdxRef<CZone>: the ref database loaded after the colony database in zone-aware slots
        colony_db = globs.get("TPdxRef<CColony>::_pDatabase", {}).get("rva")
        zone = set()
        for k in range(token_slot):
            loads = [g for g, shape in db_loads(im, slot_fn(im, bvt, k)) if shape == "ref"]
            if colony_db in loads:
                after = [g for g in loads[loads.index(colony_db) + 1:] if g != colony_db and g not in
                         {v["rva"] for v in globs.values()}]
                if after:
                    zone.add(after[0])
        if len(zone) != 1:
            failures.append(f"TPdxRef<CZone>: {[hex(x) for x in zone]}")
        else:
            z = zone.pop()
            result["globals"]["TPdxRef<CZone>::_pDatabase"] = z
            print(f"TPdxRef<CZone>: 0x{z:X}")

    # g_bFrameSmoothing: the `smooth` console command's handler toggles it with `sete byte [rip+X]`.
    # Console commands are {name, ..., help, handler} records; the handler pointer follows the
    # pointer to the command's help text.
    help_rva = im.img.find(b"Toggle framesmoothing\x00")
    handler = None
    if im.rdata0 <= help_rva < im.rdata1:
        p = im.img.find(struct.pack("<Q", im.ib + help_rva))
        if p != -1:
            handler = struct.unpack_from("<Q", im.img, p + 8)[0] - im.ib
    toggles = [rip_target(im, i) for i in (im.disasm_fn(handler, 0x400) if handler else [])
               if i.mnemonic == "sete" and "rip + " in i.op_str]
    if len(set(toggles)) != 1:
        failures.append(f"g_bFrameSmoothing: {[hex(x) for x in toggles if x]}")
    else:
        result["globals"]["g_bFrameSmoothing"] = toggles[0]
        print(f"g_bFrameSmoothing: 0x{toggles[0]:X}")

    # CConsole::_pInstance: callers do `mov rcx, [rip+X]` right before `call RunCommandNow`
    run_cmd = funcs.get("CConsole_RunCommandNow", {}).get("rva")
    if run_cmd:
        text = im.img[im.text0:im.text1]
        seen = {}
        for m in re.finditer(rb"\xe8", text):
            i = m.start()
            if im.text0 + i + 5 + struct.unpack_from("<i", text, i + 1)[0] != run_cmd:
                continue
            c = im.text0 + i
            pre = list(im.md.disasm(im.img[c - 0x20:c + 5], im.ib + c - 0x20))
            loads = [rip_target(im, x) for x in pre if x.mnemonic == "mov" and x.op_str.startswith("rcx, qword ptr [rip")]
            if loads:
                seen[loads[-1]] = seen.get(loads[-1], 0) + 1
        best = sorted(seen.items(), key=lambda kv: -kv[1])
        if not best or best[0][1] < 2 or (len(best) > 1 and best[1][1] == best[0][1]):
            failures.append(f"CConsole::_pInstance: {[(hex(k), v) for k, v in best]}")
        else:
            result["globals"]["CConsole::_pInstance"] = best[0][0]
            print(f"CConsole::_pInstance: 0x{best[0][0]:X} ({best[0][1]} call sites)")
    else:
        failures.append("CConsole_RunCommandNow not in functions.json")

    # CStrategicResourceDatabase: `mov rax, [rip+X]` then `mov r8, [rax + 0x70]` (unity)
    ccg = funcs.get("CCountry_CanChangeGovernment", {}).get("rva")
    if ccg:
        ins = im.disasm_fn(ccg, 0x800)
        res = [rip_target(im, a) for a, b in zip(ins, ins[1:])
               if a.mnemonic == "mov" and "rip + " in a.op_str and b.op_str.endswith("[rax + 0x70]")]
        if len(res) != 1:
            failures.append(f"CStrategicResourceDatabase: {[hex(x) for x in res if x]}")
        else:
            result["globals"]["CStrategicResourceDatabase::_pInstance"] = res[0]
            print(f"CStrategicResourceDatabase: 0x{res[0]:X}")
    else:
        failures.append("CCountry_CanChangeGovernment not in functions.json")

    # CGalacticObject owner: the id HasAutoSurveyedSystem looks up in TPdxRef<CCountry>
    has_auto = funcs.get("CCountry_HasAutoSurveyedSystem", {}).get("rva")
    country_db = globs.get("TPdxRef<CCountry>::_pDatabase", {}).get("rva")
    if has_auto and country_db:
        ins = im.disasm_fn(has_auto, 0x200)
        k = next((n for n, i in enumerate(ins) if i.mnemonic == "mov" and rip_target(im, i) == country_db), None)
        owner = [re.search(r"\[(?:rdi|rdx|rsi|rbx) \+ (0x[0-9a-f]+)\]", i.op_str)
                 for i in (ins[k + 1:k + 5] if k is not None else []) if i.mnemonic == "mov"]
        owner = [int(m.group(1), 16) for m in owner if m]
        if len(owner) != 1:
            failures.append(f"CGalacticObject owner field: {owner}")
        else:
            result["fields"]["CGalacticObject_owner"] = owner[0]
            print(f"CGalacticObject owner field: 0x{owner[0]:X}")
    else:
        failures.append("CGalacticObject owner field: HasAutoSurveyedSystem or TPdxRef<CCountry> missing")

    # CFleetManagerView reinforce flag: Update stores NGuiUtil::ShouldUpdateExpensiveThisFrame(8)
    # (inlined: `(int)(clock * 100) % 8 == 0`, i.e. `and eax, 0x80000007` ... `sete al`) in a bool
    # member and runs CalcAllShipsToReinforce only when it is set
    fm_update = funcs.get("CFleetManagerView_Update", {}).get("rva")
    if fm_update:
        ins = im.disasm_fn(fm_update, 0x800)
        k = next((n for n, i in enumerate(ins) if i.mnemonic == "and" and i.op_str.endswith(", 0x80000007")), None)
        due = [re.search(r"^byte ptr \[r\w+ \+ (0x[0-9a-f]+)\], al$", i.op_str)
               for i in (ins[k + 1:k + 10] if k is not None else []) if i.mnemonic == "mov"]
        due = [int(m.group(1), 16) for m in due if m]
        if len(due) != 1:
            failures.append(f"CFleetManagerView reinforce flag: {due}")
        else:
            result["fields"]["CFleetManagerView_reinforce_due"] = due[0]
            print(f"CFleetManagerView reinforce flag: 0x{due[0]:X}")
    else:
        failures.append("CFleetManagerView reinforce flag: CFleetManagerView_Update missing")

    # CInGameIdler paused flag / speed: SetGameSpeed opens with `cmp byte ptr [rcx + P], 0` (only a
    # running game restarts its timer) and `mov edx, dword ptr [rcx + S]` (the old speed)
    set_speed = funcs.get("CInGameIdler_SetGameSpeed", {}).get("rva")
    if set_speed:
        ins = im.disasm_fn(set_speed, 0x80)[:8]
        paused = [int(m.group(1), 16) for i in ins
                  for m in [re.search(r"^byte ptr \[rcx \+ (0x[0-9a-f]+)\], 0$", i.op_str)] if i.mnemonic == "cmp" and m]
        speed = [int(m.group(1), 16) for i in ins
                 for m in [re.search(r"^edx, dword ptr \[rcx \+ (0x[0-9a-f]+)\]$", i.op_str)] if i.mnemonic == "mov" and m]
        if len(paused) != 1 or len(speed) != 1:
            failures.append(f"CInGameIdler paused/speed: {paused} {speed}")
        else:
            result["fields"]["CInGameIdler_paused"] = paused[0]
            result["fields"]["CInGameIdler_speed"] = speed[0]
            print(f"CInGameIdler paused 0x{paused[0]:X}, speed 0x{speed[0]:X}")
    else:
        failures.append("CInGameIdler paused/speed: CInGameIdler_SetGameSpeed missing")

    # CGameIdler multiplayer flag: the is_multiplayer trigger tests `cmp byte ptr [rax + M], 0` on the
    # idler (CGameIdler::IsMultiplayer is a one-byte getter, inlined everywhere)
    is_mp = funcs.get("CIsMultiplayerTrigger_ActualEvaluate", {}).get("rva")
    if is_mp:
        ins = im.disasm_fn(is_mp, 0x60)[:8]
        mp = [int(m.group(1), 16) for i in ins
              for m in [re.search(r"^byte ptr \[rax \+ (0x[0-9a-f]+)\], 0$", i.op_str)] if i.mnemonic == "cmp" and m]
        if len(mp) != 1:
            failures.append(f"CGameIdler multiplayer flag: {mp}")
        else:
            result["fields"]["CGameIdler_is_multiplayer"] = mp[0]
            print(f"CGameIdler multiplayer flag 0x{mp[0]:X}")
    else:
        failures.append("CGameIdler multiplayer flag: CIsMultiplayerTrigger_ActualEvaluate missing")

    # CInGameIdler views: the idler's builder allocates each window view and stores it in a member
    # (`call <ctor>; nop; mov qword ptr [reg + X], rax`). A view's constructor names its gui window
    # and installs the class vtable right after the base constructor.
    views = {"CStartScreenWindow": "start_screen_window", "CAnomalyWindow": "anomaly_view_window",
             "CFirstContactView": "first_contact_view", "CAlertIconsWindow": "alerticon_window"}
    xrefs = string_xrefs(im, list(views.values()))
    builders = collections.defaultdict(dict)
    for cls, gui in views.items():
        for ctor in xrefs[gui]:
            head = im.disasm_fn(ctor, 0x200)[:24]
            vt = None
            for a, b in zip(head, head[1:]):
                if (a.mnemonic == "lea" and a.op_str.startswith("rax, [rip + ") and b.mnemonic == "mov"
                        and re.match(r"^qword ptr \[r\w+\], rax$", b.op_str)):
                    vt = rip_target(im, a)
                    break
            if vt is None:
                continue
            for site in find_calls_to(im, ctor):
                after = list(im.md.disasm(im.img[site:site + 0x20], im.ib + site))[1:3]
                if (len(after) == 2 and after[0].mnemonic == "nop" and after[1].mnemonic == "mov"):
                    m = re.match(r"^qword ptr \[r\w+ \+ (0x[0-9a-f]+)\], rax$", after[1].op_str)
                    if m:
                        builders[im.fn_of(site)][cls] = (int(m.group(1), 16), vt)
    full = [b for b, v in builders.items() if len(v) == len(views)]
    if len(full) != 1:
        failures.append(f"CInGameIdler views: builders {[hex(b) for b in builders]}")
    else:
        for cls, (off, vt) in builders[full[0]].items():
            result["fields"][f"CInGameIdler_{cls}"] = off
            result["vtables"][cls] = vt
            print(f"CInGameIdler {cls} at +0x{off:X}, vtable 0x{vt:X}")
        # CGuiView::Hide: the slot CAnomalyWindow overrides with OnLeaveBe's body (hide the gui
        # window at +0x78 when visible, clear the shown flag at +0x90, return)
        avt = builders[full[0]]["CAnomalyWindow"][1]
        hide = []
        for k in range(40):
            body = " | ".join(f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(slot_fn(im, avt, k), 0x60)[:14])
            if re.search(r"mov rcx, qword ptr \[rcx \+ 0x78\] \| .*cmp byte ptr \[rcx \+ 0x41\], 0 \| .*"
                         r"call qword ptr \[rax \+ 0x[0-9a-f]+\] \| mov byte ptr \[rbx \+ 0x90\], 0 \| add rsp, 0x20 \| pop rbx \| ret", body):
                hide.append(k)
        if len(hide) != 1:
            failures.append(f"CGuiView::Hide slot: {hide}")
        else:
            result["slots"]["CGuiView_Hide"] = hide[0]
            print(f"slots: CGuiView::Hide {hide[0]}")

    # CModifier definitions: CPdxArray<CPdxModifierDefinition> {data, capacity +8, size +0xC} that
    # CModifier::LogDefinitions walks (stride 0xB0)
    log_defs = string_xrefs(im, ["Printing Modifier Definitions:"])["Printing Modifier Definitions:"]
    d0, d1 = data_range(im)
    loads = collections.defaultdict(set)
    for f in log_defs:
        for i in im.disasm_fn(f, 0x400)[:120]:
            if i.mnemonic in ("mov", "movsxd") and "ptr [rip + " in i.op_str:
                g = rip_target(im, i)
                if d0 <= g < d1:
                    loads[i.op_str.split(",")[1].split()[0]].add(g)
    arr = [g for g in loads["qword"] if g + 0xC in loads["dword"]]
    if len(log_defs) != 1 or len(arr) != 1:
        failures.append(f"CModifier definitions: fns {len(log_defs)} arrays {[hex(g) for g in arr]}")
    else:
        result["globals"]["CModifier::_Definitions"] = arr[0]
        print(f"CModifier definitions 0x{arr[0]:X}")

    # CGameState date in hours: HandleTurnTick advances it with `add dword ptr [reg], 0x18` after
    # `lea reg, [state + X]`
    turn_tick = funcs.get("CGameState_HandleTurnTick", {}).get("rva")
    if turn_tick:
        ins = im.disasm_fn(turn_tick, 0x3000)
        date = set()
        for k, i in enumerate(ins):
            m = re.search(r"^dword ptr \[(r\w+)\], 0x18$", i.op_str)
            if i.mnemonic == "add" and m:
                for j in range(k - 1, max(k - 300, 0), -1):
                    lm = re.search(r"^" + m.group(1) + r", \[r\w+ \+ (0x[0-9a-f]+)\]$", ins[j].op_str)
                    if ins[j].mnemonic == "lea" and lm:
                        date.add(int(lm.group(1), 16))
                        break
        if len(date) != 1:
            failures.append(f"CGameState date: {sorted(date)}")
        else:
            result["fields"]["CGameState_date_hours"] = date.pop()
            print(f"CGameState date (hours): 0x{result['fields']['CGameState_date_hours']:X}")
    else:
        failures.append("CGameState date: CGameState_HandleTurnTick missing")

    # has_*_flag: CHasFlagTrigger::ActualEvaluate reads the dynamic base-name size (`cmp qword ptr
    # [rcx + D], 0`), the static id (`movzx ebx, word ptr [rcx + F]`), calls GetFlags through the
    # vtable (`call qword ptr [rax + V]`), then scans the container: count (`movsxd rcx, dword ptr
    # [rax + C]`) and ids (`mov rax, qword ptr [rax + I]`). UpdateFlags reads days (`mov rcx,
    # qword ptr [rbx + Y]`) with the same count
    has_flag = funcs.get("CHasFlagTrigger_ActualEvaluate", {}).get("rva")
    upd_flags = funcs.get("CPdxIntegerFlags_UpdateFlags", {}).get("rva")
    if has_flag and upd_flags:
        ops = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(has_flag, 0x100)[:40]]

        def first(rx):
            return next((int(m.group(1), 16) for o in ops for m in [re.search(rx, o)] if m), None)

        vals = {
            "CHasFlagTrigger_dynamic_size": first(r"^cmp qword ptr \[rcx \+ (0x[0-9a-f]+)\], 0$"),
            "CHasFlagTrigger_flag": first(r"^movzx ebx, word ptr \[rcx \+ (0x[0-9a-f]+)\]$"),
            "CHasFlagTrigger_vt_GetFlags": first(r"^call qword ptr \[rax \+ (0x[0-9a-f]+)\]$"),
            "CPdxIntegerFlags_count": first(r"^movsxd rcx, dword ptr \[rax \+ (0x[0-9a-f]+)\]$"),
            "CPdxIntegerFlags_ids": first(r"^mov rax, qword ptr \[rax \+ (0x[0-9a-f]+)\]$"),
        }
        uops = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(upd_flags, 0x80)[:20]]
        vals["CPdxIntegerFlags_days"] = next((int(m.group(1), 16) for o in uops
                                             for m in [re.search(r"^mov rcx, qword ptr \[rbx \+ (0x[0-9a-f]+)\]$", o)] if m), None)
        ucount = next((int(m.group(1), 16) for o in uops
                       for m in [re.search(r"^mov esi, dword ptr \[rcx \+ (0x[0-9a-f]+)\]$", o)] if m), None)
        if None in vals.values() or ucount != vals["CPdxIntegerFlags_count"]:
            failures.append(f"flag container fields: {vals} (UpdateFlags count {ucount})")
        else:
            result["fields"].update(vals)
            print("flag fields: " + ", ".join(f"{k}=0x{v:X}" for k, v in vals.items()))
    else:
        failures.append("flag container fields: CHasFlagTrigger_ActualEvaluate / CPdxIntegerFlags_UpdateFlags missing")

    # Modifier graph flush (CModifierNodeManager::Update): batch byte (`cmp byte ptr [rcx + B], 0`),
    # busy and has-invalid bytes (the two `xchg byte ptr [rcx + X], al`), masked-invalidate byte
    # (`lea r14, [rcx + M]`), the RNG-forbidden global (`movzx eax, byte ptr [rip + F]` then
    # `mov byte ptr [rip + F], 1`) and the random-log config field (`mov ebx, dword ptr [rax + L]`
    # after the first call, then `or ebx, 2`)
    mupd = funcs.get("CModifierNodeManager_Update", {}).get("rva")
    nupd = funcs.get("CModifierNodeBase_Update", {}).get("rva")
    addmod = funcs.get("CPdxModifier_AddModifierInternal", {}).get("rva")
    if mupd and nupd and addmod:
        ins = im.disasm_fn(mupd, 0x200)[:40]
        ops = [f"{i.mnemonic} {i.op_str}" for i in ins]
        vals, glob = {}, None

        def disp(rx, lst=ops):
            return [int(m.group(1), 16) for o in lst for m in [re.search(rx, o)] if m]

        vals["CModifierNodeManager_batch"] = (disp(r"^cmp byte ptr \[rcx \+ (0x[0-9a-f]+)\], 0$") or [None])[0]
        xchg = disp(r"^xchg byte ptr \[rcx \+ (0x[0-9a-f]+)\], al$")
        vals["CModifierNodeManager_busy"], vals["CModifierNodeManager_has_invalid"] = (xchg + [None, None])[:2]
        vals["CModifierNodeManager_masked"] = (disp(r"^lea r14, \[rcx \+ (0x[0-9a-f]+)\]$") or [None])[0]
        vals["CRandomLog_config"] = (disp(r"^mov ebx, dword ptr \[rax \+ (0x[0-9a-f]+)\]$") or [None])[0]
        for k, a in enumerate(ins):
            if a.mnemonic == "movzx" and "rip + " in a.op_str:
                for b in ins[k + 1:k + 4]:  # the saved value is spilled to the stack in between
                    if b.mnemonic == "mov" and b.op_str.endswith("], 1") and "rip + " in b.op_str \
                            and rip_target(im, a) == rip_target(im, b):
                        glob = rip_target(im, a)
        # node fields (CModifierNodeBase::Update): dirty byte, embedded CModifier (`lea rcx, [rbx + X]`
        # right before the vcall +0x80 Clear)
        nops = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(nupd, 0x100)[:30]]
        vals["CModifierNode_dirty"] = (disp(r"^movzx eax, byte ptr \[rcx \+ (0x[0-9a-f]+)\]$", nops) or [None])[0]
        k = next((n for n, o in enumerate(nops) if o == "call qword ptr [rax + 0x80]"), None)
        mod = [int(m.group(1), 16) for o in (nops[max(k - 4, 0):k] if k else [])
               for m in [re.search(r"^lea rcx, \[rbx \+ (0x[0-9a-f]+)\]$", o)] if m]
        vals["CModifierNode_modifier"] = mod[0] if len(mod) == 1 else None
        # CModifier: entries data/count (`mov R, qword ptr [S + D]` then `movsxd R2, dword ptr [S + C]`,
        # the source's entries) and parents (`mov edx, dword ptr [rsi + P]` then `lea rcx, [rsi + A]`,
        # a CPdxArray: data at A + 8, count at A + 0x14 == P)
        aops = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(addmod, 0x400)[:120]]
        for a, b in zip(aops, aops[1:]):
            m1 = re.search(r"^mov \w+, qword ptr \[(\w+) \+ (0x[0-9a-f]+)\]$", a)
            m2 = re.search(r"^movsxd \w+, dword ptr \[(\w+) \+ (0x[0-9a-f]+)\]$", b)
            if m1 and m2 and m1.group(1) == m2.group(1) and "CModifier_entries" not in vals:
                vals["CModifier_entries"], vals["CModifier_entry_count"] = int(m1.group(2), 16), int(m2.group(2), 16)
            m3 = re.search(r"^mov edx, dword ptr \[rsi \+ (0x[0-9a-f]+)\]$", a)
            m4 = re.search(r"^lea rcx, \[rsi \+ (0x[0-9a-f]+)\]$", b)
            if m3 and m4 and "CModifier_parents" not in vals:
                if int(m4.group(1), 16) + 0x14 == int(m3.group(1), 16):
                    vals["CModifier_parents"], vals["CModifier_parent_count"] = int(m4.group(1), 16) + 8, int(m3.group(1), 16)
        # node slot table: `and eax, 0xfffff; mov rcx, [rip+mgr]; cmp eax, [rcx + COUNT]; jae;
        # lea rdx, [rax + rax*2]; mov rcx, [rcx + SLOTS]; cmp dword ptr [rcx + rdx*8], id; jne;
        # mov rcx, [rcx + rdx*8 + NODE]` (every match in the exe must agree)
        text = im.img[im.text0:im.text1]
        slots = set()
        for m in re.finditer(rb"\x25\xFF\xFF\x0F\x00\x48\x8B\x0D....\x3B\x41(.)\x73.\x48\x8D\x14\x40\x48\x8B\x49(.)",
                             text, re.S):
            tail = text[m.end():m.end() + 16]
            n = re.search(rb"\x48\x8B\x4C\xD1(.)", tail, re.S)
            if n:
                slots.add((m.group(1)[0], m.group(2)[0], n.group(1)[0]))
        if len(slots) == 1:
            c, sl, nd = slots.pop()
            vals.update({"CModifierNodeManager_slot_count": c, "CModifierNodeManager_slots": sl,
                         "CModifierNodeManager_slot_node": nd})
        if None in vals.values() or glob is None or len(vals) != 14:
            failures.append(f"modifier flush fields: {vals} forbidden={glob} slots={slots}")
        else:
            result["fields"].update(vals)
            result["globals"]["CRandom_Forbidden"] = glob
            print("modifier flush fields: " + ", ".join(f"{k}=0x{v:X}" for k, v in vals.items()) + f", CRandom_Forbidden=0x{glob:X}")
    else:
        failures.append("modifier flush fields: CModifierNodeManager_Update / CModifierNodeBase_Update / AddModifierInternal missing")

    # Fleet parallel-for chunk size (inlined CPdxParallelForDescriptor, partition mode 1):
    # tasks = threads + 1; chunk = max(1, count / tasks / 3):
    #   idiv ecx; mov ecx, eax; mov eax, 0x55555556; imul ecx; ... mov eax, 1; cmp edx, eax; cmovg eax, edx
    # The 5-byte `cmp edx, eax; cmovg eax, edx` (3B D0 0F 4F C2) is the clamp stellaris_perf.dll can
    # turn into NOPs to make every chunk one fleet.
    usp = funcs.get("CGameState_UpdateShipParallel", {}).get("rva")
    if usp:
        ins = im.disasm_fn(usp, 0x800)
        site = None
        for k, i in enumerate(ins):
            if i.mnemonic == "mov" and i.op_str == "eax, 0x55555556" and k + 8 < len(ins) \
                    and any(x.mnemonic == "idiv" for x in ins[max(k - 4, 0):k]):
                for j in range(k + 1, k + 9):
                    if ins[j].mnemonic == "cmp" and ins[j].op_str == "edx, eax" and ins[j + 1].mnemonic == "cmovg" \
                            and ins[j + 1].op_str == "eax, edx":
                        a = ins[j].address - im.ib if ins[j].address > im.ib else ins[j].address
                        if im.img[a:a + 5] == b"\x3B\xD0\x0F\x4F\xC2":
                            site = a
                        break
                break
        if site is None:
            failures.append("UpdateShipParallel grain clamp not found")
        else:
            result["globals"]["UpdateShipParallel_GrainClamp"] = site
            print(f"UpdateShipParallel grain clamp: 0x{site:X}")
    else:
        failures.append("UpdateShipParallel grain clamp: CGameState_UpdateShipParallel missing")

    # Fleet paths (DrawMovementDebugLines inlines CFleetMovementManager::CalcPath): the call to
    # CFleetPath::Create takes `lea rcx, [rbp + P]` (path), `lea rdx, [rbp + F]` (from) and
    # `lea r8, [rbp + T]` (to); the vtables are the rip-relative leas stored to [rbp + P] (CFleetPath),
    # [rbp + P + 8] (its CPdxArray<SNode>) and [rbp + T] (CCelestialCoordinate). Node count is the
    # `cmp dword ptr [rbp + P + C], 0` after Create, the node array the `mov rcx, [rbp + P + D]` freed
    # right after it, the node stride the `add R, S` after `inc reg` in the ETA-label loop. The fleet's
    # position is a vcall `lea rcx, [rdi + B]; mov rax, [rcx]; call [rax + 8k]` whose result is copied at once (a secondary base)
    # A node's jump method (EPathJumpMethod) is what CalcEstimatedDays copies into its CFTLJump.
    ddl = funcs.get("DrawMovementDebugLines", {}).get("rva")
    create = funcs.get("CFleetPath_Create", {}).get("rva")
    if ddl and create:
        ins = im.disasm_fn(ddl, 0x3000)
        ops = [f"{i.mnemonic} {i.op_str}" for i in ins]
        k = next((n for n, i in enumerate(ins) if i.mnemonic == "call" and i.op_str.startswith("0x")
                  and int(i.op_str, 16) - im.ib == create), None)
        vals, vts = {}, {}
        if k is not None:
            args = {}
            for o in ops[max(k - 12, 0):k]:
                m = re.search(r"^lea (rcx|rdx|r8), \[rbp(?: \+ (0x[0-9a-f]+))?\]$", o)
                if m:
                    args[m.group(1)] = int(m.group(2) or "0", 16)
            stores, regs = {}, {}
            for i, o in zip(ins[:k], ops[:k]):
                m = re.search(r"^lea (\w+), \[rip \+ 0x[0-9a-f]+\]$", o)
                if m:
                    regs[m.group(1)] = rip_target(im, i)
                    continue
                m = re.search(r"^mov qword ptr \[rbp(?: \+ (0x[0-9a-f]+))?\], (\w+)$", o)
                if m and m.group(2) in regs:
                    stores[int(m.group(1) or "0", 16)] = regs[m.group(2)]
            p, t = args.get("rcx"), args.get("r8")
            if p is not None and t is not None:
                vts = {"CFleetPath": stores.get(p), "CPdxArray_CFleetPath_SNode": stores.get(p + 8),
                       "CCelestialCoordinate": stores.get(t)}
                after = ops[k + 1:k + 12]
                cnt = [int(m.group(1), 16) - p for o in after for m in [re.search(r"^cmp dword ptr \[rbp \+ (0x[0-9a-f]+)\], 0$", o)] if m]
                data = [int(m.group(1), 16) - p for o in after for m in [re.search(r"^mov rcx, qword ptr \[rbp \+ (0x[0-9a-f]+)\]$", o)] if m]
                vals["CFleetPath_node_count"] = cnt[0] if cnt else None
                vals["CFleetPath_nodes"] = data[0] if data else None
            stride = [int(m.group(1), 16) for a, b in zip(ops, ops[1:]) if re.match(r"^inc r\w+$", a)
                      for m in [re.search(r"^add \w+, (0x[0-9a-f]+)$", b)] if m]
            vals["CFleetPath_node_size"] = stride[0] if stride else None
            for j, (a, b, c) in enumerate(zip(ops, ops[1:], ops[2:])):
                m = re.search(r"^lea rcx, \[rdi \+ (0x[0-9a-f]+)\]$", a)
                n = re.search(r"^call qword ptr \[rax(?: \+ (0x[0-9a-f]+|\d+))?\]$", c)
                # the returned coordinate is copied right away (x/y at [rax + 8])
                if m and b == "mov rax, qword ptr [rcx]" and n and any("[rax + 8]" in o for o in ops[j + 3:j + 6]):
                    vals["CFleet_coordinate_base"] = int(m.group(1), 16)
                    result["slots"]["CFleet_GetCoordinate"] = int(n.group(1) or "0", 0) // 8
                    break
        # node jump method (EPathJumpMethod: jump_hyperlane / jump_bypass): CalcEstimatedDays builds
        # a CFTLJump on the stack (`lea R, [rip + CFTLJump vtable]; mov [rbp + B], R`) and stores
        # `mov dword ptr [rbp + B + jump_method], R32` loaded by `mov R32, dword ptr [node + J]`
        ced = funcs.get("CFleetPath_CalcEstimatedDays", {}).get("rva")
        lay = json.loads((OUT / "win_layouts.json").read_text(encoding="utf-8"))["layouts"].get("CFTLJump::WriteMembers", {})
        ftl_vt = (lay.get("vtables") or [[None]])[0][0]
        jm = next((f["win_off"] for f in lay.get("fields", []) if f["name"] == "jump_method"), None)
        vals["CFleetPath_node_jump_method"] = None
        if ced and ftl_vt and jm is not None:
            cins = im.disasm_fn(ced, 0x1400)
            cops = [f"{i.mnemonic} {i.op_str}" for i in cins]

            base_off = None
            for j, i in enumerate(cins):
                if i.mnemonic == "lea" and rip_target(im, i) == ftl_vt:
                    reg = i.op_str.split(",")[0]
                    nxt = cops[j + 1] if j + 1 < len(cops) else ""
                    if nxt.startswith("mov qword ptr [rbp") and nxt.endswith(", " + reg):
                        base_off = rbp_off(nxt)
                        break
            if base_off is not None:
                for j, o in enumerate(cops):
                    m = re.search(r"^mov dword ptr \[rbp[^\]]*\], (\w+)$", o)
                    if m and rbp_off(o) == base_off + jm:
                        for o2 in reversed(cops[max(j - 40, 0):j]):
                            m2 = re.search(r"^mov " + m.group(1) + r", dword ptr \[\w+ \+ \w+\*8 \+ (0x[0-9a-f]+)\]$", o2)
                            if m2:
                                vals["CFleetPath_node_jump_method"] = int(m2.group(1), 16)
                                break
                        break
        # node bypass (the gateway / wormhole / relay used): CFleetPath::Create fills a CFTLJump at
        # [rbp + F] and copies its fields into the node it inserts (`lea r8, [rsp + N]` before the
        # insert call): `mov eax, dword ptr [rbp + F + bypass_to]; mov dword ptr [rsp + N + K], eax`
        # for an entry node; K must sit next to the jump method copied the same way
        bto = next((f["win_off"] for f in lay.get("fields", []) if f["name"] == "bypass_to"), None)
        vals["CFleetPath_node_bypass"] = None
        if create and ftl_vt and jm is not None and bto is not None:
            kins = im.disasm_fn(create, 0x1400)
            kops = [f"{i.mnemonic} {i.op_str}" for i in kins]
            fbase = None
            for j, i in enumerate(kins):
                if i.mnemonic == "lea" and rip_target(im, i) == ftl_vt and j + 1 < len(kops) \
                        and kops[j + 1].startswith("mov qword ptr [rbp"):
                    fbase = rbp_off(kops[j + 1])
                    break

            if fbase is not None:
                for j in range(len(kops) - 1):
                    a, b = kops[j], kops[j + 1]
                    if a.startswith("mov eax, dword ptr [rbp") and rbp_off(a) == fbase + bto and \
                            b.startswith("mov dword ptr [rsp") and b.endswith(", eax"):
                        node = next((rsp_off(o) for o in kops[j + 2:j + 8] if o.startswith("lea r8, [rsp")), None)
                        jm_node = [rsp_off(kops[k + 1]) - node for k in range(max(j - 8, 0), j)
                                   if node is not None and kops[k].startswith("mov eax, dword ptr [rbp")
                                   and rbp_off(kops[k]) == fbase + jm and kops[k + 1].startswith("mov dword ptr [rsp")]
                        if node is not None and jm_node == [vals["CFleetPath_node_jump_method"]]:
                            vals["CFleetPath_node_bypass"] = rsp_off(b) - node
                        break
        if len(vals) != 6 or None in vals.values() or not vts or None in vts.values() \
                or "CFleet_GetCoordinate" not in result["slots"]:
            failures.append(f"fleet path: {vals} vtables={vts}")
        else:
            result["fields"].update(vals)
            result["vtables"].update(vts)
            print("fleet path: " + ", ".join(f"{k}=0x{v:X}" for k, v in {**vals, **vts}.items())
                  + f", GetCoordinate slot {result['slots']['CFleet_GetCoordinate']}")
    else:
        failures.append("fleet path: DrawMovementDebugLines / CFleetPath_Create missing")

    # Ship design layout, read from the designer's own edit path:
    #   CShipDesign::CalcLongName: `mov rax, [design + S]; ... mov rcx, [rax + Z]` (stage 0, its hull)
    #   CShipGrowthStage::UpdateResources: core components `mov rax, [stage + CD]` / `movsxd r, [stage + CC]`,
    #     sections `mov rax, [stage + SD]` / `movsxd r, [stage + SC]`, section template `mov rdx, [sec + T]`
    #   CShipDesignSection::SetComponentOnSlot: components `mov rsi, [sec + D]` / `movsxd r13, [sec + C]`,
    #     element `shl rbx, 5`, `mov [rbx + TT], rax` (template), `mov [rbx + SL], rax` (slot, after the
    #     CSectionTemplate::GetComponentSlot call on `[sec + T]`)
    #   CSectionTemplate::GetComponentSlot: slots `mov r15, [tmpl + SD]` / `mov ebp, [tmpl + SC]`, `imul r, r, STRIDE`,
    #     name `lea rcx, [slot + N]`
    #   CShipDesignerBase::SetComponentOnSlot: `mov rax, [component + SET]; mov rcx, [rax + 0xc8]` (component set)
    fn_names = ["CShipDesign_CalcLongName", "CShipGrowthStage_UpdateResources", "CShipDesignSection_SetComponentOnSlot",
                "CShipDesignerBase_SetComponentOnSlot"]
    if all(funcs.get(n) for n in fn_names):
        def ops_of(rva_, n=120):
            return [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(rva_, 0x1000)[:n]]

        def hexs(m):
            return int(m.group(1), 16)

        sd = {}
        cl = ops_of(funcs["CShipDesign_CalcLongName"]["rva"], 40)
        for k, a in enumerate(cl):
            m1 = re.search(r"^mov rax, qword ptr \[rcx \+ (0x[0-9a-f]+)\]$", a)
            if m1:
                for b in cl[k + 1:k + 4]:
                    m2 = re.search(r"^mov rcx, qword ptr \[rax \+ (0x[0-9a-f]+|\d+)\]$", b)
                    if m2:
                        sd["CShipDesign_stages"] = hexs(m1)
                        sd["CShipGrowthStage_ship_size"] = int(m2.group(1), 0)
                        break
                if "CShipDesign_stages" in sd:
                    break
        ur = ops_of(funcs["CShipGrowthStage_UpdateResources"]["rva"], 80)
        loads = [(o, re.search(r"^(mov|movsxd) \w+, (qword|dword) ptr \[(r\w+) \+ (0x[0-9a-f]+)\]$", o)) for o in ur]
        pairs = [(m.group(1), m.group(3), int(m.group(4), 16)) for o, m in loads if m]
        # the stage register is the base of both `movsxd` counts
        counts = [(base, off) for kind, base, off in pairs if kind == "movsxd"]
        if len(counts) >= 2 and counts[0][0] == counts[1][0]:
            stage_reg = counts[0][0]
            datas = [off for kind, base, off in pairs if kind == "mov" and base == stage_reg]
            if len(datas) >= 2:
                sd["CShipGrowthStage_component_count"], sd["CShipGrowthStage_components"] = counts[0][1], datas[0]
                sd["CShipGrowthStage_section_count"], sd["CShipGrowthStage_sections"] = counts[1][1], datas[1]
        sc = ops_of(funcs["CShipDesignSection_SetComponentOnSlot"]["rva"], 90)
        get_slot = None
        for k, o in enumerate(sc):
            m = re.search(r"^movsxd r13, dword ptr \[rbp \+ (0x[0-9a-f]+)\]$", o)
            if m:
                sd["CShipDesignSection_component_count"] = hexs(m)
            m = re.search(r"^mov rsi, qword ptr \[rbp \+ (0x[0-9a-f]+)\]$", o)
            if m and "CShipDesignSection_components" not in sd:
                sd["CShipDesignSection_components"] = hexs(m)
            m = re.search(r"^shl rbx, (\d+)$", o)
            if m:
                sd["CShipDesignComponent_size"] = 1 << int(m.group(1))
            m = re.search(r"^mov qword ptr \[rbx \+ (0x[0-9a-f]+|\d+)\], rax$", o)
            if m and "CShipDesignComponent_template" not in sd:
                sd["CShipDesignComponent_template"] = int(m.group(1), 0)
            m = re.search(r"^mov rcx, qword ptr \[rbp \+ (0x[0-9a-f]+)\]$", o)
            if m and k + 1 < len(sc) and sc[k + 1].startswith("call 0x"):
                sd["CShipDesignSection_template"] = hexs(m)
                get_slot = int(sc[k + 1].split()[1], 16) - im.ib
            m = re.search(r"^mov qword ptr \[rbx \+ (0x[0-9a-f]+|\d+)\], rax$", o)
            if m and get_slot and "CShipDesignComponent_slot" not in sd:
                sd["CShipDesignComponent_slot"] = int(m.group(1), 0)
        if get_slot:
            gs = ops_of(get_slot, 40)
            for o in gs:
                m = re.search(r"^mov e\w+, dword ptr \[rcx \+ (0x[0-9a-f]+)\]$", o)
                if m and "CSectionTemplate_slot_count" not in sd:
                    sd["CSectionTemplate_slot_count"] = hexs(m)
                m = re.search(r"^mov r\w+, qword ptr \[rcx \+ (0x[0-9a-f]+)\]$", o)
                if m and "CSectionTemplate_slots" not in sd:
                    sd["CSectionTemplate_slots"] = hexs(m)
                m = re.search(r"^imul \w+, \w+, (0x[0-9a-f]+)$", o)
                if m:
                    sd["CComponentSlot_size"] = hexs(m)
                m = re.search(r"^lea rcx, \[rdi \+ (0x[0-9a-f]+)\]$", o)
                if m and "CComponentSlot_name" not in sd:
                    sd["CComponentSlot_name"] = hexs(m)
        ui = ops_of(funcs["CShipDesignerBase_SetComponentOnSlot"]["rva"], 80)
        for a, b in zip(ui, ui[1:]):
            m1 = re.search(r"^mov rax, qword ptr \[r\w+ \+ (0x[0-9a-f]+)\]$", a)
            if m1 and b == "mov rcx, qword ptr [rax + 0xc8]":
                sd["CComponentTemplate_component_set"] = hexs(m1)
                break
        # hull flags (is_designable is bit 1, CShipSize::ReadMember token 0x31fe): the save check
        # starts with `mov rcx, [stage + 8]; mov eax, dword ptr [rcx + F]; and eax, 0xc0000`
        vs = funcs.get("CShipGrowthStage_IsValidToSaveForCountry", {}).get("rva")
        if vs:
            vo = ops_of(vs, 40)
            for a, b in zip(vo, vo[1:]):
                m = re.search(r"^mov eax, dword ptr \[rcx \+ (0x[0-9a-f]+)\]$", a)
                if m and b == "and eax, 0xc0000":
                    sd["CShipSize_flags"] = hexs(m)
                    break
        # slot compatibility (CShipDesignerBase::ComponentIsAllowedOnSlot): `movzx eax, [slot + SS];
        # cmp al, [component + CS]; ... cmp al, ANY_SIZE` then the same for the slot type
        ca = funcs.get("CShipDesignerBase_ComponentIsAllowedOnSlot", {}).get("rva")
        if ca:
            co = ops_of(ca, 30)
            slot_b = [int(m.group(1), 16) for o in co for m in [re.search(r"^movzx eax, byte ptr \[r8 \+ (0x[0-9a-f]+)\]$", o)] if m]
            comp_b = [int(m.group(1), 16) for o in co for m in [re.search(r"^cmp al, byte ptr \[rdx \+ (0x[0-9a-f]+)\]$", o)] if m]
            anys = [int(m.group(1), 0) for o in co for m in [re.search(r"^cmp al, (0x[0-9a-f]+|\d+)$", o)] if m]
            if len(slot_b) >= 2 and len(comp_b) >= 2 and len(anys) >= 2:
                sd.update({"CComponentSlot_size_kind": slot_b[0], "CComponentSlot_type_kind": slot_b[1],
                           "CComponentTemplate_size_kind": comp_b[0], "CComponentTemplate_type_kind": comp_b[1],
                           "kComponentSizeAny": anys[0], "kComponentTypeAny": anys[1]})
        need = ["CComponentSlot_size_kind", "CComponentSlot_type_kind", "CComponentTemplate_size_kind",
                "CComponentTemplate_type_kind", "kComponentSizeAny", "kComponentTypeAny",
                "CShipSize_flags", "CShipDesign_stages", "CShipGrowthStage_ship_size", "CShipGrowthStage_components",
                "CShipGrowthStage_component_count", "CShipGrowthStage_sections", "CShipGrowthStage_section_count",
                "CShipDesignSection_template", "CShipDesignSection_components", "CShipDesignSection_component_count",
                "CShipDesignComponent_size", "CShipDesignComponent_template", "CShipDesignComponent_slot",
                "CSectionTemplate_slots", "CSectionTemplate_slot_count", "CComponentSlot_size", "CComponentSlot_name",
                "CComponentTemplate_component_set"]
        if any(n not in sd for n in need) or sd["CShipDesignComponent_slot"] == sd["CShipDesignComponent_template"]:
            failures.append(f"ship design layout: {sd}")
        else:
            result["fields"].update(sd)
            print("ship design layout: " + ", ".join(f"{k}=0x{v:X}" for k, v in sd.items()))
    else:
        failures.append("ship design layout: designer functions missing")

    # CGoMIACommand's EMiaType: its serializer converts it with an out-of-line EnumToToken, which
    # win_extract cannot pair with the token; IsValid starts with `cmp dword ptr [rcx + X], 9`
    # (9 = no MIA type)
    sdk_cmds = json.loads((OUT / "sdk.json").read_text(encoding="utf-8"))["commands"]
    mia = next((c for c in sdk_cmds if c["token_name"] == "mia_command"), None)
    mia_type = None
    if mia:
        for i in im.disasm_fn(slot_fn(im, mia["vtable"], 8), 0x40)[:8]:
            m = re.search(r"^dword ptr \[rcx \+ (0x[0-9a-f]+)\], 9$", i.op_str)
            if i.mnemonic == "cmp" and m:
                mia_type = int(m.group(1), 16)
                break
    if mia_type is None:
        failures.append("mia_command mia_type: not found")
    else:
        result["fields"]["CGoMIACommand_mia_type"] = mia_type
        print(f"mia_command mia_type: 0x{mia_type:X}")

    # Event targets. CEventTarget: the "is event_target" and "? optional" bytes are the two adjacent
    # `cmp byte ptr [r13 + X], 0` flags in GetScope. CEventScope (CEventScope::Copy): root/from/prev
    # are the three `mov rax, [rdx + N]; mov [rcx + N], rax` pairs at the entry (from = the middle one),
    # the event target container is `mov rbx, [r15 + C]` right before `mov ecx, 0x58` (its allocation)
    gs_fn = funcs.get("CEventTarget_GetScope", {}).get("rva")
    copy_fn = funcs.get("CEventScope_Copy", {}).get("rva")
    if gs_fn and copy_fn:
        flags = sorted({int(m.group(1), 16) for i in im.disasm_fn(gs_fn, 0x6000)
                        for m in [re.search(r"^byte ptr \[r13 \+ (0x[0-9a-f]+)\], 0$", i.op_str)] if i.mnemonic == "cmp" and m})
        pair = [x for x in flags if x + 1 in flags]
        cins = im.disasm_fn(copy_fn, 0x200)[:60]
        links = [int(m.group(1), 16) for a, b in zip(cins, cins[1:])
                 for m in [re.search(r"^rax, qword ptr \[rdx \+ (0x[0-9a-f]+)\]$", a.op_str)]
                 if m and a.mnemonic == "mov" and b.mnemonic == "mov" and b.op_str == f"qword ptr [rcx + {m.group(1)}], rax"]
        cont = None
        for k, i in enumerate(cins):
            m = re.search(r"^rbx, qword ptr \[r15 \+ (0x[0-9a-f]+)\]$", i.op_str)
            if i.mnemonic == "mov" and m and any(x.mnemonic == "mov" and x.op_str == "ecx, 0x58" for x in cins[k + 1:k + 5]):
                cont = int(m.group(1), 16)
                break
        if len(pair) != 1 or len(links) < 3 or cont is None:
            failures.append(f"event target fields: flags={flags} links={links} container={cont}")
        else:
            result["fields"].update({"CEventTarget_is_event_target": pair[0], "CEventTarget_optional": pair[0] + 1,
                                     "CEventScope_from": links[1], "CEventScope_event_targets": cont})
            print(f"event target fields: is_event_target=0x{pair[0]:X} from=0x{links[1]:X} container=0x{cont:X}")
    else:
        failures.append("event target fields: CEventTarget_GetScope / CEventScope_Copy missing")

    linux = json.loads((OUT / "linux_anchors.json").read_text(encoding="utf-8"))
    found = databases_by_folder(im, linux)
    bdb = result["globals"].get("TGameDatabase<CBuildingTypeDatabase>::_pInstance")
    if bdb is not None and found.get("TGameDatabase<CBuildingTypeDatabase>::_pInstance") not in (None, bdb):
        failures.append("TGameDatabase<CBuildingTypeDatabase>: folder rule disagrees with CalcBuildSpeed")
    for sym, g in found.items():
        result["globals"].setdefault(sym, g)
    print(f"script databases by folder: {len(found)}")
    try:
        known = {k: v["rva"] for k, v in globs.items()}
        known.update(result["globals"])
        refs = game_state_refs(im, linux, known)
        for sym, g in refs.items():
            if sym not in known:
                result["globals"][sym] = g
        print(f"CGameStateDatabase ref databases: {len(refs)}")
    except Fail as e:
        failures.append(str(e))

    # TPdxRef<X> databases and X's id offset from command ref fields: a command's IsValid resolves
    # its `ref<CX>` field with `mov rA, [rip + G]; ... mov rV, dword ptr [this' + field]; ...
    # cmp eax, [rA + 0x20]` (the slot count) and then `cmp dword ptr [obj + ID], rV` (the object's
    # own id). Every command naming CX must agree; G must be CX's known database or unnamed.
    known = {k: v["rva"] for k, v in globs.items()}
    known.update(result["globals"])
    taken = set(known.values())
    votes, id_votes = {}, {}
    for c in sdk_cmds:
        refs_f = {f["win_off"]: f["ref"] for f in c["fields"] if f["kind"] == "ref" and f.get("ref") and f["win_off"] is not None}
        if not refs_f:
            continue
        ins = im.disasm_fn(slot_fn(im, c["vtable"], 8), 0x800)[:300]
        for k, i in enumerate(ins):
            m = re.search(r"^(r\w+), qword ptr \[rip \+ 0x[0-9a-f]+\]$", i.op_str)
            if i.mnemonic != "mov" or not m:
                continue
            ra, g = m.group(1), rip_target(im, i)
            field = reg = None
            for j in ins[k + 1:k + 6]:
                mf = re.search(r"^(\w+), dword ptr \[r\w+ \+ (0x[0-9a-f]+)\]$", j.op_str)
                if j.mnemonic == "mov" and mf and int(mf.group(2), 16) in refs_f:
                    field, reg = int(mf.group(2), 16), mf.group(1)
                    break
            if field is None or not any(j.mnemonic == "cmp" and j.op_str == f"eax, dword ptr [{ra} + 0x20]"
                                        for j in ins[k + 1:k + 10]):
                continue
            t = refs_f[field]
            sym = f"TPdxRef<{t}>::_pDatabase"
            if sym in known and known[sym] != g:
                continue  # some other database load; not this field's lookup
            if sym not in known:
                votes.setdefault(t, set()).add(g)
            for j in ins[k + 1:k + 16]:
                mi = re.search(r"^dword ptr \[r\w+ \+ (0x[0-9a-f]+|\d+)\], " + reg + "$", j.op_str)
                if j.mnemonic == "cmp" and mi:
                    id_votes.setdefault(t, set()).add(int(mi.group(1), 0))
                    break
    added = 0
    for t, gs in sorted(votes.items()):
        sym = f"TPdxRef<{t}>::_pDatabase"
        if len(gs) == 1 and next(iter(gs)) not in taken:
            result["globals"][sym] = next(iter(gs))
            taken.add(next(iter(gs)))
            added += 1
    print(f"ref databases from command IsValid: {added} ({', '.join(sorted(t for t, g in votes.items() if len(g) == 1))})")
    # every other inlined lookup of a known ref database in the image votes too: `mov rA, [rip + G];
    # ... cmp eax, [rA + 0x20]; ... mov rB, [rA + 0x18]; mov rO, [rB + rI*8 + 8]; ... cmp dword ptr
    # [rO + ID], rV` (classes no command references, such as CDebris via special projects)
    text = im.img[im.text0:im.text1]
    db_of = {v: k[len("TPdxRef<"):-len(">::_pDatabase")] for k, v in {**known, **result["globals"]}.items()
             if k.startswith("TPdxRef<") and k.endswith(">::_pDatabase")}
    site_votes = {}
    for mt in re.finditer(rb"[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
        at = mt.start()
        g = im.text0 + at + 7 + struct.unpack_from("<i", text, at + 3)[0]
        t = db_of.get(g)
        if t is None:
            continue
        ins = list(im.md.disasm(im.img[im.text0 + at:im.text0 + at + 0x60], im.ib + im.text0 + at))[:18]
        if not ins or ins[0].mnemonic != "mov":
            continue
        ra = ins[0].op_str.split(",")[0]
        if not any(i.mnemonic == "cmp" and i.op_str.endswith(f"dword ptr [{ra} + 0x20]") for i in ins[1:8]):
            continue
        obj = None
        for i in ins[1:14]:
            mo = re.search(r"^(r\w+), qword ptr \[r\w+ \+ r\w+\*8 \+ 8\]$", i.op_str)
            if i.mnemonic == "mov" and mo:
                obj = mo.group(1)
                continue
            mi = re.search(r"^dword ptr \[" + (obj or "@") + r" \+ (0x[0-9a-f]+|\d+)\], \w+$", i.op_str)
            if obj and i.mnemonic == "cmp" and mi:
                site_votes.setdefault(t, collections.Counter())[int(mi.group(1), 0)] += 1
                break
    for t, cnt in site_votes.items():
        off, n = cnt.most_common(1)[0]
        if n >= 3 and n >= 0.9 * sum(cnt.values()):
            id_votes.setdefault(t, set()).add(off)
    ids = {t: next(iter(o)) for t, o in id_votes.items() if len(o) == 1}
    for t, off in ids.items():
        result["fields"][f"{t}_id"] = off
    print("object id offsets: " + ", ".join(f"{t}=0x{o:X}" for t, o in sorted(ids.items())))
    # Event options, from CEventWindow::Setup's call of the shown-options loop:
    # `lea rcx, [window + S]` (its scope), `movzx edx, byte ptr [window + F]` (the flag it passes),
    # `lea r8, [event + O]` (the event's options array); and the option's name object, which the
    # button builder passes to CEventOption::GetName as `lea rcx, [option + N]`
    setup = funcs.get("CEventWindow_Setup", {}).get("rva")
    each = funcs.get("CEventWindow_ForEachShownOption", {}).get("rva")
    got = {}
    if setup and each:
        ins = im.disasm_fn(setup, 0x4000)
        for k, i in enumerate(ins):
            if i.mnemonic == "call" and i.op_str.startswith("0x") and int(i.op_str, 16) - im.ib == each:
                for j in ins[max(0, k - 8):k]:
                    o = f"{j.mnemonic} {j.op_str}"
                    m = re.match(r"^lea rcx, \[r\w+ \+ (0x[0-9a-f]+)\]$", o)
                    if m:
                        got["CEventWindow_scope"] = int(m.group(1), 16)
                    m = re.match(r"^movzx edx, byte ptr \[r\w+ \+ (0x[0-9a-f]+)\]$", o)
                    if m:
                        got["CEventWindow_option_flag"] = int(m.group(1), 16)
                    m = re.match(r"^lea r8, \[r\w+ \+ (0x[0-9a-f]+)\]$", o)
                    if m:
                        got["CEvent_options"] = int(m.group(1), 16)
                break
    add = funcs.get("CEventWindow_AddOptionButton", {}).get("rva")
    get_name = funcs.get("CEventOption_GetName", {}).get("rva")
    if add and get_name:
        ins = im.disasm_fn(add, 0x1400)
        for k, i in enumerate(ins):
            if i.mnemonic == "call" and i.op_str.startswith("0x") and int(i.op_str, 16) - im.ib == get_name:
                for j in ins[max(0, k - 6):k]:
                    m = re.match(r"^lea rcx, \[r\w+ \+ (0x[0-9a-f]+)\]$", j.op_str and f"{j.mnemonic} {j.op_str}")
                    if m:
                        got["CEventOption_name"] = int(m.group(1), 16)
                break
    if len(got) != 4:
        failures.append(f"event options: {got}")
    else:
        result["fields"].update(got)
        print("event options:", {k: hex(v) for k, v in got.items()})

    # CShipDesignImplementation: the vtable its constructor installs last, the deleting destructor slot
    # of that vtable (first slot that is one) and the object size the destructor frees
    ctor = funcs.get("CShipDesignImplementation_Ctor", {}).get("rva")
    got = {}
    if ctor:
        ins = im.disasm_fn(ctor, 0x200)
        vt = None
        for a, b in zip(ins, ins[1:]):
            if a.mnemonic == "lea" and a.op_str.startswith("rax, [rip + ") and b.mnemonic == "mov" \
                    and re.match(r"^qword ptr \[r\w+\], rax$", b.op_str):
                vt = rip_target(im, a)  # the last one is the derived class
        if vt:
            for k in range(0, 8):
                f = slot_fn(im, vt, k)
                if is_deleting_dtor(im, f):
                    got["slot"] = k
                    for i in im.disasm_fn(f, 0x100)[:24]:
                        m = re.match(r"^edx, (0x[0-9a-f]+)$", i.op_str)
                        if i.mnemonic == "mov" and m:
                            got["size"] = int(m.group(1), 16)
                    break
            got["vt"] = vt
    # the design id the constructor stores (`mov dword ptr [impl + D], <register holding edx>`) and
    # where the ship buildables keep their implementation (`lea rax, [impl vtable]; mov [this + K], rax`
    # in the constructors CreateBuildable calls)
    if ctor and got.get("vt"):
        ins = im.disasm_fn(ctor, 0x200)
        held = None
        for i in ins:
            m = re.match(r"^(e\w\w|r\d+d), edx$", i.op_str)
            if i.mnemonic == "mov" and m and held is None:
                held = m.group(1)
            m = re.match(r"^dword ptr \[r\w+ \+ (0x[0-9a-f]+)\], (\w+)$", i.op_str)
            if i.mnemonic == "mov" and m and held and m.group(2) == held:
                result["fields"]["CShipDesignImplementation_design"] = int(m.group(1), 16)
        create = funcs.get("NConstruction_CreateShipBuildable", {}).get("rva")
        offs = set()
        ship_vts = []
        if create:
            for i in im.disasm_fn(create, 0x1000):
                if i.mnemonic != "call" or not i.op_str.startswith("0x"):
                    continue
                callee = int(i.op_str, 16) - im.ib
                cins = im.disasm_fn(callee, 0x200)[:60]
                hit = False
                final_vt = None
                last_lea = None  # rax's vtable; an unrelated instruction may sit before its store
                for i2 in cins:
                    if i2.mnemonic == "lea" and i2.op_str.startswith("rax, [rip + "):
                        last_lea = rip_target(im, i2)
                    elif i2.mnemonic == "mov" and last_lea is not None and i2.op_str.endswith(", rax"):
                        m = re.match(r"^qword ptr \[r\w+ \+ (0x[0-9a-f]+|\d+)\], rax$", i2.op_str)
                        if last_lea == got["vt"] and m:
                            offs.add(int(m.group(1), 0))
                            hit = True
                        elif re.match(r"^qword ptr \[r\w+\], rax$", i2.op_str):
                            final_vt = last_lea  # the class's own vtable, stored last
                        last_lea = None
                if hit and final_vt:
                    ship_vts.append(final_vt)
        # a ship buildable's GetToken slot is `mov eax, TOKEN; ret` with one of the ship tokens (the
        # linear sweep may run into the next function, whose vtables are not)
        ship_tokens = {tokens.get(n) for n in ("buildable_ship", "buildable_federation_ship",
                                               "buildable_galactic_community_ship")} - {None}
        token_slot = result["slots"].get("CBuildableBase_GetToken")

        def buildable_token(vt):
            if token_slot is None:
                return None
            b = im.img[slot_fn(im, vt, token_slot):][:6]
            return struct.unpack_from("<I", b, 1)[0] if b[0] == 0xB8 and b[5] == 0xC3 else None

        # the vtables the objects end up with: CreateBuildable stores them at [object] itself after
        # each constructor returns (the constructors' own last stores are their base classes)
        ship_vts = []
        if create:
            last_lea = None
            for i2 in im.disasm_fn(create, 0x1000):
                if i2.mnemonic == "lea" and i2.op_str.startswith("rax, [rip + "):
                    last_lea = rip_target(im, i2)
                elif i2.mnemonic == "mov" and last_lea is not None and re.match(r"^qword ptr \[r\w+\], rax$", i2.op_str):
                    if last_lea not in ship_vts and buildable_token(last_lea) in ship_tokens:
                        ship_vts.append(last_lea)
                    last_lea = None
        if len(offs) == 1 and ship_vts and "CShipDesignImplementation_design" in result["fields"]:
            # the ship buildables' tokens (what their GetToken slot returns): a queue item's buildable
            # is a ship when its token is one of these
            for n in ("buildable_ship", "buildable_federation_ship", "buildable_galactic_community_ship",
                      "buildable_colony_ship"):
                if tokens.get(n) is not None:
                    result["fields"][f"Token_{n}"] = tokens[n]
            result["fields"]["CBuildableShip_implementation"] = offs.pop()
            print(f"ship buildable: implementation +0x{result['fields']['CBuildableShip_implementation']:X}, "
                  f"design +0x{result['fields']['CShipDesignImplementation_design']:X}")
        else:
            failures.append(f"ship buildable implementation: {offs}")
    # the colony ship buildable: the vtable the constructor CreateBuildable's colony overload calls
    # stores last at [this]
    colony_create = funcs.get("NConstruction_CreateColonyShipBuildable", {}).get("rva")
    colony_vts = set()
    if colony_create:
        for i in im.disasm_fn(colony_create, 0x400):
            if i.mnemonic != "call" or not i.op_str.startswith("0x"):
                continue
            last_lea, final_vt = None, None
            for i2 in im.disasm_fn(int(i.op_str, 16) - im.ib, 0x200)[:60]:
                if i2.mnemonic == "lea" and i2.op_str.startswith("rax, [rip + "):
                    last_lea = rip_target(im, i2)
                elif i2.mnemonic == "mov" and last_lea is not None and re.match(r"^qword ptr \[r\w+\], rax$", i2.op_str):
                    final_vt = last_lea
            if final_vt:
                colony_vts.add(final_vt)
    if len(colony_vts) != 1:
        failures.append(f"CBuildableColonyShip vtable: {[hex(v) for v in colony_vts]}")
    else:
        result["vtables"]["CBuildableColonyShip"] = colony_vts.pop()
        print(f"CBuildableColonyShip vtable 0x{result['vtables']['CBuildableColonyShip']:X}")
    if len(got) != 3:
        failures.append(f"CShipDesignImplementation layout: {got}")
    else:
        result["vtables"]["CShipDesignImplementation"] = got["vt"]
        result["slots"]["CShipDesignImplementation_Destroy"] = got["slot"]
        result["fields"]["CShipDesignImplementation_size"] = got["size"]
        print(f"CShipDesignImplementation vtable 0x{got['vt']:X}, destroy slot {got['slot']}, size 0x{got['size']:X}")

    # A design's ship class (EShipClass; 2 = colony ship) inside its implementation: CFleet::UpdateShipClass
    # copies the ship's `movzx R, byte ptr [ship + K]` into `mov byte ptr [fleet + ship_class], R`;
    # the ship embeds its implementation at CShip::ship_design_implementation
    sdk_text = (HERE.parent.parent / "stellaris_bridge" / "include" / "sdk" / "stellaris_sdk.hpp").read_text(encoding="utf-8")
    fleet_cls = re.search(r"namespace CFleet \{.*?ship_class = (0x[0-9A-F]+);", sdk_text, re.S)
    ship_impl = re.search(r"namespace CShip \{.*?ship_design_implementation = (0x[0-9A-F]+);", sdk_text, re.S)
    votes = collections.Counter()
    if fleet_cls and ship_impl:
        fc, si = int(fleet_cls.group(1), 16), int(ship_impl.group(1), 16)
        text = im.img[im.text0:im.text1]
        for m in re.finditer(re.escape(struct.pack("<I", fc)), text):
            at = im.text0 + m.start()
            ins = list(im.md.disasm(im.img[at - 24:at + 8], im.ib + at - 24))
            store = [i for i in ins if i.mnemonic == "mov" and re.match(r"^byte ptr \[r\w+ \+ " + hex(fc) + r"\], \w+$", i.op_str)]
            if not store:
                continue
            for i in ins:
                mm = re.match(r"^\w+, byte ptr \[r\w+ \+ (0x[0-9a-f]+)\]$", i.op_str)
                # a byte inside the ship's embedded implementation (CShipDesignImplementation_size)
                k = int(mm.group(1), 16) - si if mm else -1
                if i.mnemonic in ("movzx", "mov") and 0 < k < 0x1000:
                    votes[k] += 1
    if not votes:
        failures.append("CShipDesignImplementation ship class: no CFleet::UpdateShipClass copy")
    else:
        result["fields"]["CShipDesignImplementation_ship_class"] = votes.most_common(1)[0][0]
        print(f"implementation ship class +0x{votes.most_common(1)[0][0]:X} (votes {dict(votes)})")

    # TPdxNullObject<CColonyType>::_pInstance: SColonizationData's default designation, the global a
    # default-constructed one reads (`mov R, [rip + G]; mov [data + designation], R; mov ..., -1` for
    # the species) right after allocating it
    nulls = collections.Counter()
    text = im.img[im.text0:im.text1]
    for m in re.finditer(rb"\xB9\x18\x00\x00\x00\xE8", text):  # mov ecx, 0x18; call alloc
        at = im.text0 + m.start()
        ins = list(im.md.disasm(im.img[at:at + 0x40], im.ib + at))[:9]
        ops = [f"{i.mnemonic} {i.op_str}" for i in ins]
        joined = " | ".join(ops)
        if "+ 8], r" in joined and "0x10], " in joined and "0x14], 0" in joined and "0xffffffff" in joined:
            for i in ins:
                if i.mnemonic == "mov" and "qword ptr [rip + " in i.op_str and not i.op_str.startswith("qword"):
                    nulls[rip_target(im, i)] += 1
                    break
    if len(nulls) != 1:
        failures.append(f"TPdxNullObject<CColonyType>: {[hex(k) for k in nulls]}")
    else:
        result["globals"]["TPdxNullObject<CColonyType>::_pInstance"] = next(iter(nulls))
        print(f"TPdxNullObject<CColonyType> 0x{next(iter(nulls)):X}")

    # Economic scopes the megastructure costs are computed in: the CalcTable unit is {country id,
    # -1, scope, type}. The upgrade command's Execute takes `lea rdx, [megastructure + K]`, the
    # type's CanAfford `lea rcx, [country + K]`, both right before the call.
    calc_table = funcs.get("CMegaStructureType_CalcCostTable", {}).get("rva")
    can_afford = funcs.get("CMegaStructureType_CanAfford", {}).get("rva")
    sdk_json = json.loads((OUT / "sdk.json").read_text(encoding="utf-8"))
    up_vt = [c["vtable"] for c in sdk_json["commands"] if c["token_name"] == "upgrade_megastructure_command"]
    scopes = {}
    for name, fn, rx in (("CMegaStructure_economic_scope", im.q(up_vt[0] + 9 * 8) - im.ib if len(up_vt) == 1 else None,
                          r"^rdx, \[rbx \+ (0x[0-9a-f]+)\]$"),
                         ("CCountry_economic_scope", can_afford, r"^rcx, \[rdx \+ (0x[0-9a-f]+)\]$")):
        if fn is None or calc_table is None:
            continue
        last = None
        for i in im.disasm_fn(fn, 0x400):
            mm = re.match(rx, i.op_str) if i.mnemonic == "lea" else None
            if mm:
                last = int(mm.group(1), 16)
            if i.mnemonic == "call" and i.op_str.startswith("0x") and int(i.op_str, 16) - im.ib in (calc_table,) and last:
                scopes[name] = last
                break
            if i.mnemonic == "ret":
                break
    for name in ("CMegaStructure_economic_scope", "CCountry_economic_scope"):
        if name in scopes:
            result["fields"][name] = scopes[name]
            print(f"{name} +0x{scopes[name]:X}")
        else:
            failures.append(f"{name}: no lea before CalcTable")

    # A megastructure type's build type (0 at a planet, else a point in the system: the script's
    # inside / outside_gravity_well) and placement rules, as the build menu's per-planet check
    # reads them: `cmp dword ptr [type + K], 0` then `lea rcx, [type + K]` for CPlacementRules::IsPossible
    has_for_planet = funcs.get("CCountry_HasPotentiallyBuildableMegaStructureForPlanet", {}).get("rva")
    got_bt = {}
    if has_for_planet is not None:
        for i in im.disasm_fn(has_for_planet, 0x200):
            mm = re.match(r"^dword ptr \[rdi \+ (0x[0-9a-f]+)\], 0$", i.op_str) if i.mnemonic == "cmp" else None
            if mm and "CMegaStructureType_build_type" not in got_bt:
                got_bt["CMegaStructureType_build_type"] = int(mm.group(1), 16)
            mm = re.match(r"^rcx, \[rdi \+ (0x[0-9a-f]+)\]$", i.op_str) if i.mnemonic == "lea" else None
            if mm and "CMegaStructureType_placement_rules" not in got_bt:
                got_bt["CMegaStructureType_placement_rules"] = int(mm.group(1), 16)
            if i.mnemonic == "ret":
                break
    for name in ("CMegaStructureType_build_type", "CMegaStructureType_placement_rules"):
        if name in got_bt:
            result["fields"][name] = got_bt[name]
            print(f"{name} +0x{got_bt[name]:X}")
        else:
            failures.append(f"{name}: not in HasPotentiallyBuildableMegaStructureForPlanet")

    # Engine defines (NDefines) the bridge reads: each is registered by a thunk
    # `mov rcx, rdx; lea r9, [variable]; lea rdx, [category]; lea r8, [name]; jmp Read<T>`
    for define in ("DEEPSPACE_CITADEL_INNER_RADIUS_PERCENTAGE",):
        at = im.img.find(define.encode() + b"\x00", im.rdata0, im.rdata1)
        found = set()
        for r in (im.lea_index().get(at, []) if at != -1 else []):
            back = list(im.md.disasm(im.img[r - 0x10:r], im.ib + r - 0x10))
            lea_r9 = [i for i in back if i.mnemonic == "lea" and i.op_str.startswith("r9, [rip + ")]
            if lea_r9:
                found.add(rip_target(im, lea_r9[-1]))
        if len(found) == 1:
            result["globals"][f"NDefines::{define}"] = found.pop()
            print(f"define {define} 0x{result['globals'][f'NDefines::{define}']:X}")
        else:
            failures.append(f"define {define}: {[hex(v) for v in found]}")

    # Species rights and modification, from the code that uses them.
    layouts = json.loads((OUT / "win_layouts.json").read_text(encoding="utf-8"))["layouts"]
    cmds = json.loads((OUT / "sdk.json").read_text(encoding="utf-8")).get("commands", [])
    rights_vt = next((c["vtable"] for c in cmds if c.get("token_name") == "set_species_right_command"), None)

    # CCountry::GetSpeciesRightsModule, inlined into CCountrySetSpeciesRightsCommand::IsValid: the
    # member is loaded, a virtual check on it decides, and the same member is loaded again (else a
    # null object)
    module = set()
    if rights_vt:
        ins = im.disasm_fn(slot_fn(im, rights_vt, 8), 0x800)
        ops = [f"{i.mnemonic} {i.op_str}" for i in ins]
        for k in range(len(ops) - 5):
            m = re.match(r"^mov rcx, qword ptr \[(r\w+) \+ (0x[0-9a-f]+)\]$", ops[k])
            if (m and ops[k + 1] == "mov rax, qword ptr [rcx]" and ops[k + 2].startswith("call qword ptr [rax + ")
                    and ops[k + 3] == "test al, al" and ops[k + 4].startswith("je ")
                    and ops[k + 5] == f"mov rcx, qword ptr [{m.group(1)} + {m.group(2)}]"):
                module.add(int(m.group(2), 16))
    if len(module) != 1:
        failures.append(f"CCountry species rights module: {sorted(module)}")
    else:
        result["fields"]["CCountry_species_rights_module"] = module.pop()
        print(f"CCountry species rights module +0x{result['fields']['CCountry_species_rights_module']:X}")

    # CSpeciesRightsCountryConfiguration::CopySettingsFrom: per category `mov R, [rdx + P]; cmp [this
    # + P], R; mov dword ptr [this + D], eax` stamps the date the category may change again. Reached
    # from the rights command's vtable (its Execute calls it)
    pairs = {}
    if rights_vt:
        seen = set()
        for slot in range(4, 24):
            f = slot_fn(im, rights_vt, slot)
            for g in [f] + [int(i.op_str, 16) - im.ib for i in im.disasm_fn(f, 0x1000)
                            if i.mnemonic == "call" and i.op_str.startswith("0x")]:
                if g in seen or not (im.text0 <= g < im.text1):
                    continue
                seen.add(g)
                ops = [f"{i.mnemonic} {i.op_str}" for i in im.disasm_fn(g, 0x600)]
                found = {}
                for k in range(len(ops) - 3):
                    a = re.match(r"^mov (r\w+), qword ptr \[rdx \+ (0x[0-9a-f]+)\]$", ops[k])
                    if not a:
                        continue
                    b = re.match(r"^cmp qword ptr \[(r\w+) \+ (0x[0-9a-f]+)\], (r\w+)$", ops[k + 1])
                    st = k + 3 if ops[k + 2].startswith("je ") and k + 3 < len(ops) else k + 2
                    d = re.match(r"^mov dword ptr \[(r\w+) \+ (0x[0-9a-f]+)\], eax$", ops[st])
                    if b and d and b.group(2) == a.group(2) and b.group(3) == a.group(1) and b.group(1) == d.group(1):
                        found.setdefault(int(a.group(2), 16), int(d.group(2), 16))
                if len(found) >= 6:
                    pairs[g] = found
    conf = layouts.get("CSpeciesRightsCountryConfiguration::WriteMembers", {})
    names = {}
    for fld in conf.get("fields", []):
        if fld.get("found") and not fld["name"].startswith("former_"):
            names.setdefault(fld["win_off"], fld["name"])
    if len(pairs) != 1:
        failures.append(f"CSpeciesRightsCountryConfiguration::CopySettingsFrom: {[hex(g) for g in pairs]}")
    else:
        for ptr_off, date_off in sorted(pairs.popitem()[1].items()):
            if ptr_off in names:
                result["fields"][f"CSpeciesRightsCountryConfiguration_changed_{names[ptr_off]}"] = date_off
        print("species rights change dates:", {k: hex(v) for k, v in result["fields"].items() if "_changed_" in k})
        # the serializer writes these dates in another order than the categories, so pairing by
        # position put the wrong names on them: drop every last_changed_<category> the code contradicts
        bad = []
        for fld in conf.get("fields", []):
            if not fld["name"].startswith("last_changed_"):
                continue
            category = fld["name"][len("last_changed_"):].removesuffix("_type")
            real = result["fields"].get(f"CSpeciesRightsCountryConfiguration_changed_{category}")
            if real is None or real != fld.get("win_off"):
                bad.append(fld["name"])
        if bad:
            result.setdefault("rejected_fields", {})["CSpeciesRightsCountryConfiguration"] = bad
            print("rejected serializer fields:", bad)
    # the right types the configuration points to are written as their key
    key = {fld.get("indirect_off") for fld in conf.get("fields", []) if fld.get("kind") == "string" and fld.get("found")}
    key.discard(None)
    if len(key) != 1:
        failures.append(f"species right type key: {key}")
    else:
        result["fields"]["CSpeciesRightType_key"] = key.pop()
    # every engine call of CSpeciesRightBase::IsAllowed passes the right type's CSpeciesRightBase
    # part: `lea rcx, [type + B]` right before the call
    allowed = funcs.get("CSpeciesRightBase_IsAllowed", {}).get("rva")
    votes = collections.Counter()
    if allowed:
        for site in find_calls_to(im, allowed):
            for i in list(im.md.disasm(im.img[site - 0x30:site + 5], im.ib + site - 0x30)):
                m = re.match(r"^rcx, \[r\w+ \+ (0x[0-9a-f]+)\]$", i.op_str)
                if i.mnemonic == "lea" and m:
                    votes[int(m.group(1), 16)] += 1
    if not votes or (len(votes) > 1 and votes.most_common(2)[1][1] * 4 > votes.most_common(1)[0][1]):
        failures.append(f"CSpeciesRightType right base: {votes}")
    else:
        result["fields"]["CSpeciesRightType_right_base"] = votes.most_common(1)[0][0]
        print(f"species right type: key +0x{result['fields']['CSpeciesRightType_key']:X}, "
              f"CSpeciesRightBase +0x{result['fields']['CSpeciesRightType_right_base']:X}")

    # CTraitSet::WriteMembers: `movsxd R, dword ptr [rcx + N]` count, `mov rax, [set + D]` data,
    # and each trait written as its key `lea rdx, [trait + K]`
    ts = layouts.get("CTraitSet::WriteMembers", {})
    tfn = ts.get("win_fn")
    got = {}
    if tfn:
        for i in im.disasm_fn(tfn, 0x400)[:80]:
            o = f"{i.mnemonic} {i.op_str}"
            m = re.match(r"^movsxd r\w+, dword ptr \[rcx \+ (0x[0-9a-f]+)\]$", o)
            if m and "count" not in got:
                got["count"] = int(m.group(1), 16)
            m = re.match(r"^mov rax, qword ptr \[r\w+ \+ (0x[0-9a-f]+)\]$", o)
            if m and "count" in got and "data" not in got:
                got["data"] = int(m.group(1), 16)
            m = re.match(r"^lea rdx, \[r\w+ \+ (0x[0-9a-f]+)\]$", o)
            if m and "data" in got and "key" not in got:
                got["key"] = int(m.group(1), 16)
    if len(got) != 3:
        failures.append(f"CTraitSet layout: {got}")
    else:
        result["fields"]["CTraitSet_traits_data"] = got["data"]
        result["fields"]["CTraitSet_traits_count"] = got["count"]
        result["fields"]["CTrait_key"] = got["key"]
        print(f"CTraitSet traits data +0x{got['data']:X} count +0x{got['count']:X}; CTrait key +0x{got['key']:X}")

    # CTraitDatabase::_pInstance: the trait array {data, capacity, size} its readers walk
    tdb = result["globals"].get("CTraitDatabase::_pInstance") or globs.get("CTraitDatabase::_pInstance")
    arr = collections.Counter()
    if tdb:
        text = im.img[im.text0:im.text1]
        for m in re.finditer(rb"[\x48\x4C]\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
            at = im.text0 + m.start()
            if at + 7 + struct.unpack_from("<i", im.img, at + 3)[0] != tdb:
                continue
            ins = list(im.md.disasm(im.img[at:at + 0x30], im.ib + at))
            reg = ins[0].op_str.split(",")[0]
            loads = [re.match(r"^(\w+), (q|d)word ptr \[" + reg + r" \+ (0x[0-9a-f]+)\]$", i.op_str) for i in ins[1:6]
                     if i.mnemonic in ("mov", "movsxd")]
            q = {int(x.group(3), 16) for x in loads if x and x.group(2) == "q"}
            d = {int(x.group(3), 16) for x in loads if x and x.group(2) == "d"}
            for off in q:
                if off + 0xC in d:
                    arr[off] += 1
    if not arr:
        failures.append("CTraitDatabase trait array: no reader")
    else:
        result["fields"]["CTraitDatabase_traits"] = arr.most_common(1)[0][0]
        print(f"CTraitDatabase traits at +0x{arr.most_common(1)[0][0]:X} (votes {dict(arr)})")

    # NSpeciesModification::SSpeciesColonyPair: the element vtable the special project's colony list
    # is built of
    pair = layouts.get("NSpeciesModification::SSpeciesColonyPair::WriteMembers", {})
    pvt = (pair.get("vtables") or [[None]])[0][0]
    if pvt is None:
        failures.append("SSpeciesColonyPair vtable")
    else:
        result["vtables"]["NSpeciesModification_SSpeciesColonyPair"] = pvt
        print(f"SSpeciesColonyPair vtable 0x{pvt:X}")

    # the engine's Dear ImGui (sdk::glob::GImGui & co, sdk::rt::ImGui*)
    ig, ifl, ibad = imgui_internals(im, funcs)
    result["globals"].update(ig)
    result["fields"].update(ifl)
    failures += [f"ImGui: {p}" for p in ibad]

    have = set(result["globals"]) | set(globs)
    failures += [f"{sym}: not located" for sym in REQUIRED if sym not in have]
    for sym in REQUIRED:
        if sym in result["globals"]:
            print(f"  {sym}: 0x{result['globals'][sym]:X}")

    (OUT / "anchors.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    if failures:
        print("FAILED:", *failures, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
