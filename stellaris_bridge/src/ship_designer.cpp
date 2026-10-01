#include "ship_designer.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "fleet_access.hpp"
#include "common.hpp"
#include "command_builder.hpp"
#include "game_state.hpp"
#include "outliner_manager.hpp"
#include <windows.h>
#include <algorithm>

namespace bridge {

static bool SafeReadPtr(const void* addr, void** out) {
    if (!addr || !out) return false;
    __try {
        *out = *(void**)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI64(const void* addr, int64_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const int64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI32(const void* addr, int32_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const int32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeWritePtr(void* addr, void* val) {
    if (!addr) return false;
    __try {
        *(void**)addr = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

static bool SafeReadPdxString(const void* pdx_str_addr, std::string& out) {
    out.clear();
    if (!pdx_str_addr) return false;

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.size == 0 || raw.size > 1024) return false;

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            out = std::string(temp, len);
            return true;
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 512 ? (size_t)raw.size : 512;
            std::string temp(len, '\0');
            if (SafeCopyChars(&temp[0], raw.heap_ptr, len)) {
                out = temp;
                return true;
            }
        }
    }
    return false;
}

static bool SafeWritePdxString(void* pdx_str_addr, const std::string& new_str) {
    if (!pdx_str_addr) return false;
    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.capacity < 16 && new_str.size() < 16) {
        char temp[16]{ 0 };
        memcpy(temp, new_str.data(), new_str.size());
        SafeCopyChars(raw.buf, temp, 16);
        raw.size = new_str.size();
        return SafeCopyChars((char*)pdx_str_addr, (const char*)&raw, sizeof(RawPdxString));
    } else if (raw.capacity >= 16 && raw.heap_ptr && new_str.size() <= raw.capacity) {
        SafeCopyChars(raw.heap_ptr, new_str.data(), new_str.size());
        raw.heap_ptr[new_str.size()] = '\0';
        raw.size = new_str.size();
        return SafeCopyChars((char*)pdx_str_addr, (const char*)&raw, sizeof(RawPdxString));
    }
    return false;
}

struct StringView {
    const char* data{ nullptr };
    size_t size{ 0 };
};

struct PdxCString {
    char meta[16]{ 0 };
    union {
        char buf[16]{ 0 };
        char* heap_ptr;
    };
    uint64_t size{ 0 };
    uint64_t capacity{ 15 };
};

namespace designs {
namespace rt = sdk::rt;
// CShipSize::ReadMember stores the script flag is_designable (token 0x31fe) as bit 1 of the flags
constexpr uint64_t kShipSizeDesignable = 1ull << 1;
// script keys: a ship size's at +0x20, a component template's at +0x1B0 (as the catalog reads them)
constexpr std::ptrdiff_t kShipSizeKey = 0x20;
constexpr std::ptrdiff_t kComponentKey = 0x1B0;
constexpr uint32_t kOwnerCountry = 0;  // EDesignOwner

template <typename T>
T Rd(uintptr_t addr, T fallback) {
    T v{};
    __try {
        v = *(const T*)addr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
    return v;
}

void* Stage0(void* design) { return design ? Rd<void*>((uintptr_t)design + rt::CShipDesign_stages, nullptr) : nullptr; }
void* SizeOf(void* stage) { return stage ? Rd<void*>((uintptr_t)stage + rt::CShipGrowthStage_ship_size, nullptr) : nullptr; }
bool Designable(void* size) { return size && (Rd<uint64_t>((uintptr_t)size + rt::CShipSize_flags, 0) & kShipSizeDesignable); }

std::vector<void*> Ptrs(uintptr_t data_field, uintptr_t count_field) {
    std::vector<void*> out;
    void* data = Rd<void*>(data_field, nullptr);
    int32_t n = Rd<int32_t>(count_field, 0);
    if (!data || n <= 0 || n > 256) return out;
    for (int32_t i = 0; i < n; ++i) out.push_back(Rd<void*>((uintptr_t)data + i * sizeof(void*), nullptr));
    return out;
}

std::vector<void*> Sections(void* stage) {
    return stage ? Ptrs((uintptr_t)stage + rt::CShipGrowthStage_sections, (uintptr_t)stage + rt::CShipGrowthStage_section_count)
                 : std::vector<void*>{};
}
std::vector<void*> Cores(void* stage) {
    return stage ? Ptrs((uintptr_t)stage + rt::CShipGrowthStage_components, (uintptr_t)stage + rt::CShipGrowthStage_component_count)
                 : std::vector<void*>{};
}

// the section template's component slots (CComponentSlot, inline)
std::vector<void*> SlotsOf(void* section) {
    std::vector<void*> out;
    void* tmpl = section ? Rd<void*>((uintptr_t)section + rt::CShipDesignSection_template, nullptr) : nullptr;
    if (!tmpl) return out;
    void* data = Rd<void*>((uintptr_t)tmpl + rt::CSectionTemplate_slots, nullptr);
    int32_t n = Rd<int32_t>((uintptr_t)tmpl + rt::CSectionTemplate_slot_count, 0);
    if (!data || n <= 0 || n > 128) return out;
    for (int32_t i = 0; i < n; ++i) out.push_back((void*)((uintptr_t)data + i * rt::CComponentSlot_size));
    return out;
}

// the component template installed in a slot of the section (nullptr: empty)
void* Installed(void* section, void* slot) {
    void* data = Rd<void*>((uintptr_t)section + rt::CShipDesignSection_components, nullptr);
    int32_t n = Rd<int32_t>((uintptr_t)section + rt::CShipDesignSection_component_count, 0);
    if (!data || n <= 0 || n > 128) return nullptr;
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t c = (uintptr_t)data + i * rt::CShipDesignComponent_size;
        if (Rd<void*>(c + rt::CShipDesignComponent_slot, nullptr) == slot) return Rd<void*>(c + rt::CShipDesignComponent_template, nullptr);
    }
    return nullptr;
}

struct Call {
    uintptr_t fn;
    void* a;
    void* b;
    void* c;
    bool result;
};

bool Guarded(void (*call)(void*, void*), Call* ctx) { return CommandBuilder::Get().CallGuarded(call, ctx); }

// CShipDesignSection::SetComponentOnSlot(section, component, slot)
bool SetComponent(void* section, void* component, void* slot) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::CShipDesignSection_SetComponentOnSlot, section, component, slot, false };
    return Guarded([](void* x, void*) {
        auto* k = (Call*)x;
        ((void (*)(void*, void*, void*))k->fn)(k->a, k->b, k->c);
    }, &c);
}

bool UpdateResources(void* stage) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::CShipGrowthStage_UpdateResources, stage, nullptr, nullptr, false };
    return Guarded([](void* x, void*) {
        auto* k = (Call*)x;
        ((void (*)(void*))k->fn)(k->a);
    }, &c);
}

bool CalcLongName(void* design) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::CShipDesign_CalcLongName, design, nullptr, nullptr, false };
    return Guarded([](void* x, void*) {
        auto* k = (Call*)x;
        ((void (*)(void*))k->fn)(k->a);
    }, &c);
}

bool CanBeBuiltBy(void* component, void* country) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::CComponentTemplate_CanBeBuiltBy, component, country, nullptr, false };
    if (!Guarded([](void* x, void*) {
            auto* k = (Call*)x;
            k->result = ((bool (*)(void*, void*, uint32_t))k->fn)(k->a, k->b, kOwnerCountry);
        }, &c)) {
        return false;
    }
    return c.result;
}

