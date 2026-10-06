"""Assemble the launcher plugin folder (DLL plugin spec v2) from a build, and optionally zip it.

The folder is what goes to Documents\\Paradox Interactive\\Stellaris\\plugins\\stellaris-mcp\\:

    stl-plugin.json          manifest (plugin/stl-plugin.json; --version overrides its version)
    stellaris_bridge.dll     the bridge (build/stellaris_bridge/Release)
    defaults/                default settings the launcher makes config/ from
    mcp-server/              the MCP server: dist/, package.json, the lockfile and install.cmd; its
                             dependencies are installed by the user (install.cmd), not shipped
    README.md                plugin/README.md

The zip holds the same files at its root, so it can be unpacked into plugins\\stellaris-mcp\\ or
installed with the launcher's "Install plugin". Fails when the manifest does not list the
stellaris.exe build the SDK was generated for.

Usage:
    python scripts/make_plugin.py [--out DIR] [--version 0.5.0] [--zip FILE] [--dll PATH]
    stl plugin install build/plugin/stellaris-mcp     (the Stellaris launcher's command line)
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "stellaris_bridge" / "include" / "sdk" / "stellaris_sdk.hpp"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=str(ROOT / "build" / "plugin" / "stellaris-mcp"))
    ap.add_argument("--version", help="plugin version; a release passes its tag vX.Y.Z and the manifest gets X.Y.Z "
                                      "(the launcher compares it with the latest release's tag); default: the manifest's")
    ap.add_argument("--dll", default=str(ROOT / "build" / "stellaris_bridge" / "Release" / "stellaris_bridge.dll"))
    ap.add_argument("--zip", help="also write this zip (the folder's files at its root)")
    ap.add_argument("--with-deps", action="store_true", help="also install the MCP server's dependencies (local use)")
    args = ap.parse_args()

    manifest = json.loads((ROOT / "plugin" / "stl-plugin.json").read_text(encoding="utf-8"))
    ts = re.search(r"kExeTimestamp = (0x[0-9A-Fa-f]+)", SDK.read_text(encoding="utf-8")).group(1)
    listed = [t.lower() for t in manifest["game"]["exe_timestamps"]]
    if ts.lower() not in listed:
        sys.exit(f"plugin/stl-plugin.json lists {listed}, but the SDK was generated for {ts}: update exe_timestamps")
    if args.version:
        manifest["version"] = args.version.lstrip("vV")
    if not re.fullmatch(r"\d+\.\d+\.\d+([-+][0-9A-Za-z.+-]+)?", manifest["version"]):
        sys.exit(f"version {manifest['version']!r} is not X.Y.Z: release tags are vX.Y.Z")

    dll = Path(args.dll)
    server = ROOT / "stellaris_mcp_server"
    for need in (dll, server / "dist" / "index.js"):
        if not need.exists():
            sys.exit(f"missing {need}: build the DLL (scripts/build.ps1) and the MCP server (npm run build) first")

    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / "stl-plugin.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    shutil.copy2(dll, out / manifest["dll"])
    shutil.copytree(ROOT / "plugin" / "defaults", out / "defaults")
    shutil.copy2(ROOT / "plugin" / "README.md", out / "README.md")
    # the MCP server without its dependencies: they are installed on the user's machine when wanted
    # (mcp-server\install.cmd: npm ci --omit=dev from the lockfile), not shipped in the plugin
    shutil.copytree(server / "dist", out / "mcp-server" / "dist")
    for f in ("package.json", "package-lock.json"):
        shutil.copy2(server / f, out / "mcp-server" / f)
    shutil.copy2(ROOT / "plugin" / "mcp-server" / "install.cmd", out / "mcp-server" / "install.cmd")
    if args.with_deps:
        subprocess.run("npm ci --omit=dev --ignore-scripts --no-audit --no-fund", cwd=out / "mcp-server", shell=True, check=True)
    print(f"plugin folder: {out} (version {manifest['version']}, game build {ts})")

    if args.zip:
        z = Path(args.zip)
        z.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in sorted(out.rglob("*")):
                if f.is_file():
                    zf.write(f, f.relative_to(out).as_posix())
        # <zip>.sha256 beside it ("<hex digest>  <name>"): the launcher's updater checks the download against it
        digest = hashlib.sha256(z.read_bytes()).hexdigest()
        Path(str(z) + ".sha256").write_text(f"{digest}  {z.name}\n", encoding="ascii")
        print(f"zip: {z} (sha256 {digest})")


if __name__ == "__main__":
    main()
