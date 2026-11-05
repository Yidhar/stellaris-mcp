"""node_trace_report.py [log] [window_index]: the last (or the given, 0-based from the end) node_trace window of the
plugin log, with the sites annotated by their containing function (Windows 4.5.2 exe) and known names, per-day
averages for the normal-day rows, the rebuild table by category and the category names (from the calc each delegate calls)."""
import re
import runpy
import sys
from collections import defaultdict

args = sys.argv[1:]
LOG = next((a for a in args if not a.lstrip("-").isdigit()), r"D:\stellaris-perf\build\plugin\stellaris-perf\logs\stellaris_perf.log")
IDX = next((int(a) for a in args if a.lstrip("-").isdigit()), 0)
sys.argv = sys.argv[:1]
wx = runpy.run_path(r"D:\stellarismcp\tools\sdk_dumper\win_extract.py", run_name="x")
im = wx["Image"](wx["EXE"])
names = {}
hdr = open(r"D:\stellarismcp\stellaris_bridge\include\sdk\stellaris_sdk.hpp", encoding="utf-8").read()
fn_ns = hdr[hdr.index("namespace fn {"):hdr.index("}  // namespace fn")]
for m in re.finditer(r"inline constexpr uintptr_t (\w+) = (0x[0-9A-Fa-f]+);", fn_ns):
    names[int(m.group(2), 16)] = m.group(1)
KNOWN = {0x2605F0: "CGameState::MonthlyUpdate", 0x25BDB0: "CGameState::DailyUpdate", 0x252CC0: "CGameState::MicroUpdate",
         0xE3BFA0: "NColonyUpdate::MonthlyUpdate", 0xDC43E0: "CColony::MonthlyUpdateSerial", 0xDC09A0: "CColony::ApplyMonthlyGrowth",
         0xDC1920: "CColony::ApplyMonthlyDecline", 0x67E240: "CPopGroup::MoveSizeFrom", 0xDB0BB0: "CColony::AccessOrAddPopGroupAddSize",
         0xB36CC0: "NPlanetJobs::AssignPopsToJobs(core)", 0xB25DF0: "NPlanetJobs::UpdatePopJobs", 0x6F5510: "CCountry::MonthlyUpdateSerial",
         0x3F9F50: "COnActionDatabase::PerformEvent", 0x2636C0: "CGameState::ProcessAI", 0x238300: "CModifierNodeBase::Update",
         0xDACEF0: "CColony::InvalidateModifier", 0x239740: "CModifierNodeBase::Invalidate(child)", 0x238430: "CModifierNodeBase::InvalidateChildren",
         0xDAD840: "CColony::CalculateModifier", 0xE04060: "CPlanet::CalculateModifier", 0x67B980: "CPopGroup::CalculateModifier",
         0x29D9D0: "CModifierNodeManager::Update(flush)", 0x699D60: "CPopFaction::UpdateSupportAndApproval", 0xB35F30: "AssignPopsToJobs(promotion tail)",
         0xDB0C60: "CColony::AccessOrAddPopGroupMoveSizeFrom", 0xDB0D20: "AccessOrAddPopGroupMoveSizeFrom(new group)",
         0x23EFA0: "CCountry::InvalidateModifier?(node+0x3150)", 0x709BF0: "CCountry::CalculateModifier?", 0x231B10: "InheritParent",
         0x231BC6: "InheritParent(variant)", 0x231C40: "InheritParent(variant2)", 0x377353: "AddModifierNode(variant)", 0x37365C: "AddModifierNode(variant2)"}
names.update(KNOWN)


def fname(rva):
    f = im.fn_of(rva)
    n = names.get(f)
    return f"{f:#x}" + (f" {n}" if n else "")


def callees(rva, limit=6):
    """direct call/jmp targets of the function at rva, in order"""
    out = []
    try:
        ins = im.disasm_fn(rva, 400)
    except Exception:
        return out
    for i in ins:
        if i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x"):
            t = int(i.op_str, 16) - im.ib
            if t not in out:
                out.append(t)
            if len(out) >= limit:
                break
    return out


lines = open(LOG, encoding="utf-8", errors="replace").read().splitlines()
starts = [i for i, l in enumerate(lines) if "node_trace: ---- window" in l]
if not starts:
    raise SystemExit("no node_trace window in the log")