// CShipGrowthStage::IsValidToSaveForCountry: the designer's save check, with its reason
bool ValidToSave(void* stage, void* country, std::string* why) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::CShipGrowthStage_IsValidToSaveForCountry, stage, country, nullptr, false };
    std::string text;
    if (!CommandBuilder::Get().CallForText([](void* x, void* out) {
            auto* k = (Call*)x;
            k->result = ((bool (*)(void*, uint32_t, void*, void*))k->fn)(k->a, kOwnerCountry, k->b, out);
        }, &c, &text)) {
        if (why) *why = "the engine's design check failed";
        return false;
    }
    if (!c.result && why) *why = text.empty() ? "The game refused the design" : text;
    return c.result;
}

// CShipDesignerBase::ComponentIsAllowedOnSlot's first test: slot size and slot type match the
// component's (or the slot takes any)
bool SlotAccepts(void* slot, void* component) {
    uint8_t ss = Rd<uint8_t>((uintptr_t)slot + rt::CComponentSlot_size_kind, 0xFF);
    uint8_t st = Rd<uint8_t>((uintptr_t)slot + rt::CComponentSlot_type_kind, 0xFF);
    uint8_t cs = Rd<uint8_t>((uintptr_t)component + rt::CComponentTemplate_size_kind, 0xFE);
    uint8_t ct = Rd<uint8_t>((uintptr_t)component + rt::CComponentTemplate_type_kind, 0xFE);
    return (ss == cs || ss == (uint8_t)rt::kComponentSizeAny) && (st == ct || st == (uint8_t)rt::kComponentTypeAny);
}

// NShipDesignUtil::CanBuildComponent(component, owner, stage, slot or nullptr, reason): allowed on
// this hull, valid for the country, technology researched (missing techs in the reason)
bool CanBuildComponent(void* component, void* stage, void* slot, std::string* why) {
    Call c{ CommandBuilder::Get().Base() + sdk::fn::NShipDesignUtil_CanBuildComponent, component, stage, slot, false };
    std::string text;
    if (!CommandBuilder::Get().CallForText([](void* x, void* out) {
            auto* k = (Call*)x;
            k->result = ((bool (*)(void*, uint32_t, void*, void*, void*))k->fn)(k->a, kOwnerCountry, k->b, k->c, out);
        }, &c, &text)) {
        if (why) *why = "the engine's component check failed";
        return false;
    }
    if (!c.result && why) *why = text;
    return c.result;
}

// same hull and the same component in every slot and required slot
bool SameComponents(void* a, void* b) {
    void* sa = Stage0(a);
    void* sb = Stage0(b);
    if (!sa || !sb || SizeOf(sa) != SizeOf(sb)) return false;
    auto ca = Cores(sa), cb = Cores(sb);
    if (ca != cb) return false;
    auto xa = Sections(sa), xb = Sections(sb);
    if (xa.size() != xb.size()) return false;
    for (size_t i = 0; i < xa.size(); ++i) {
        void* ta = Rd<void*>((uintptr_t)xa[i] + rt::CShipDesignSection_template, nullptr);
        void* tb = Rd<void*>((uintptr_t)xb[i] + rt::CShipDesignSection_template, nullptr);
        if (ta != tb) return false;
        for (void* slot : SlotsOf(xa[i])) {
            if (Installed(xa[i], slot) != Installed(xb[i], slot)) return false;
        }
    }
    return true;
}
}  // namespace designs

// ---- shipyard ------------------------------------------------------------------------------
// A ship of a design built at a starbase's shipyard, as the starbase view's ship list builds it
// (CBuildableShipListItem::Build): CShipDesignImplementation(design, growth stage 0) ->
// NConstruction::CreateBuildable(orbitable = the starbase, implementation) -> CAddBuildableToQueueCommand
// on the starbase's shipyard queue. Military, civilian and colony ships alike.
namespace shipyard {
// CRefObjectOrbitableRef<CFleetOrbitableEnumType>: {id +0, type byte +4, has_extra +8, extra +0x10};
// the starbase view passes {starbase id, 2}
constexpr uint8_t kOrbitableStarbase = 2;

struct ImplCall {
    uintptr_t fn;
    void* impl;
    uint32_t design_id;
};
void CallImplCtor(void* p, void*) {
    auto* x = (ImplCall*)p;
    ((void* (*)(void*, uint32_t, int))x->fn)(x->impl, x->design_id, 0);
}
void CallImplDestroy(void* p, void*) {
    auto* x = (ImplCall*)p;
    void** vt = *(void***)x->impl;
    ((void* (*)(void*, unsigned))vt[sdk::vt::CShipDesignImplementation_Destroy])(x->impl, 0);  // no free
}
struct CreateCall {
    uintptr_t fn;
    const void* orbitable;
    const void* impl;
    const void* colonization;  // colony ships only
    void* out;
};
void CallCreate(void* p, void*) {
    auto* x = (CreateCall*)p;
    if (x->colonization) {
        ((void** (*)(void**, const void*, const void*, const void*))x->fn)(&x->out, x->orbitable, x->impl, x->colonization);
    } else {
        ((void** (*)(void**, const void*, const void*))x->fn)(&x->out, x->orbitable, x->impl);
    }
}
struct QueueCall {
    uintptr_t fn;
    const void* starbase;
    uint32_t queue;
};
void CallShipsQueue(void* p, void*) {
    auto* x = (QueueCall*)p;
    ((uint32_t* (*)(const void*, uint32_t*))x->fn)(x->starbase, &x->queue);
}
}  // namespace shipyard

// The engine-heap ship buildable for a design at a starbase, or null with the reason. Colony ships
// (the implementation's ship class) carry who settles: SColonizationData {designation: the default
// (TPdxNullObject<CColonyType>), species}, as the starbase view builds one after the species dialog.
void* ShipDesigner::ShipBuildable(uint32_t design_id, uint32_t starbase_id, uint32_t species_id, std::string* why,
                                  bool* is_colony) {
    auto& cb = CommandBuilder::Get();
    alignas(16) uint8_t impl[0x600];
    if ((size_t)sdk::rt::CShipDesignImplementation_size > sizeof(impl)) {
        *why = "CShipDesignImplementation grew past the bridge's buffer";
        return nullptr;
    }
    memset(impl, 0, sizeof(impl));
    shipyard::ImplCall ic{ cb.Base() + sdk::fn::CShipDesignImplementation_Ctor, impl, design_id };
    if (!cb.CallGuarded(&shipyard::CallImplCtor, &ic)) {
        *why = "CShipDesignImplementation could not be built for design " + std::to_string(design_id);
        return nullptr;
    }
    uint8_t orbitable[0x18]{};
    *(uint32_t*)orbitable = starbase_id;
    orbitable[4] = shipyard::kOrbitableStarbase;
    const bool colony = impl[sdk::rt::CShipDesignImplementation_ship_class] == (uint8_t)fleets::ShipClass::Colonizer;
    if (is_colony) *is_colony = colony;
    uint8_t colonization[0x18]{};
    if (colony) {
        if (species_id == 0xFFFFFFFF) {
            species_id = designs::Rd<uint32_t>((uintptr_t)GetPlayerCountry() + sdk::ent::CCountry::founder_species_ref, 0xFFFFFFFF);
        }
        *(void**)(colonization + sdk::ent::SColonizationData::designation) =
            designs::Rd<void*>(cb.Base() + sdk::glob::TPdxNullObject_CColonyType_pInstance, nullptr);
        *(uint32_t*)(colonization + sdk::ent::SColonizationData::species) = species_id;
        colonization[sdk::ent::SColonizationData::automation] = 0;
    }
    shipyard::CreateCall cc{ cb.Base() + (colony ? sdk::fn::NConstruction_CreateColonyShipBuildable
                                                 : sdk::fn::NConstruction_CreateShipBuildable),
                             orbitable, impl, colony ? colonization : nullptr, nullptr };
    bool made = cb.CallGuarded(&shipyard::CallCreate, &cc);
    cb.CallGuarded(&shipyard::CallImplDestroy, &ic);
    if (!made || !cc.out) {
        *why = "the game makes no buildable for this design (owner type)";
        return nullptr;
    }
    return cc.out;
}

