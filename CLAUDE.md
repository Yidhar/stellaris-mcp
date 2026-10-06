# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**`AGENTS.md` is mandatory reading.** Its rules (game launch, threading, progressive disclosure, reverse-engineering discipline) apply to all work here and are summarized below. Where they differ, AGENTS.md wins.

## What this is

An MCP server that lets AI agents read and play **Stellaris 4.5** (Windows x64). It has two halves:

1. `stellaris_bridge/`: a native C++20 DLL injected into `stellaris.exe`. It reads engine memory directly and dispatches native Clausewitz `C*Command` objects.
2. `stellaris_mcp_server/`: a TypeScript MCP server (stdio). Each MCP tool forwards to the DLL over the named pipe `\\.\pipe\stellaris_mcp_bridge`, using newline-delimited JSON-RPC 2.0.

## Commands

```powershell
# Build the DLL. Uses the vcvars64 path hard-coded in the script (F:\vss\...), VS 2022, CMake 3.20+.
# If the DLL is still loaded in the game, the script renames it to .dll.old to avoid LNK1104.
.\scripts\build.ps1                      # -> build/stellaris_bridge/Release/stellaris_bridge.dll

# Build the MCP server
cd stellaris_mcp_server; npm install; npm run build   # tsc -> dist/index.js  (npm run dev = watch)

# Launch the game (never go through Steam or the Paradox launcher; see below)
python scripts/launch_stellaris.py       # runs stellaris.exe -dx11 from the game dir
python scripts/launch_and_wait_ready.py

# Inject / hot-reload the DLL into a running game
python scripts/auto_inject.py            # wait for stellaris.exe, then inject
python scripts/reload_dll.py             # FreeLibrary the old DLL, inject the new build
python scripts/build_and_reload.py       # eject, MSBuild the existing build/ vcxproj, re-inject, ping
```

There is no unit-test framework. Tests are standalone Python scripts in `scripts/` that talk to the live pipe, so the game must be running with a save loaded and the DLL injected:

- `python scripts/test_pipe.py`: smoke test (ping, get_status, pause, speed).
- `python scripts/test_<feature>_pipeline.py` and `scripts/verify_*.py`: per-feature end-to-end checks against the raw pipe.
- `python scripts/test_mcp_*_end_to_end.py` / `test_mcp_stdio.py`: exercise the built MCP server over stdio.

