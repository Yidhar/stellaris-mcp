"""Stage 4 (optional, needs the game running with a save loaded): verify the SDK against live
memory with ReadProcessMemory only -- no injection, no remote threads, no engine calls.

1. TPdxRef<T>::_pDatabase: for each candidate address, walk the database and check that the
   stored objects carry T's serializer vtable at [obj + this_adjust]. Picks the candidate that
   passes (settles static ties between a database and its TPdxNullObject twin).
2. Entity fields: for every ref<T> field of objects in a verified database, the value must be
   0xFFFFFFFF or an id that resolves in T's database. Reports a pass rate per field.

Usage: python tools/sdk_dumper/live_verify.py
Output: out/globals_verified.json, out/live_report.json
"""
import ctypes
import json
import re
import struct
import subprocess
from ctypes import wintypes
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
INVALID = 0xFFFFFFFF

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("th32ModuleID", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                ("GlblcntUsage", wintypes.DWORD), ("ProccntUsage", wintypes.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wintypes.DWORD), ("hModule", ctypes.c_void_p), ("szModule", wintypes.WCHAR * 256),
                ("szExePath", wintypes.WCHAR * 260)]


class Proc:
    def __init__(self):
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq stellaris.exe", "/FO", "CSV", "/NH"],
                             capture_output=True, text=True).stdout
        m = re.search(r'"stellaris.exe","(\d+)"', out)
        if not m:
            raise SystemExit("stellaris.exe is not running")
        self.pid = int(m.group(1))
        self.h = k32.OpenProcess(0x10 | 0x400, False, self.pid)  # VM_READ | QUERY_INFORMATION
        snap = k32.CreateToolhelp32Snapshot(0x18, self.pid)
        me = MODULEENTRY32W()
        me.dwSize = ctypes.sizeof(me)
        ok = k32.Module32FirstW(snap, ctypes.byref(me))
        self.base = None
        while ok:
            if me.szModule.lower() == "stellaris.exe":
                self.base = me.modBaseAddr
                break
            ok = k32.Module32NextW(snap, ctypes.byref(me))
        k32.CloseHandle(snap)

    def read(self, addr, n):
        if not addr or addr < 0x10000:
            return None
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            return None
        return buf.raw

    def q(self, a):
        b = self.read(a, 8)
        return struct.unpack("<Q", b)[0] if b else None

    def u32(self, a):
        b = self.read(a, 4)
        return struct.unpack("<I", b)[0] if b else None


def db_objects(p, db, limit=4096):
    """(slot index, object pointer) for live slots of a TPdxRefDatabase (arr[+0x18], cap[+0x20])."""
    arr, cap = p.q(db + 0x18), p.u32(db + 0x20)
    if not arr or cap is None or cap > 1 << 20:
        return []
    raw = p.read(arr, min(cap, limit) * 16)
    if not raw:
        return []
    out = []
    for i in range(min(cap, limit)):
        obj = struct.unpack_from("<Q", raw, i * 16 + 8)[0]
        if obj:
            out.append((i, obj))
    return out


def main():
    p = Proc()
    B = p.base
    sdk = json.loads((OUT / "win_layouts.json").read_text(encoding="utf-8"))["layouts"]
    globs = json.loads((OUT / "globals.json").read_text(encoding="utf-8"))

    # vtables that objects of class T carry at [obj + this_adjust]
    type_vt = {}
    for key, lay in sdk.items():
        if lay["method"] == "WriteMembers" and lay["this_adjust"] is not None and lay["vtables"]:
            type_vt[lay["class"]] = (lay["this_adjust"], {B + vt for vt, _ in lay["vtables"]})

    verified, dbs = {}, {}
    for sym, g in sorted(globs.items()):
        m = re.match(r"TPdxRef<(\w+)>::_pDatabase", sym)
        if not m:
            verified[sym] = {**g, "live": "not-checked"}
            continue
        T = m.group(1)
        if T not in type_vt:
            verified[sym] = {**g, "live": "no-type-vtable"}
            continue
        adj, vts = type_vt[T]
        best = None
        pick_empty = False
        for cand in g.get("candidates", [g["rva"]]):
            db = p.q(B + cand)
            objs = db_objects(p, db)[:256] if db else []
            if not objs:
                pick_empty = pick_empty or cand == g["rva"]
                continue
            hits = sum(1 for _, o in objs if p.q(o + adj) in vts)
            rate = hits / len(objs)
            if best is None or rate > best[0]:
                best = (rate, cand, len(objs))
        if best and best[0] >= 0.9:
            verified[sym] = {**g, "rva": best[1], "live": "ok", "live_rate": round(best[0], 3), "live_objects": best[2]}
            dbs[T] = p.q(B + best[1])
        else:
            # no live objects (main menu / empty database) is "unverified", not a failure
            # the voted pick being empty (this save has none of T) says nothing against it either
            verified[sym] = {**g, "live": "unverified" if best is None or pick_empty else f"failed ({best[0]:.2f})"}
    (OUT / "globals_verified.json").write_text(json.dumps(verified, indent=1), encoding="utf-8")
    ok = [s for s, v in verified.items() if v.get("live") == "ok"]
    moved = [s for s in ok if verified[s]["rva"] != globs[s]["rva"]]
    print(f"databases verified live: {len(ok)}; static pick corrected for: {moved}")

    # ---- field plausibility over live objects
    ids = {T: {struct_id for struct_id, _ in db_objects(p, db)} for T, db in dbs.items()}
    report = {}
    for T, db in dbs.items():
        lay = sdk.get(f"{T}::WriteMembers")
        if not lay:
            continue
        objs = db_objects(p, db)[:200]
        rows = []
        for f in lay["fields"]:
            if f["win_off"] is None or f["kind"] != "ref" or f["ref"] not in ids:
                continue
            good = total = 0
            for _, o in objs:
                v = p.u32(o + f["win_off"])
                if v is None:
                    continue
                total += 1
                if v == INVALID or (v & 0xFFFFFF) in ids[f["ref"]]:
                    good += 1
            if total:
                rows.append({"token": f["token"], "name": f["name"], "off": f["win_off"], "ref": f["ref"],
                             "pass": round(good / total, 3), "n": total})
        report[T] = rows
    (OUT / "live_report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    n = sum(len(r) for r in report.values())
    bad = [(T, r["name"], hex(r["off"]), r["pass"]) for T, rs in report.items() for r in rs if r["pass"] < 0.95]
    print(f"ref fields checked live: {n}; below 95% pass: {len(bad)}")
    for b in bad[:30]:
        print("   ", b)


if __name__ == "__main__":
    main()