// starbase_id, else the starbase in system_id; with its shipyard queue
bool ShipDesigner::ShipyardOf(uint32_t starbase_id, uint32_t system_id, uint32_t* out_starbase, uint32_t* out_queue,
                              std::string* why) {
    auto ref = [&](uintptr_t db, uint32_t id, std::ptrdiff_t id_off) -> void* {
        void* mgr = designs::Rd<void*>(base_address_ + db, nullptr);
        if (!mgr || id == 0xFFFFFFFF) return nullptr;
        void* slots = designs::Rd<void*>((uintptr_t)mgr + 0x18, nullptr);
        uint32_t cap = designs::Rd<uint32_t>((uintptr_t)mgr + 0x20, 0);
        if (!slots || (id & 0xFFFFFF) >= cap) return nullptr;
        void* obj = designs::Rd<void*>((uintptr_t)slots + (id & 0xFFFFFF) * 16 + 8, nullptr);
        return obj && designs::Rd<uint32_t>((uintptr_t)obj + id_off, 0xFFFFFFFF) == id ? obj : nullptr;
    };
    if (starbase_id == 0xFFFFFFFF) {
        void* sys = ref(sdk::db::CGalacticObject, system_id, sdk::rt::CGalacticObject_id);
        if (!sys) {
            *why = "Pass starbase_id, or system_id of a system with a starbase";
            return false;
        }
        uintptr_t arr = (uintptr_t)sys + sdk::ent::CGalacticObject::starbases;
        void* data = designs::Rd<void*>(arr + 0x8, nullptr);  // CPdxArray: data +8, size +0x14
        if (!data || designs::Rd<int32_t>(arr + 0x14, 0) <= 0) {
            *why = "System " + std::to_string(system_id) + " has no starbase";
            return false;
        }
        starbase_id = designs::Rd<uint32_t>((uintptr_t)data, 0xFFFFFFFF);
    }
    void* sb = ref(sdk::db::CStarbase, starbase_id, sdk::rt::CStarbase_id);
    if (!sb) {
        *why = "Starbase " + std::to_string(starbase_id) + " not found";
        return false;
    }
    *out_starbase = starbase_id;
    // CStarbase::GetShipsBuildQueueRef: the starbase's ship may carry the one queue it builds into
    shipyard::QueueCall qc{ CommandBuilder::Get().Base() + sdk::fn::CStarbase_GetShipsBuildQueueRef, sb, 0xFFFFFFFF };
    if (!CommandBuilder::Get().CallGuarded(&shipyard::CallShipsQueue, &qc) || qc.queue == 0xFFFFFFFF) {
        *why = "Starbase " + std::to_string(starbase_id) + " has no shipyard queue";
        return false;
    }
    *out_queue = qc.queue;
    return true;
}

nlohmann::json ShipDesigner::ShipDesignRow(void* design) {
    uint32_t did = designs::Rd<uint32_t>((uintptr_t)design + sdk::rt::CShipDesign_id, 0xFFFFFFFF);
    std::string size_key;
    if (void* size = designs::SizeOf(designs::Stage0(design))) {
        SafeReadPdxString((const void*)((uintptr_t)size + designs::kShipSizeKey), size_key);
    }
    return { {"design_id", did},
             {"name", PersistentNameText((const void*)((uintptr_t)design + sdk::ent::CShipDesign::name))},
             {"ship_size", size_key}, {"ship_size_name", LocalizeKey(size_key)} };
}

nlohmann::json ShipDesigner::GetBuildableShipsJson(const nlohmann::json& params) {
    if (!CommandBuilder::Get().SdkMatchesExe()) return { {"success", false}, {"error", "SDK does not match this stellaris.exe"} };
    uint32_t starbase_id = 0xFFFFFFFF, queue_id = 0xFFFFFFFF;
    std::string why;
    if (!ShipyardOf(params.value("starbase_id", 0xFFFFFFFFu), params.value("system_id", 0xFFFFFFFFu), &starbase_id,
                    &queue_id, &why)) {
        return { {"success", false}, {"error", why} };
    }
    const uint32_t only = params.value("design_id", 0xFFFFFFFFu);
    const uint32_t species_id = params.value("species_id", 0xFFFFFFFFu);  // colony ships (default: founder species)
    const uint32_t country_id = GameState::Get().GetPlayerCountryId();
    nlohmann::json buildable = nlohmann::json::array(), refused = nlohmann::json::array();
    for (void* design : PlayerDesigns()) {
        nlohmann::json row = ShipDesignRow(design);
        if (only != 0xFFFFFFFF && row["design_id"] != only) continue;
        std::string reason;
        bool colony = false;
        void* b = ShipBuildable(row["design_id"], starbase_id, species_id, &reason, &colony);
        if (colony) row["colony_ship"] = true;
        if (!b) {
            row["can_build"] = false;
            row["reason"] = reason;
            refused.push_back(row);
            continue;
        }
        nlohmann::json cost = OutlinerManager::Get().BuildableCostJsonPtr(b);
        bool ok = OutlinerManager::Get().QueueOwnedBuildable(b, country_id, queue_id, false, &reason);  // frees b
        row["can_build"] = ok;
        if (ok) {
            row.update(cost);
            buildable.push_back(row);
        } else {
            row["reason"] = reason.empty() ? "The game does not build it at this shipyard (no reason given)"
                                           : RenderPdxMarkup(reason);
            refused.push_back(row);
        }
    }
    nlohmann::json out = { {"success", true}, {"starbase_id", starbase_id}, {"queue_id", queue_id},
                           {"queue", OutlinerManager::Get().ExtractConstructionQueue(queue_id)}, {"buildable", buildable} };
    if (only != 0xFFFFFFFF) out["refused"] = refused;  // one design: say why not
    else out["refused_count"] = refused.size();
    return out;
}

