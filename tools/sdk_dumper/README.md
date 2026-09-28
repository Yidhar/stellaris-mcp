# Stellaris SDK dumper

Generates `stellaris_bridge/include/sdk/stellaris_sdk.hpp`: Windows object layouts, command
layouts/factories and engine globals, derived from the installed `stellaris.exe`. Run it after
every game patch instead of re-locating offsets by hand:

```powershell
python tools/sdk_dumper/dump.py               # full run; live checks if the game is running
python tools/sdk_dumper/dump.py --skip-linux  # reuse out/linux_index.json (source/ not needed)
python tools/sdk_dumper/dump.py --no-live     # static only
```

A run takes about 1.5 minutes. It fails if `validate.py` no longer reproduces the
hand-verified layouts.

## How it works

Clausewitz has no reflection, but every persisted class and every command has a serializer
(`WriteMembers` / `WriteCommandMembers`). The serializer writes `token -> this+offset` pairs,
and the tokens are the same numbers on Linux and Windows.

| Stage | Input | Output |
|---|---|---|
| `linux_index.py` | `source/stellaris_4.5_source.cpp` (symbolized Linux decompile) | class name → ordered `(token, kind, ref type)` |
| `win_extract.py` | `stellaris.exe` | Windows function per class (token fingerprint, or vtable slot 20 for commands); field offsets from register data flow; `this` adjust from the ctor's vtable store; token names from `RegisterToken` |
| `emit_sdk.py` | the above | commands (vtable = slot 10 `mov eax,TOKEN; ret`, factory, `kSize`), flattened entities, header |
| `globals.py` | Linux method bodies ↔ Windows command vtable slots | `TPdxRef<T>::_pDatabase` and other named globals, by co-occurrence voting |
| `live_verify.py` | running game (ReadProcessMemory only) | picks between tied database candidates by object type, and checks `ref<T>` fields |
| `validate.py` | `out/win_layouts.json` | regression against layouts verified by hand |

Linux offsets are only a reference. GCC and MSVC lay classes out differently (commands start
their payload at `+0x1C` on Linux and `+0x20` on Windows), so never copy offsets from `source/`.

## Using the header

```cpp
#include "sdk/stellaris_sdk.hpp"
// entity field (absolute offset from the object pointer stored in the database)
uint32_t colony_id = *(uint32_t*)(planet + sdk::ent::CPlanet::colony);
// database global
void* db = *(void**)(base + sdk::db::CPlanet);
// command: build it with the engine factory, then write the payload
auto make = (void*(*)())(base + sdk::cmd::research_technology_command::kFactoryRva);
```

Check `sdk::kExeTimestamp` against the running exe's PE `TimeDateStamp` at startup, and refuse
to dispatch commands when they differ.

## Coverage and limits (4.5.1)

- 809/980 serializers matched and 3143/3442 fields resolved. Core classes (`CCountry`,
  `CPlanet`, `CColony`, `CFleet`, `CShip`, `CLeader`, `CSpecies`, queues, ...) are 85–100%.
- 367 commands with factory and size; 308 of them also have a C++ class name.
- 62 globals, 22 of them databases type-checked live.
- Only serialized fields exist. Runtime caches and UI state still need other anchors.
- `persistent` fields are nested objects (for example `STradeData`) whose inner layout is not
  expanded yet.
- Fields annotated `[check: ...]` were paired by position only. Verify them before relying on
  them.