The bridge is a Stellaris launcher plugin (`plugin/stl-plugin.json`, id `stellaris-mcp`). It writes its log to `logs\stellaris_mcp.log` and reads `config\stellaris_mcp.ini` in the folder the DLL was loaded from: `Documents\Paradox Interactive\Stellaris\plugins\stellaris-mcp\` when the launcher loaded it, `build\stellaris_bridge\Release\` when injected by hand with `scripts/reload_dll.py`. `python scripts/make_plugin.py` assembles the plugin folder from a build.

## Architecture

Request flow, which you need several files to see:

```
MCP client → tools.ts (zod schema, client.request(method, params))
          → pipe_client.ts (named pipe, per-request timeout)
          → ipc_server.cpp  (big if/else on `method`; parses params, then TaskQueue::Enqueue(lambda))
          → task_queue.cpp  (drained by ProcessAll() inside the hooked IDXGISwapChain::Present, hook_manager.cpp / MinHook)
          → <Domain>Manager::Get().Method()  (runs on the game's main render thread)
```

- `ipc_server.cpp` waits about 2.5s on each task future. A task that runs longer, or a game with no frames rendering, times out.
- Exceptions thrown in a task become JSON-RPC errors `-32001`/`-32002`. Raw memory reads are wrapped in SEH `__try/__except`, and a bad pointer must never crash the game.
- Every domain lives in a singleton `XxxManager` (`include/xxx_manager.hpp` + `src/xxx_manager.cpp`) with `Init(base_address)`, called from `dll_main.cpp`. `outliner_manager.cpp` (about 5.5k lines) covers planets, sectors, construction, jobs and armies.
- `commands.cpp` locates the engine's `PostCommand` by pattern scan, falling back to `kRvaPostCommand`. Command objects are allocated through the engine allocator (`kRvaEngineAlloc`) and posted from the main thread.
- `common.hpp` has `Logger` (`LOG`/`LOGF`) and `SafeLocalize` (calls the engine's localization function to turn loc keys into display strings).
- All addresses are **RVAs against the Stellaris 4.5 image base** (`GetModuleHandleA(nullptr)`). A game update breaks them.

### Adding a feature end-to-end
1. Reverse the data path or command (see below), then implement it in the relevant manager.
2. Add a `method == "..."` branch in `ipc_server.cpp` that enqueues the manager call.
3. Register an MCP tool in `stellaris_mcp_server/src/tools.ts`, following the existing `server.tool(name, description, zodSchema, handler)` pattern and `stellaris_` naming.
4. For a new `.cpp`, add it to `stellaris_bridge/CMakeLists.txt` and call its `Init` in `dll_main.cpp`.
5. Add a `scripts/test_<feature>_pipeline.py` that calls the pipe method against the live game.

## Hard rules (from AGENTS.md)

- **Launching the game:** run `E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe -dx11` with that directory as the working directory, or use `scripts/launch_stellaris.py`. Never use `steam://run/281990`, `dowser.exe`, or the Paradox Launcher, because they block on a manual click.
- **No cross-thread calls into game UI or logic functions** (no `CreateRemoteThread` calls into engine functions such as `0x14d8d00`). They deadlock (`WAIT_TIMEOUT`) or crash the game silently. Only call engine code from TaskQueue tasks on the main thread.
- **Read state by traversing data directly**, starting from global `TPdxRef<T>::_pDatabase` managers. Don't drive the UI.
- **Progressive disclosure:** L1 tools return compact summaries (names, IDs, flags or counts such as `has_construction`, `alerts_count`). Full detail goes only in L2 tools scoped to one entity. Don't bloat L1 responses.
- **The invalid ID is `0xFFFFFFFF`, not 0.** ID 0 is valid: the player country, Earth's colony, Earth's surface queue. Always check `id != 0xFFFFFFFF`.
- **Normalize fixed-point values** before returning them. Progress and time are scaled by 100000 (for example, 360 days is stored as 36000000).
- **Verify database types.** Confirm the real `T` of each `TPdxRef<T>::_pDatabase` in the source, and don't guess from nearby addresses. For example, `+0x3113148` is `CSolarSystem` and `+0x3113128` is `CPlanet`. Mixing them up gives wrong results, such as reading a starbase queue as a planet's surface queue.
- **Before dispatching a Command**, reverse its `IsValid` and `ExecuteLocal` to get the stack/struct layout (vtable, action object, country ID, queue ID). The target queue ID must match the one the game UI binds to.

## Offsets: use the generated SDK

`stellaris_bridge/include/sdk/stellaris_sdk.hpp` is **generated** by `tools/sdk_dumper` from the installed exe. Don't edit it, and don't hand-copy RVAs or field offsets into managers. After every game patch, run `python tools/sdk_dumper/dump.py`: it takes about 1.5 minutes, and does live checks if the game is running. `tools/sdk_dumper/README.md` explains how it works. What the header contains:

- `sdk::ent::<Class>::<field>`: absolute Windows offsets. Names are the engine's save-file tokens. Sub-objects are flattened, for example `CPlanet::colony == 0xE0`.
- `sdk::cmd::<token_name>::{kToken, kVtableRva, kFactoryRva, kSize, <payload fields>}`: build commands with the engine factory and write only the payload. On Windows the payload starts at `+0x20`. The Linux decompile shows `+0x1C`, which is GCC layout. That mismatch is a known source of bugs.
- `sdk::db::<T>`: `TPdxRef<T>::_pDatabase` RVAs. The system database's type is `CGalacticObject`, not `CSolarSystem`.

- `sdk::fn::<name>`: engine functions the bridge calls, such as computed values and trigger checks. Each is located by a fingerprint in `tools/sdk_dumper/functions.py` (instruction pattern, mnemonics or referenced strings).
- `sdk::rt::<Class>_<field>`: offsets of runtime (not serialized) members, read out of located engine code by `anchors.py` (for example the system's cached owner).
- `sdk::vt::<Class>` / `sdk::vt::<Class>_<Method>`: engine vtables and virtual slot indices (for example the buildables `build_building` constructs, and their `CalcCost` / `CalcProgressionTimeNeeded` slots), plus globals no command touches, derived in `tools/sdk_dumper/anchors.py` from code that is already located (script databases such as `sdk::glob::TGameDatabase_CTraditionTypeDatabase_pInstance` by their `common/<folder>` path, and ref databases such as `sdk::db::CDeposit` / `CPopJob` / `CSituation` by `CGameStateDatabase` construction order). The bridge has no hard-coded `.data` addresses; add a rule there instead of writing one.

Dispatch commands through `CommandBuilder` (`command_builder.hpp`):
- `Create(sdk::cmd::X::kSpec)`, then `Set`/`SetString`/`SetBytes`, then `IsValid(&why)` (which returns the engine's own reason), then `Post(...)`.
- When a command embeds objects that need engine copy constructors, have the engine `Clone` a stack-built one and wrap the result with `Adopt(spec, obj)`.
- For const engine checks of the form `bool fn(self, a, b, CString* reason)`, use `CallPredicate`.

If `tools/sdk_dumper/validate.py` fails after a patch, fix the dumper's heuristics before you touch any manager.

## Reverse-engineering resources

- `source/stellaris_4.5_source.cpp` (about 320 MB, git-ignored) is the decompile of the **Linux** build. It has symbols, but its offsets are GCC layout and don't match Windows. **Never read it whole.** Stream-scan it with Python or `mmap` for RTTI names, `TPdxRef<C...>::_pDatabase`, or `C...Command::IsValid`, then slice out the lines around a match with `python scripts/slice_source.py <start_line> [count]`.
- `docs/REVERSE_ENGINEERING_CHEATSHEET.md` covers verified database base offsets, entity field offsets, the planet → colony → construction queue → item chain, and command stack-frame layouts. Check it before re-deriving offsets, and update it when you verify new ones.
- `scripts/` holds hundreds of one-off `disasm_*`, `inspect_*`, `probe_*`, `find_*` and `dump_*` exploration scripts, plus `scratch/` (git-ignored). They are throwaway research tools, not maintained code. Reuse their patterns, but don't treat them as the source of truth.