nlohmann::json ShipDesigner::BuildShipJson(const nlohmann::json& params) {
    if (!CommandBuilder::Get().SdkMatchesExe()) return { {"success", false}, {"error", "SDK does not match this stellaris.exe"} };
    uint32_t design_id = params.value("design_id", 0xFFFFFFFFu);
    int count = std::clamp(params.value("count", 1), 1, 10);
    void* design = FindShipDesign(design_id);
    if (!design) return { {"success", false}, {"error", "Ship design " + std::to_string(design_id) + " not found (see stellaris_get_buildable_ships)"} };
    uint32_t starbase_id = 0xFFFFFFFF, queue_id = 0xFFFFFFFF;
    std::string why;
    if (!ShipyardOf(params.value("starbase_id", 0xFFFFFFFFu), params.value("system_id", 0xFFFFFFFFu), &starbase_id,
                    &queue_id, &why)) {
        return { {"success", false}, {"error", why} };
    }
    const uint32_t country_id = GameState::Get().GetPlayerCountryId();
    const uint32_t species_id = params.value("species_id", 0xFFFFFFFFu);
    nlohmann::json cost;
    int queued = 0;
    bool colony = false;
    for (int i = 0; i < count; ++i) {
        void* b = ShipBuildable(design_id, starbase_id, species_id, &why, &colony);
        if (!b) break;
        if (i == 0) cost = OutlinerManager::Get().BuildableCostJsonPtr(b);
        if (!OutlinerManager::Get().QueueOwnedBuildable(b, country_id, queue_id, true, &why)) break;
        ++queued;
    }
    nlohmann::json out = ShipDesignRow(design);
    out.update({ {"starbase_id", starbase_id}, {"queue_id", queue_id}, {"queued", queued}, {"success", queued > 0} });
    if (colony) {
        out["colony_ship"] = true;
        out["species_id"] = species_id != 0xFFFFFFFF ? species_id
            : designs::Rd<uint32_t>((uintptr_t)GetPlayerCountry() + sdk::ent::CCountry::founder_species_ref, 0xFFFFFFFF);
    }
    if (!cost.is_null()) out.update(cost);
    if (queued < count) out["error"] = why.empty() ? "The game refused the ship" : RenderPdxMarkup(why);
    return out;
}

ShipDesigner& ShipDesigner::Get() {
    static ShipDesigner instance;
    return instance;
}

bool ShipDesigner::Init(uintptr_t base_address) {
    base_address_ = base_address;
    if (!base_address_) return false;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + sdk::kRvaEngineAlloc);

    LOGF("[SHIP_DESIGNER] Initialized with Base=0x%llX", (unsigned long long)base_address_);
    return true;
}

std::string ShipDesigner::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

bool ShipDesigner::CanCountryUseComponent(void* p_tmpl, void* p_country) {
    return p_tmpl && p_country && designs::CanBeBuiltBy(p_tmpl, p_country);
}

void* ShipDesigner::GetPlayerCountry() {
    return GameState::Get().GetPlayerCountry();  // the local player's country (not country 0)
}

void* ShipDesigner::FindShipDesign(uint32_t design_id) {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CShipDesign), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &cap)) return nullptr;

    uint32_t idx = design_id & 0xFFFFFF;
    if (idx >= cap) return nullptr;

    void* candidate = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr + idx * 16 + 8), &candidate) || !candidate) return nullptr;

    uint32_t actual_id = 0;
    if (SafeReadU32((const void*)((uintptr_t)candidate + 0x10), &actual_id) && actual_id == design_id) {
        return candidate;
    }
    return nullptr;
}

void* ShipDesigner::FindFleet(uint32_t fleet_id) {
    return fleets::Find(base_address_, fleet_id);
}

void ShipDesigner::BuildComponentIndexIfNeeded() {
    if (component_cache_built_ && !component_cache_.empty()) return;
    if (!base_address_) return;

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) return;

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt) || cnt == 0) return;

    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) continue;

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string key;
            if (SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), key) && !key.empty()) {
                component_cache_[key] = p_tmpl;
            }
        }
    }

    component_cache_built_ = true;
    LOGF("[SHIP_DESIGNER] Indexed %zu component templates", component_cache_.size());
}

void* ShipDesigner::FindComponentTemplate(const std::string& component_key) {
    BuildComponentIndexIfNeeded();
    auto it = component_cache_.find(component_key);
    if (it != component_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<void*> ShipDesigner::PlayerDesigns() {
    std::vector<void*> out;
    void* country = GetPlayerCountry();
    if (!country) return out;
    uintptr_t arr = (uintptr_t)country + sdk::ent::CCountry::ship_design_collection + sdk::ent::CShipDesignCollection::ship_design;
    void* data = designs::Rd<void*>(arr + 0x8, nullptr);       // CPdxArray: data +8, size +0x14
    int32_t n = designs::Rd<int32_t>(arr + 0x14, 0);
    if (!data || n <= 0 || n > 4096) return out;
    for (int32_t i = 0; i < n; ++i) {
        if (void* d = FindShipDesign(designs::Rd<uint32_t>((uintptr_t)data + i * 4, 0xFFFFFFFF))) out.push_back(d);
    }
    return out;
}

std::vector<ShipDesignInfo> ShipDesigner::GetShipDesigns(uint32_t specific_design_id) {
    namespace rt = sdk::rt;
    std::vector<ShipDesignInfo> results;
    for (void* design : PlayerDesigns()) {
        uint32_t did = designs::Rd<uint32_t>((uintptr_t)design + rt::CShipDesign_id, 0xFFFFFFFF);
        if (specific_design_id != 0xFFFFFFFF && did != specific_design_id) continue;
        void* stage = designs::Stage0(design);
        void* size = designs::SizeOf(stage);
        if (!designs::Designable(size)) continue;  // the ship designer lists designable hulls only

        ShipDesignInfo d_info{};
        d_info.design_id = did;
        SafeReadPdxString((const void*)((uintptr_t)design + sdk::ent::CShipDesign::name + sdk::ent::CPersistentName::key), d_info.name);
        SafeReadPdxString((const void*)((uintptr_t)size + designs::kShipSizeKey), d_info.ship_size);
        for (void* sec : designs::Sections(stage)) {
            SectionInfo s_info{};
            SafeReadPdxString((const void*)((uintptr_t)sec + sdk::ent::CShipDesignSection::slot), s_info.name);
            uint32_t k = 0;
            for (void* slot : designs::SlotsOf(sec)) {
                SlotInfo sl{};
                sl.slot_index = k++;
                SafeReadPdxString((const void*)((uintptr_t)slot + rt::CComponentSlot_name), sl.slot_name);
                if (void* comp = designs::Installed(sec, slot)) {
                    SafeReadPdxString((const void*)((uintptr_t)comp + designs::kComponentKey), sl.component_key);
                    sl.component_name = LocalizeKey(sl.component_key);
                }
                s_info.slots.push_back(sl);
            }
            d_info.sections.push_back(s_info);
        }
        uint32_t i = 0;
        for (void* core : designs::Cores(stage)) {
            CoreComponentInfo c{};
            c.index = i++;
            if (core) {
                SafeReadPdxString((const void*)((uintptr_t)core + designs::kComponentKey), c.component_key);
                c.component_name = LocalizeKey(c.component_key);
            }
            d_info.core_components.push_back(c);
        }
        results.push_back(d_info);
    }
    return results;
}

nlohmann::json ShipDesigner::GetShipDesignsJson(const nlohmann::json& params) {
    uint32_t did = 0xFFFFFFFF;
    if (params.contains("design_id") && params["design_id"].is_number()) {
        did = params["design_id"].get<uint32_t>();
    }

    auto list = GetShipDesigns(did);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : list) {
        nlohmann::json dj;
        dj["design_id"] = d.design_id;
        dj["name"] = d.name;
        dj["ship_size"] = d.ship_size;

        nlohmann::json sec_arr = nlohmann::json::array();
        for (const auto& s : d.sections) {
            nlohmann::json sj;
            sj["name"] = s.name;
            nlohmann::json sl_arr = nlohmann::json::array();
            for (const auto& sl : s.slots) {
                sl_arr.push_back({
                    {"slot_index", sl.slot_index},
                    {"slot_name", sl.slot_name},
                    {"component_key", sl.component_key},
                    {"component_name", sl.component_name}
                });
            }
            sj["slots"] = sl_arr;
            sec_arr.push_back(sj);
        }
        dj["sections"] = sec_arr;

        nlohmann::json cores = nlohmann::json::array();
        for (const auto& c : d.core_components) {
            cores.push_back({ {"index", c.index}, {"component_key", c.component_key}, {"component_name", c.component_name} });
        }
        dj["core_components"] = cores;

        arr.push_back(dj);
    }

    return {
        {"count", arr.size()},
        {"designs", arr}
    };
}

nlohmann::json ShipDesigner::GetShipDesignCatalogJson(const nlohmann::json& params) {
    BuildComponentIndexIfNeeded();

    void* country = GetPlayerCountry();
    bool unlocked_only = params.value("unlocked_only", true);

    std::string cat_filter;
    if (params.contains("category") && params["category"].is_string()) {
        cat_filter = params["category"].get<std::string>();
        std::transform(cat_filter.begin(), cat_filter.end(), cat_filter.begin(), ::tolower);
    }

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) {
        return { {"error", "Component database not found"} };
    }

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt)) {
        return { {"error", "Failed to read component sets"} };
    }

    nlohmann::json components_json = nlohmann::json::array();

    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key, loc_name, icon_gfx;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x50), loc_name);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x80), icon_gfx);

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) continue;

        nlohmann::json variants = nlohmann::json::array();
        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);

            bool is_unlocked = CanCountryUseComponent(p_tmpl, country);
            if (unlocked_only && !is_unlocked) {
                continue;
            }

            std::string size_str = "standard";
            if (t_key.rfind("SMALL_", 0) == 0) size_str = "small";
            else if (t_key.rfind("MEDIUM_", 0) == 0) size_str = "medium";
            else if (t_key.rfind("LARGE_", 0) == 0) size_str = "large";
            else if (t_key.rfind("AUX_", 0) == 0) size_str = "aux";

            std::string var_name = LocalizeKey(t_key);

            variants.push_back({
                {"component_key", t_key},
                {"name", var_name},
                {"size", size_str},
                {"is_unlocked", is_unlocked}
            });
        }

        if (!variants.empty()) {
            std::string set_loc = LocalizeKey(set_key);
            if (set_loc.empty() || set_loc == set_key) set_loc = loc_name;
            components_json.push_back({
                {"set_key", set_key},
                {"localized_name", set_loc},
                {"icon", icon_gfx},
                {"variants", variants}
            });
        }
    }

    std::vector<std::string> standard_sizes = {
        "corvette", "frigate", "destroyer", "cruiser", "battleship", "titan",
        "colossus", "juggernaut", "military_station_small", "ion_cannon"
    };

    return {
        {"total_component_sets", components_json.size()},
        {"components", components_json},
        {"ship_sizes", standard_sizes}
    };
}

