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
| `linux_index.py` | `source/stellaris_4.5_source.cpp` (symbolized Linux decompile) | class name → ordered `(token, kind, ref type)`; `out/linux_anchors.json`: the function that loads each `"common/<folder>"` database, and the order in which `CGameStateDatabase` constructs its `TPdxRefDatabase<X>` members |
| `win_extract.py` | `stellaris.exe` | Windows function per class (token fingerprint, or vtable slot 20 for commands); field offsets from register data flow (a value passed straight to the writer, `lea/mov r8|r9,[this+d]; mov edx,TOKEN; call`, wins over loads after the call; a field Linux writes as the key of a pointed-to object takes the `[this+d]` load read after its token); `this` adjust from the ctor's vtable store; token names from `RegisterToken` |
| `emit_sdk.py` | the above | commands (vtable = slot 10 `mov eax,TOKEN; ret`, factory, `kSize`), flattened entities, header |
| `globals.py` | Linux method bodies ↔ Windows command vtable slots | `TPdxRef<T>::_pDatabase` and other named globals, by co-occurrence voting; `TOKEN_BRANCH_HINTS` pins a database to the `ReadCommandMember` branch of its token when one command reads several |
| `live_verify.py` | running game (ReadProcessMemory only) | picks between tied database candidates by object type, and checks `ref<T>` fields |
| `functions.py` | `stellaris.exe` | engine functions the bridge calls (`sdk::fn`), each by one fingerprint: instruction pattern, referenced strings, a call next to an anchor instruction, a command vtable slot's call |
| `anchors.py` | the above | data never touched by commands, followed from located code: buildable vtables and their virtual slot indices (`sdk::vt`), the building type / zone / strategic resource databases; script databases by the `common/<folder>` path their loader references (`TGameDatabase<CX>::_pInstance` stored by the inlined `CreateInstance`, or `CX::_pInstance` read first by `Init`); `TPdxRef<X>` databases by aligning Windows' inlined `CGameStateDatabase` constructor stores with the Linux order, anchored on known databases; fleet paths from `DrawMovementDebugLines` (`"ETA %.1f days"`): the `CFleetPath`, node-array and `CCelestialCoordinate` vtables stored around the `CFleetPath::Create` call, node count / array / stride, the fleet's coordinate interface (`sdk::rt::CFleet_coordinate_base`, slot `sdk::vt::CFleet_GetCoordinate`), and the node's jump method and bypass as `CalcEstimatedDays` and `Create` copy them to and from a `CFTLJump`. `TPdxRef<X>` databases the other rules miss, and every referenced class's id offset (`sdk::rt::<Class>_id`), from the lookup of a command's `ref<CX>` field in its `IsValid`; `CGoMIACommand`'s MIA type (its serializer converts it out of line) from the first compare in its `IsValid`. `win_extract.py` also pairs a value handed straight to the writer when register moves sit between the `lea` and the token, and treats a tail `jmp` into the writer after the epilogue as a writer call. Where an anchor and a `globals.py` vote put different names on one address, `emit_sdk.py` keeps the anchor |
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

### Notes

- Some command tokens have two vtables: the command and a prototype object whose slot 20 is `_purecall` and whose slot 12 clones from `this`. `win_extract.py` ignores a `_purecall` slot 20, and `emit_sdk.py` keeps the vtable with a real serializer, so `kFactoryRva` is the argument-less factory.
- `live_verify.py` treats an empty database (the save has no objects of that type) as unverified, not failed.

