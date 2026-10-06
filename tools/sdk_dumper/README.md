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
| `win_extract.py` | `stellaris.exe` | Windows function per class (token fingerprint, or vtable slot 20 for commands: a command's vtable is the one whose slot 10 returns its token and whose name is the class name in snake case, read from the exe, nothing kept between patches). The decompile's token numbers are renumbered to the exe's by name first (`linux_tokens.json` holds the names of the decompile's build): a patch that registers new tokens shifts every later number (4.5.2 moved 4141), and `out/linux_index_win.json` is the decompile's classes with the exe's numbers, which `emit_sdk.py` reads; `validate.py` maps its ground truth the same way; field offsets from register data flow (a value passed straight to the writer, `lea/mov r8|r9,[this+d]; mov edx,TOKEN; call`, wins over loads after the call; a field Linux writes as the key of a pointed-to object takes the `[this+d]` load read after its token); `this` adjust from the ctor's vtable store; token names from `RegisterToken` |
| `emit_sdk.py` | the above | commands (vtable = slot 10 `mov eax,TOKEN; ret`, factory, `kSize`; a function that reads its first argument through (a clone or copy constructor) is never taken for the factory, so a command whose only allocator is a clone gets `kFactoryRva = 0` instead of a function that would copy from garbage; the payload comes from the command's own serializer, slot 20, when the class's fingerprint matched a sibling with the same tokens), flattened entities, header |
| `globals.py` | Linux method bodies ↔ Windows command vtable slots | `TPdxRef<T>::_pDatabase` and other named globals, by co-occurrence voting; `TOKEN_BRANCH_HINTS` pins a database to the `ReadCommandMember` branch of its token when one command reads several |
| `live_verify.py` | running game (ReadProcessMemory only) | picks between tied database candidates by object type, and checks `ref<T>` fields |
| `functions.py` | `stellaris.exe` | engine functions the bridge calls (`sdk::fn`), each by one fingerprint: instruction pattern, referenced strings, a call next to an anchor instruction, a command vtable slot's call, or a function that installs a class's serializer vtable (`vtable_ref`: the class's constructor, or the call before `test sil, 1` in its deleting destructor). `identical_ok` accepts byte-identical copies the linker kept. `call_in` takes the call next to an anchor inside a function located earlier in the table (`HasFreeSpeciesTraitPoints` → `CalcFreeTraitPoints` → `CTrait::GetCost`), or inside a command's own vtable slot (`{"command": token name, "slot": 8 IsValid / 9 Execute}`: the megastructure cost table from the upgrade command's Execute), and `call_near` accepts `require` regexes over the target's opening instructions. Helpers the bridge once called by hand-written address (PostCommand, PdxLocalize, CString assign / free, CTraitSet::SetTraits, the CSpecies copy constructor and destructor, the leader name formatter) and the UI entry points the bridge drives (`CMessage::LeftClick`, `CAlertManager::Click`, `CStartScreenWindow::Close`) live here too |
| `anchors.py` | the above | data never touched by commands, followed from located code: buildable vtables and their virtual slot indices (`sdk::vt`), the building type / zone / strategic resource databases; script databases by the `common/<folder>` path their loader references (`TGameDatabase<CX>::_pInstance` stored by the inlined `CreateInstance`, or `CX::_pInstance` read first by `Init`); `TPdxRef<X>` databases by aligning Windows' inlined `CGameStateDatabase` constructor stores with the Linux order, anchored on known databases; fleet paths from `DrawMovementDebugLines` (`"ETA %.1f days"`): the `CFleetPath`, node-array and `CCelestialCoordinate` vtables stored around the `CFleetPath::Create` call, node count / array / stride, the fleet's coordinate interface (`sdk::rt::CFleet_coordinate_base`, slot `sdk::vt::CFleet_GetCoordinate`), and the node's jump method and bypass as `CalcEstimatedDays` and `Create` copy them to and from a `CFTLJump`. `TPdxRef<X>` databases the other rules miss, and every referenced class's id offset (`sdk::rt::<Class>_id`), from the lookup of a command's `ref<CX>` field in its `IsValid` and from a vote over every inlined lookup of a known database in the image; the ship design layout (stages, sections, slots, required components, slot size / type, component set, hull flags) from the designer's own edit path (`CShipDesignerBase::SetComponentOnSlot`, `CShipDesignSection::SetComponentOnSlot`, `CSectionTemplate::GetComponentSlot`, `CShipGrowthStage::UpdateResources`, `CShipDesign::CalcLongName`, `ComponentIsAllowedOnSlot`, `IsValidToSaveForCountry`); `CGoMIACommand`'s MIA type (its serializer converts it out of line) from the first compare in its `IsValid`. The `CInGameIdler` window views (start screen, anomaly, first contact, alert icons): the member each is stored in by the idler's builder and its vtable (`sdk::rt::CInGameIdler_<Class>`, `sdk::vt::<Class>`), found by the gui window name its constructor references, and the `CGuiView::Hide` slot as the one `CAnomalyWindow` overrides with `OnLeaveBe`'s body; the modifier definition array (`sdk::glob::CModifier_Definitions`, size at `+0xC`) that `CModifier::LogDefinitions` walks. Shipyard: `CShipDesignImplementation` (vtable, deleting-destructor slot and size from its constructor, the design id field, and the ship class byte via `CFleet::UpdateShipClass`), where a ship buildable keeps its implementation, the buildable tokens a queue item is classified by, the `CBuildableColonyShip` vtable and `TPdxNullObject<CColonyType>` (a colony ship's default designation). Megastructures: the economic scopes costs are computed in (`sdk::rt::CMegaStructure_economic_scope` from the upgrade command's Execute, `CCountry_economic_scope` from `CMegaStructureType::CanAfford`), and a type's build type and placement rules (`CMegaStructureType_build_type` / `_placement_rules`, from `CCountry::HasPotentiallyBuildableMegaStructureForPlanet`). Engine defines (`sdk::glob::NDefines_<NAME>`) from their registration thunk (`lea r9, [variable]; ...; lea r8, [name]; jmp Read<T>`). Event options: from `CEventWindow::Setup`'s call of the shown-options loop, the window's scope and option flag and the event's options array (`sdk::rt::CEventWindow_scope`, `CEventWindow_option_flag`, `CEvent_options`), and from the button builder the option's name object (`CEventOption_name`); with `FindMatchingPotentialExclusiveOptionIndex`, `IsPotentialIgnoreExclusive`, `GetName` and `IsAllowedSkipPotential` the bridge lists the options any event window type shows (standard and leader story). Species: the country's species rights module (`sdk::rt::CCountry_species_rights_module`, from the inlined `GetSpeciesRightsModule` in the rights command's `IsValid`), the date each rights category may change again (`CSpeciesRightsCountryConfiguration_changed_<category>`, from `CopySettingsFrom`, which the rights command calls; the serializer pairs these dates by position and gets them wrong), the right type's key and `CSpeciesRightBase` part (from every engine call of `IsAllowed`), the trait set's array and a trait's key (`CTraitSet::WriteMembers`), the trait database's array, and the `SSpeciesColonyPair` element vtable. `win_extract.py` counts a token load in a chained `.pdata` fragment (`UNW_FLAG_CHAININFO`) for the function it belongs to, so a serializer MSVC split into fragments (e.g. `CDistrict`) still matches; it also treats a call or tail jump into a writer helper that emits its own key (`mov edx, TOKEN; call WriteToken` at its start, e.g. an inlined `WriteUniform<T>`) as that key's event when `rcx` is the writer, and pairs a value handed straight to the writer when register moves sit between the `lea` and the token, and treats a tail `jmp` into the writer after the epilogue as a writer call. A field written as an enum token (`mov ecx, [this+d]; test ecx, ecx; je; sub ecx, 1; ... mov ebx, TOKEN_k`, then the key and `mov edx, ebx`) belongs to the key that follows the switch; the load must be switched on as a register value before any call or reload. Where an anchor and a `globals.py` vote put different names on one address, `emit_sdk.py` keeps the anchor |
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

## Coverage and limits (4.5.2)

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