nlohmann::json ShipDesigner::GetComponentDetailsJson(const nlohmann::json& params) {
    BuildComponentIndexIfNeeded();

    std::string query;
    if (params.contains("key") && params["key"].is_string()) {
        query = params["key"].get<std::string>();
    } else if (params.contains("component_key") && params["component_key"].is_string()) {
        query = params["component_key"].get<std::string>();
    } else if (params.contains("set_key") && params["set_key"].is_string()) {
        query = params["set_key"].get<std::string>();
    }

    if (query.empty()) {
        return { {"error", "Missing required parameter 'key' (set_key or component_key)"} };
    }

    void* country = GetPlayerCountry();

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) {
        return { {"error", "Component database not found"} };
    }

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt)) {
        return { {"error", "Failed to read component sets"} };
    }

    std::string query_upper = query;
    std::transform(query_upper.begin(), query_upper.end(), query_upper.begin(), ::toupper);

    auto format_set_json = [&](void* set_ptr) -> nlohmann::json {
        if (!set_ptr) return nullptr;

        std::string set_key, loc_name, icon_gfx;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x50), loc_name);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x80), icon_gfx);

        std::string set_localized = LocalizeKey(set_key);
        if (set_localized.empty() || set_localized == set_key) {
            set_localized = loc_name;
        }

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) {
            return {
                {"set_key", set_key},
                {"localized_name", set_localized},
                {"icon", icon_gfx},
                {"total_variants", 0},
                {"variants", nlohmann::json::array()}
            };
        }

        nlohmann::json variants = nlohmann::json::array();

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);
            if (t_key.empty()) continue;

            std::string var_name = LocalizeKey(t_key);
            bool is_unlocked = CanCountryUseComponent(p_tmpl, country);

            int64_t power_raw = 0;
            SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x2A8), &power_raw);
            double power = power_raw / 100000.0;

            uint8_t s_enum = 0;
            SafeCopyChars((char*)&s_enum, (const char*)((uintptr_t)p_tmpl + 0x1E0), 1);
            std::string size_str = "standard";
            switch (s_enum) {
                case 1: size_str = "small"; break;
                case 2: size_str = "medium"; break;
                case 3: size_str = "large"; break;
                case 4: size_str = "torpedo"; break;
                case 5: size_str = "extra_large"; break;
                case 6: size_str = "titanic"; break;
                case 7: size_str = "planet_killer"; break;
                case 8: size_str = "hangar"; break;
                case 9: size_str = "aux"; break;
                default:
                    if (t_key.rfind("SMALL_", 0) == 0) size_str = "small";
                    else if (t_key.rfind("MEDIUM_", 0) == 0) size_str = "medium";
                    else if (t_key.rfind("LARGE_", 0) == 0) size_str = "large";
                    else if (t_key.rfind("AUX_", 0) == 0) size_str = "aux";
                    else if (t_key.find("TITAN") != std::string::npos || t_key.find("ION") != std::string::npos) size_str = "titanic";
                    break;
            }

            uintptr_t vt = 0;
            SafeReadPtr((const void*)p_tmpl, (void**)&vt);
            uintptr_t vt_rva = (vt > base_address_) ? (vt - base_address_) : 0;

            nlohmann::json vj = {
                {"component_key", t_key},
                {"name", var_name},
                {"size", size_str},
                {"power", power},
                {"is_unlocked", is_unlocked}
            };

            if (vt_rva == 0x23714F0) {
                // CWeaponComponentTemplate
                vj["type"] = "weapon";

                int64_t min_d_raw = 0, delta_d_raw = 0, rng_raw = 0, min_rng_raw = 0, cd_raw = 0;
                int64_t acc_raw = 0, trk_raw = 0, hull_m_raw = 0, armor_m_raw = 0, shield_m_raw = 0;
                int64_t sh_pen_raw = 0, ar_pen_raw = 0, min_w_raw = 0, delta_w_raw = 0;
                int64_t col_dmg_raw = 0, col_delta_raw = 0, col_rng_raw = 0;

                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1218), &min_d_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1220), &delta_d_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E8), &rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F0), &min_rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E0), &cd_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F8), &acc_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1200), &trk_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1228), &hull_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1230), &armor_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1238), &shield_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1240), &sh_pen_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1248), &ar_pen_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D0), &min_w_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D8), &delta_w_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1268), &col_dmg_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1270), &col_delta_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1278), &col_rng_raw);

                vj["weapon_stats"] = {
                    {"min_damage", min_d_raw / 100000.0},
                    {"max_damage", (min_d_raw + delta_d_raw) / 100000.0},
                    {"range", rng_raw / 100000.0},
                    {"max_range", rng_raw / 100000.0},
                    {"min_range", min_rng_raw / 100000.0},
                    {"cooldown", cd_raw / 100000.0},
                    {"accuracy", acc_raw / 100000.0},
                    {"tracking", trk_raw / 100000.0},
                    {"shield_mult", shield_m_raw / 100000.0},
                    {"armor_mult", armor_m_raw / 100000.0},
                    {"hull_mult", hull_m_raw / 100000.0},
                    {"shield_penetration", sh_pen_raw / 100000.0},
                    {"armor_penetration", ar_pen_raw / 100000.0},
                    {"min_windup", min_w_raw / 100000.0},
                    {"max_windup", (min_w_raw + delta_w_raw) / 100000.0},
                    {"aoe_damage", col_dmg_raw / 100000.0},
                    {"aoe_range", col_rng_raw / 100000.0}
                };
            } else if (vt_rva == 0x2371380) {
                // CStrikeCraftComponentTemplate
                vj["type"] = "strike_craft";

                int32_t c_cnt_val = 0;
                int64_t rng_raw = 0, cd_raw = 0, d_min_raw = 0, d_max_raw = 0, acc_raw = 0, trk_raw = 0;

                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11C8), &c_cnt_val);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E8), &rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D8), &cd_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1250), &d_min_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1258), &d_max_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F8), &acc_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1200), &trk_raw);

                vj["strike_craft_stats"] = {
                    {"craft_count", c_cnt_val},
                    {"engagement_range", rng_raw / 100000.0},
                    {"min_damage", d_min_raw / 100000.0},
                    {"max_damage", d_max_raw / 100000.0},
                    {"cooldown", cd_raw / 100000.0},
                    {"accuracy", acc_raw / 100000.0},
                    {"tracking", trk_raw / 100000.0}
                };
            } else if (vt_rva == 0x2371468) {
                // CUtilityComponentTemplate
                vj["type"] = "utility";
                nlohmann::json uj = nlohmann::json::object();

                void* p_entries = nullptr;
                uint32_t m_cnt_val = 0;
                uintptr_t mod_addr = (uintptr_t)p_tmpl + 0x2B0;
                if (SafeReadPtr((const void*)(mod_addr + 0x38), &p_entries) && p_entries &&
                    SafeReadU32((const void*)(mod_addr + 0x44), &m_cnt_val) && m_cnt_val > 0 && m_cnt_val < 32) {
                    for (uint32_t idx = 0; idx < m_cnt_val; ++idx) {
                        int64_t val_raw = 0, type_raw = 0;
                        SafeReadI64((const void*)((uintptr_t)p_entries + idx * 16), &val_raw);
                        SafeReadI64((const void*)((uintptr_t)p_entries + idx * 16 + 8), &type_raw);
                        double val = val_raw / 100000.0;

                        switch (type_raw) {
                            case 30: uj["hull_add"] = val; break;
                            case 34: uj["hull_regen"] = val; break;
                            case 36: uj["armor_add"] = val; break;
                            case 41: uj["armor_regen"] = val; break;
                            case 46: uj["shield_add"] = val; break;
                            case 51: uj["shield_regen"] = val; break;
                            case 59: uj["weapon_range_mult"] = val; break;
                            case 64: uj["fire_rate_mult"] = val; break;
                            case 73: uj["evasion_add"] = val; break;
                            case 74: uj["evasion_mult"] = val; break;
                            case 75: uj["accuracy_add"] = val; break;
                            case 79: uj["base_speed_mult"] = val; break;
                            case 80: uj["speed_mult"] = val; break;
                            default: {
                                std::string custom_mod = "mod_" + std::to_string(type_raw);
                                uj[custom_mod] = val;
                                break;
                            }
                        }
                    }
                }

                int32_t sr = 0, hl = 0;
                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11C8), &sr);
                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11CC), &hl);
                if (sr > 0) uj["sensor_range"] = sr;
                if (hl > 0) uj["hyperlane_range"] = hl;

                std::string beh;
                SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x11E8), beh);
                if (!beh.empty()) uj["ship_behavior"] = beh;

                vj["utility_stats"] = uj;
            } else {
                vj["type"] = "other";
            }

            variants.push_back(vj);
        }

        return {
            {"set_key", set_key},
            {"localized_name", set_localized},
            {"icon", icon_gfx},
            {"total_variants", variants.size()},
            {"variants", variants}
        };
    };

    // Priority 1: Exact match on set_key
    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        std::string set_key_upper = set_key;
        std::transform(set_key_upper.begin(), set_key_upper.end(), set_key_upper.begin(), ::toupper);

        if (set_key_upper == query_upper) {
            return format_set_json(set_ptr);
        }
    }

    // Priority 2: Exact match on component_key
    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) continue;

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);
            std::string t_key_upper = t_key;
            std::transform(t_key_upper.begin(), t_key_upper.end(), t_key_upper.begin(), ::toupper);

            if (t_key_upper == query_upper) {
                return format_set_json(set_ptr);
            }
        }
    }

    // Priority 3: Prefix/fuzzy match on set_key (e.g. "PLASMA" matches "PLASMA_1", "PLASMA_2", "PLASMA_3")
    std::vector<void*> matched_sets;
    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        std::string set_key_upper = set_key;
        std::transform(set_key_upper.begin(), set_key_upper.end(), set_key_upper.begin(), ::toupper);

        if (set_key_upper.find(query_upper) != std::string::npos) {
            matched_sets.push_back(set_ptr);
        }
    }

    if (matched_sets.size() == 1) {
        return format_set_json(matched_sets[0]);
    } else if (matched_sets.size() > 1) {
        nlohmann::json sets_arr = nlohmann::json::array();
        for (void* s : matched_sets) {
            sets_arr.push_back(format_set_json(s));
        }
        return {
            {"query", query},
            {"matched_count", sets_arr.size()},
            {"matching_sets", sets_arr}
        };
    }

    return { {"error", "Component or component set not found: " + query} };
}