s = starts[-1 - IDX]
e = next((i for i in range(s + 1, len(lines)) if "node_trace: ---- end" in lines[i]), len(lines) - 1)
win = [l[l.index("node_trace: ") + 12:] for l in lines[s:e + 1]]
m = re.search(r"window of (\d+) days", win[0])
days = int(m.group(1)) if m else 0
print(win[0])
print(win[1])
catname = {}
for l in win:
    m = re.match(r"delegate cat=(\d+) rva=0x([0-9A-F]+) (\w+)", l)
    if m:
        cat, rva = int(m.group(1)), int(m.group(2), 16)
        cs = callees(rva)
        tgt = next((c for c in cs if c in names), cs[0] if cs else None)
        label = names.get(tgt, f"{tgt:#x}" if tgt is not None else "?")
        catname.setdefault(cat, []).append(label)
        print(f"  cat {cat:>2}: delegate {rva:#x} {m.group(3)} -> {label}")


for l in win:
    m = re.match(r"calc cat=(\d+) rva=0x([0-9A-F]+)", l)
    if m:
        cat, rva = int(m.group(1)), int(m.group(2), 16)
        label = names.get(rva, names.get(im.fn_of(rva) if 0 < rva < 0x3000000 else 0, f"calc {rva:#x}"))
        catname[cat] = [label]
        print(f"  cat {cat:>2}: calc {rva:#x} {label}")
CATS = {0: "relay", 1: "country", 2: "system2", 3: "system3", 4: "system4", 5: "system5", 6: "system6", 7: "system7", 8: "leader",
        10: "galcom", 11: "espionage", 13: "spy_net", 16: "planet", 17: "colony", 19: "leader2", 20: "megastructure", 22: "pop_group",
        23: "fleet", 24: "starbase24", 25: "starbase25", 26: "starbase26", 27: "starbase27", 28: "starbase28", 29: "starbase29",
        30: "army", 31: "ship"}  # Windows = the Linux enum minus one from 3 on; checked for 16, 17, 22, 23, 30, 31 by the calc


def cn(cat):
    cat = int(cat)
    if cat in catname and not catname[cat][0].startswith("calc "):
        return catname[cat][0].split("::")[0].replace("CalculateModifier", "")[:18]
    return CATS.get(cat, "?")


rebuild = defaultdict(dict)
for l in win:
    m = re.match(r"rebuild (\S+) (\S+) cat=(\d+) n=(\d+) self_ms=([\d.]+) changed=(\d+) unchanged=(\d+) unknown=(\d+) direct=(\d+) cascade_only=(\d+)"
                 r" ok_unchanged=(\d+) ok_changed=(\d+) notok_unchanged=(\d+) notok_changed=(\d+) unflagged=(\d+)", l)
    if m:
        rebuild[(m.group(2), int(m.group(3)))][m.group(1)] = tuple(float(x) if "." in x else int(x) for x in m.groups()[3:])
        continue
    m = re.match(r"rebuild (\S+) (\S+) cat=(\d+) n=(\d+) self_ms=([\d.]+) changed=(\d+) unchanged=(\d+) unknown=(\d+) direct=(\d+) cascade_only=(\d+)"
                 r" cascade_unchanged=(\d+) cascade_changed=(\d+) unflagged=(\d+)", l)
    if m:  # the older format: cascade_unchanged / cascade_changed shown in the ok_* columns, no parent check
        v = [float(x) if "." in x else int(x) for x in m.groups()[3:]]
        rebuild[(m.group(2), int(m.group(3)))][m.group(1)] = tuple(v[:7] + [v[7], v[8], 0, 0, v[9]])