bool ShipDesigner::SetShipDesignName(void* design, const std::string& name) {
    if (!design || name.empty()) return false;
    uintptr_t pname = (uintptr_t)design + sdk::ent::CShipDesign::name;
    void* key = (void*)(pname + sdk::ent::CPersistentName::key);
    if (!SafeWritePdxString(key, name)) {
        RawPdxString raw{};
        if (!SafeCopyChars((char*)&raw, (const char*)key, sizeof(RawPdxString))) return false;
        void* buf = fn_engine_alloc_ ? fn_engine_alloc_(name.size() + 16) : nullptr;
        if (!buf) return false;
        memcpy(buf, name.data(), name.size());
        ((char*)buf)[name.size()] = '\0';
        raw.heap_ptr = (char*)buf;
        raw.size = name.size();
        raw.capacity = name.size() + 15;
        SafeCopyChars((char*)key, (const char*)&raw, sizeof(RawPdxString));
    }
    // a name list key (e.g. HUMAN1_SHIP_Cobra) is localized, anything else is shown as typed
    *(uint8_t*)(pname + sdk::ent::CPersistentName::literal) = name.find("_SHIP_") == std::string::npos ? 1 : 0;
    designs::CalcLongName(design);
    return true;
}

bool ShipDesigner::ApplyDesignEdits(void* design, void* country, const nlohmann::json& slots_json,
                                    const nlohmann::json& cores_json, int* changes, std::string* why) {
    namespace rt = sdk::rt;
    void* stage = designs::Stage0(design);
    if (!stage) {
        *why = "The design has no growth stage";
        return false;
    }
    auto sections = designs::Sections(stage);
    auto component_of = [&](const std::string& key, void** out) -> bool {
        *out = FindComponentTemplate(key);
        if (!*out) {
            *why = "Component key '" + key + "' not found (see stellaris_get_ship_design_catalog)";
            return false;
        }
        return true;
    };
    // the designer's own check for a component in a slot (nullptr: a required component)
    auto allowed = [&](void* comp, void* slot, const std::string& key) -> bool {
        std::string reason;
        if (slot && !designs::SlotAccepts(slot, comp)) {
            std::string sl;
            SafeReadPdxString((const void*)((uintptr_t)slot + rt::CComponentSlot_name), sl);
            *why = "'" + LocalizeKey(key) + "' (" + key + ") does not fit slot " + sl + " (wrong size or type)";
            return false;
        }
        if (!designs::CanBuildComponent(comp, stage, slot, &reason)) {
            *why = "'" + LocalizeKey(key) + "' (" + key + ") cannot be used here" + (reason.empty() ? "" : ": " + reason);
            return false;
        }
        return true;
    };

    if (slots_json.is_array()) {
        for (const auto& item : slots_json) {
            std::string key = item.value("component_key", "");
            void* comp = nullptr;
            if (!component_of(key, &comp)) return false;
            int want_sec = item.value("section_index", -1);
            std::string sec_name = item.value("section_name", "");
            int want_slot = item.value("slot_index", -1);
            std::string slot_name = item.value("slot_name", "");
            void* sec_hit = nullptr;
            void* slot_hit = nullptr;
            for (size_t si = 0; si < sections.size() && !slot_hit; ++si) {
                if (want_sec >= 0 && (int)si != want_sec) continue;
                std::string name;
                SafeReadPdxString((const void*)((uintptr_t)sections[si] + sdk::ent::CShipDesignSection::slot), name);
                if (!sec_name.empty() && name != sec_name) continue;
                auto slots = designs::SlotsOf(sections[si]);
                for (size_t k = 0; k < slots.size(); ++k) {
                    std::string sl;
                    SafeReadPdxString((const void*)((uintptr_t)slots[k] + rt::CComponentSlot_name), sl);
                    if ((want_slot >= 0 && (int)k == want_slot && slot_name.empty()) || (!slot_name.empty() && sl == slot_name)) {
                        sec_hit = sections[si];
                        slot_hit = slots[k];
                        break;
                    }
                }
            }
            if (!slot_hit) {
                *why = "No such slot (give section_index or section_name, and slot_index or slot_name from stellaris_get_ship_designs)";
                return false;
            }
            if (!allowed(comp, slot_hit, key)) return false;
            void* before = designs::Installed(sec_hit, slot_hit);
            if (!designs::SetComponent(sec_hit, comp, slot_hit) || designs::Installed(sec_hit, slot_hit) != comp) {
                std::string sl;
                SafeReadPdxString((const void*)((uintptr_t)slot_hit + rt::CComponentSlot_name), sl);
                *why = "'" + LocalizeKey(key) + "' (" + key + ") does not fit slot " + sl;
                return false;
            }
            if (before != comp) ++*changes;
        }
    }

    // required components: the engine keeps one per component set (reactor, FTL, thrusters ...)
    std::vector<std::string> core_keys;
    if (cores_json.is_array()) {
        for (const auto& k : cores_json) if (k.is_string()) core_keys.push_back(k.get<std::string>());
    } else if (cores_json.is_object()) {
        for (const auto& [field, k] : cores_json.items()) if (k.is_string() && !k.get<std::string>().empty()) core_keys.push_back(k.get<std::string>());
    }
    for (const auto& key : core_keys) {
        void* comp = nullptr;
        if (!component_of(key, &comp)) return false;
        if (!allowed(comp, nullptr, key)) return false;
        void* set = designs::Rd<void*>((uintptr_t)comp + rt::CComponentTemplate_component_set, nullptr);
        void* data = designs::Rd<void*>((uintptr_t)stage + rt::CShipGrowthStage_components, nullptr);
        auto cores = designs::Cores(stage);
        int hit = -1;
        for (size_t i = 0; i < cores.size(); ++i) {
            if (cores[i] && designs::Rd<void*>((uintptr_t)cores[i] + rt::CComponentTemplate_component_set, nullptr) == set) {
                hit = (int)i;
                break;
            }
        }
        if (hit < 0 || !data) {
            *why = "This hull has no required slot for '" + LocalizeKey(key) + "' (" + key + ")";
            return false;
        }
        if (cores[hit] != comp) {
            // what CShipDesignerBase::SetRequiredComponent does: overwrite the entry
            *(void**)((uintptr_t)data + hit * sizeof(void*)) = comp;
            ++*changes;
        }
    }
    designs::UpdateResources(stage);
    return true;
}

bool ShipDesigner::PostDesign(void* source, const std::string& name, const nlohmann::json& slots_json,
                              const nlohmann::json& cores_json, bool is_new, std::string* message) {
    void* country = GetPlayerCountry();
    if (!country) {
        *message = "Player country not available";
        return false;
    }
    // CCreateOrUpdateShipDesignCommand(design, country): the engine copy-constructs `source`
    // into the command; with id -1, Execute's CShipDesignCollection::AddShipDesign creates a new
    // design. RemoveShipDesign first drops the player's design with the same name (or identical
    // components in the same design slot): that is how the designer's save replaces a design.
    namespace cu = sdk::cmd::create_or_update_ship_design;
    void* obj = CommandBuilder::Get().EngineAlloc(cu::kSize);
    if (!obj) {
        *message = "Engine allocation for the design command failed";
        return false;
    }
    struct CtorCtx {
        uintptr_t fn;
        void* obj;
        const void* src;
        uint32_t country;
    } ctx{ base_address_ + sdk::fn::CCreateOrUpdateShipDesignCommand_CtorCountry, obj, source,
           GameState::Get().GetPlayerCountryId() };
    if (!CommandBuilder::Get().CallGuarded([](void* c, void*) {
            auto* x = (CtorCtx*)c;
            ((void* (*)(void*, const void*, uint32_t))x->fn)(x->obj, x->src, x->country);
        }, &ctx)) {
        *message = "The engine failed to copy the design";
        return false;
    }
    auto cmd = CommandBuilder::Get().Adopt(cu::kSpec, obj);
    if (!cmd) {
        *message = cmd.error();
        return false;
    }
    void* copy = (void*)((uintptr_t)obj + cu::design);
    *(uint32_t*)((uintptr_t)copy + sdk::rt::CShipDesign_id) = 0xFFFFFFFF;
    if (!name.empty()) SetShipDesignName(copy, name);

    int changes = 0;
    std::string why;
    if (!ApplyDesignEdits(copy, country, slots_json, cores_json, &changes, &why)) {
        *message = why;
        return false;  // `cmd` destroys the unposted command and its design copy
    }
    designs::CalcLongName(copy);
    if (!designs::ValidToSave(designs::Stage0(copy), country, &why)) {
        *message = why;
        return false;
    }
    if (is_new) {
        // a new design identical to one the player has would replace it instead
        for (void* d : PlayerDesigns()) {
            if (designs::SameComponents(copy, d)) {
                std::string other;
                SafeReadPdxString((const void*)((uintptr_t)d + sdk::ent::CShipDesign::name + sdk::ent::CPersistentName::key), other);
                *message = "Identical to your design '" + other + "' (" +
                           std::to_string(designs::Rd<uint32_t>((uintptr_t)d + sdk::rt::CShipDesign_id, 0)) +
                           "); the game would replace it. Change at least one component.";
                return false;
            }
        }
    }
    if (!cmd.Post(NativeCommand::Check::IsValid)) {
        *message = cmd.error();
        return false;
    }
    *message = std::to_string(changes) + " component change(s)";
    return true;
}