print(f"\nrebuilds per window ({days} days); ok_* = cascade-only nodes whose cascading parents were all rebuilt unchanged")
print(f"{'kind':8} {'cat':>3} {'class':18} {'where':13} {'n':>7} {'self_ms':>8} {'us/rb':>6} {'chg':>7} {'unchg':>7} {'unk':>5} {'direct':>7} {'casc':>7} {'ok_unch':>7} {'ok_chg':>6} {'no_unch':>7} {'no_chg':>6} {'unflag':>6}")
tot = defaultdict(float)
for (kind, cat), wh in sorted(rebuild.items(), key=lambda kv: -sum(v[1] for v in kv[1].values())):
    for where, (n, ms, ch, un, uk, di, co, ou, oc, nu, nc, uf) in sorted(wh.items()):
        print(f"{kind:8} {cat:>3} {cn(cat):18} {where:13} {n:>7} {ms:>8.1f} {ms / n * 1000:>6.1f} {ch:>7} {un:>7} {uk:>5} {di:>7} {co:>7} {ou:>7} {oc:>6} {nu:>7} {nc:>6} {uf:>6}")
        tot[(kind, where)] += ms
        tot[(kind, where, "n")] += n
        tot[(kind, where, "ou")] += ou
        tot[(kind, where, "oc")] += oc
        tot[(kind, where, "un")] += un
        tot[(kind, where, "ou_ms")] += ms / n * ou
print()
for k in sorted(set((a, b) for (a, b, *_) in tot)):
    kind, where = k
    per = f"  ({tot[k] / days:.2f} ms/day, {tot[(kind, where, 'n')] / days:.0f} rebuilds/day, skippable {tot[(kind, where, 'ou_ms')] / days:.2f} ms/day)" if kind == "normal" and days else ""
    print(f"total {kind:8} {where:13}: {tot[k]:8.1f} ms thread time, {tot[(kind, where, 'n')]:.0f} rebuilds, unchanged {tot[(kind, where, 'un')]:.0f},"
          f" skippable (cascade-only, parents unchanged) {tot[(kind, where, 'ou')]:.0f} = {tot[(kind, where, 'ou_ms')]:.1f} ms, wrong-if-skipped {tot[(kind, where, 'oc')]:.0f}{per}")
print()
for l in win:
    if l.startswith("planet "):
        print(l)
print("\nroots (InvalidateChildren at depth 0 with mask -1 = X::InvalidateModifier; caller = who called it):")
for l in win:
    m = re.match(r"root site=0x([0-9A-F]+) cat=(\d+) caller=0x([0-9A-F]+) caller2=0x([0-9A-F]+) (\S+) n=(\d+)", l)
    if m:
        site, c1, c2 = (int(m.group(i), 16) for i in (1, 3, 4))
        print(f"  {m.group(5):8} cat={m.group(2):>3} {cn(m.group(2)):14} n={m.group(6):>8}  in {fname(site)}  <- {fname(c1) if c1 else '-'}  <- {fname(c2) if c2 else '-'}")
print("\ndirect invalidations (AddInvalid sites):")
for l in win:
    m = re.match(r"direct site=0x([0-9A-F]+) cat=(\d+) (\S+) n=(\d+)", l)
    if m:
        rva = int(m.group(1), 16)
        print(f"  {m.group(3):8} cat={m.group(2):>3} {cn(m.group(2)):14} n={m.group(4):>8}  site {rva:#x} in {fname(rva)}")
print("\ncascades (child <- parent, root, who called the root's InvalidateModifier):")
by_root = defaultdict(int)
by_caller = defaultdict(int)
for l in win:
    m = re.match(r"cascade cat=(\d+) parent_cat=(\d+) root_cat=(\d+) caller=0x([0-9A-F]+) caller2=0x([0-9A-F]+) (\S+) n=(\d+)", l)
    if m:
        c1, c2 = int(m.group(4), 16), int(m.group(5), 16)
        n = int(m.group(7))
        print(f"  {m.group(6):8} {cn(m.group(1)):14} <- {cn(m.group(2)):14} root {cn(m.group(3)):14} n={n:>8}  <- {fname(c1) if c1 else '-'}  <- {fname(c2) if c2 else '-'}")
        by_root[(m.group(6), cn(m.group(3)))] += n
        by_caller[(m.group(6), cn(m.group(3)), fname(c1) if c1 else "-")] += n
print("\ncascaded nodes by root category:")
for k, n in sorted(by_root.items(), key=lambda kv: -kv[1]):
    print(f"  {k[0]:8} root {k[1]:14} n={n:>8}")
print("\ncascaded nodes by root category and the root's invalidator caller:")
for k, n in sorted(by_caller.items(), key=lambda kv: -kv[1])[:30]:
    print(f"  {k[0]:8} root {k[1]:14} n={n:>8}  caller {k[2]}")
print()
print(win[-1])