nlohmann::json ShipDesigner::CreateShipDesignJson(const nlohmann::json& params) {
    std::string size = params.value("ship_size", "");
    std::string name = params.value("name", "");
    if (size.empty()) return { {"success", false}, {"error", "ship_size is required (a designable hull, e.g. corvette)"} };
    // start from the player's own design of that hull (its sections and required components)
    void* proto = nullptr;
    auto own = PlayerDesigns();
    for (void* d : own) {
        void* sz = designs::SizeOf(designs::Stage0(d));
        std::string key;
        SafeReadPdxString((const void*)((uintptr_t)sz + designs::kShipSizeKey), key);
        if (key == size && designs::Designable(sz)) {
            proto = d;
            break;
        }
    }
    if (!proto) {
        return { {"success", false}, {"error", "You have no design of hull '" + size + "' to start from (the empire cannot build it yet, "
                                               "or it is not designable)"} };
    }
    if (name.empty()) return { {"success", false}, {"error", "name is required (a new, unique design name)"} };
    for (void* d : own) {
        std::string other;
        SafeReadPdxString((const void*)((uintptr_t)d + sdk::ent::CShipDesign::name + sdk::ent::CPersistentName::key), other);
        if (other == name) {
            return { {"success", false}, {"error", "You already have a design named '" + name +
                                                   "' (saving would replace it; use stellaris_update_ship_design)"} };
        }
    }
    nlohmann::json slots = params.value("slots", nlohmann::json::array());
    nlohmann::json cores = params.value("core_components", nlohmann::json::array());
    std::string msg;
    if (!PostDesign(proto, name, slots, cores, true, &msg)) return { {"success", false}, {"error", msg} };
    return { {"success", true}, {"ship_size", size}, {"name", name},
             {"message", "Design posted (" + msg + "); it appears in stellaris_get_ship_designs with its id on the next call"} };
}

nlohmann::json ShipDesigner::UpdateShipDesignJson(const nlohmann::json& params) {
    if (!params.contains("design_id") || !params["design_id"].is_number()) {
        return { {"success", false}, {"error", "design_id is required"} };
    }
    uint32_t did = params["design_id"].get<uint32_t>();
    void* design = nullptr;
    for (void* d : PlayerDesigns()) {
        if (designs::Rd<uint32_t>((uintptr_t)d + sdk::rt::CShipDesign_id, 0xFFFFFFFF) == did) design = d;
    }
    if (!design) return { {"success", false}, {"error", "Design " + std::to_string(did) + " is not one of your designs"} };
    if (!designs::Designable(designs::SizeOf(designs::Stage0(design)))) {
        return { {"success", false}, {"error", "Design " + std::to_string(did) + " is not a customizable ship design"} };
    }
    std::string current;
    SafeReadPdxString((const void*)((uintptr_t)design + sdk::ent::CShipDesign::name + sdk::ent::CPersistentName::key), current);
    std::string name = params.value("name", "");
    if (!name.empty() && name != current) {
        return { {"success", false}, {"error", "Renaming is not supported: the game matches the design it replaces by name. "
                                               "Create a new design with stellaris_create_ship_design instead."} };
    }
    nlohmann::json slots = params.value("slots", nlohmann::json::array());
    nlohmann::json cores = params.value("core_components", nlohmann::json::array());
    std::string msg;
    if (!PostDesign(design, "", slots, cores, false, &msg)) return { {"success", false}, {"error", msg} };
    return { {"success", true}, {"replaced_design_id", did}, {"name", current},
             {"message", "Design update posted (" + msg + "); the game saves it as a new design with a new id that replaces "
                         "this one and refits fleet templates using it"} };
}

bool ShipDesigner::UpgradeFleet(uint32_t fleet_id, uint32_t starbase_id, uint32_t target_design_id,
                               std::string& out_message) {
    void* fleet = FindFleet(fleet_id);
    if (!fleet) {
        out_message = "Fleet ID " + std::to_string(fleet_id) + " not found";
        return false;
    }

    // `starbase_id` is really the construction queue that builds the upgrade; the factory
    // default 0xFFFFFFFF lets the engine pick the nearest shipyard. Queue flags keep defaults.
    namespace upgrade = sdk::cmd::fleet_upgrade_design_command;
    auto cmd = CommandBuilder::Get().Create(upgrade::kSpec);
    cmd.Set<uint32_t>(upgrade::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(upgrade::fleet, fleet_id)
       .Set<uint32_t>(upgrade::construction_queue, starbase_id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    out_message = "CFleetUpgradeDesignCommand posted successfully for fleet " + std::to_string(fleet_id);
    LOGF("[SHIP_DESIGNER] Fleet upgrade command dispatched for fleet %u", fleet_id);
    return true;
}

nlohmann::json ShipDesigner::UpgradeFleetJson(const nlohmann::json& params) {
    if (!params.contains("fleet_id") || !params["fleet_id"].is_number()) {
        return {
            {"error", {
                {"code", -32602},
                {"message", "Missing required parameter: fleet_id"}
            }}
        };
    }

    uint32_t fid = params["fleet_id"].get<uint32_t>();
    uint32_t sbid = params.value("starbase_id", 0xFFFFFFFF);
    uint32_t tdid = params.value("target_design_id", 0xFFFFFFFF);

    std::string msg;
    bool ok = UpgradeFleet(fid, sbid, tdid, msg);
    if (!ok) {
        return {
            {"error", {
                {"code", -32002},
                {"message", msg}
            }}
        };
    }

    return {
        {"success", true},
        {"fleet_id", fid},
        {"message", msg}
    };
}

bool ShipDesigner::DeleteShipDesign(uint32_t design_id, std::string& out_message) {
    void* design = FindShipDesign(design_id);
    if (!design) {
        out_message = "Ship design ID " + std::to_string(design_id) + " not found";
        return false;
    }

    void* p_sub = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) || !p_sub) {
        out_message = "Failed to access ship design sub-structure";
        return false;
    }

    void* p_size = nullptr;
    uint32_t sec_templates_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) || !p_size ||
        !SafeReadU32((const void*)((uintptr_t)p_size + 0x848), &sec_templates_cnt) || sec_templates_cnt == 0) {
        out_message = "Ship design ID " + std::to_string(design_id) + " is a fixed core design and cannot be deleted";
        return false;
    }

    namespace remove = sdk::cmd::remove_ship_design;
    auto cmd = CommandBuilder::Get().Create(remove::kSpec);
    cmd.Set<uint32_t>(remove::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(remove::design, design_id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    out_message = "CRemoveShipDesignCommand posted successfully for design " + std::to_string(design_id);
    LOGF("[SHIP_DESIGNER] Ship design deletion command dispatched for design %u", design_id);
    return true;
}

nlohmann::json ShipDesigner::DeleteShipDesignJson(const nlohmann::json& params) {
    if (!params.contains("design_id") || !params["design_id"].is_number()) {
        return {
            {"error", {
                {"code", -32602},
                {"message", "Missing required parameter: design_id"}
            }}
        };
    }

    uint32_t did = params["design_id"].get<uint32_t>();
    std::string msg;
    bool ok = DeleteShipDesign(did, msg);
    if (!ok) {
        return {
            {"error", {
                {"code", -32003},
                {"message", msg}
            }}
        };
    }

    return {
        {"success", true},
        {"design_id", did},
        {"message", msg}
    };
}

} // namespace bridge
