#include "galaxy_setup_manager.hpp"
#include "command_builder.hpp"
#include "common.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <cctype>
#include <cmath>
#include <vector>

namespace bridge {
namespace {

namespace gs = sdk::ent::SGalaxyConfigSettings;

// a CString member: the SDK names its text part, the engine's assign takes the whole CString
constexpr std::ptrdiff_t kCStringHeader = 0x10;
// CFixedPoint: value * 100000; the settings' "unset"
constexpr int64_t kFixedScale = 100000, kFixedUnset = -100000;

template <typename T>
bool ReadVal(uintptr_t addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool WriteVal(uintptr_t addr, T value) {
    __try {
        *(volatile T*)addr = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyChars(char* dst, const char* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Text(uintptr_t addr) {
    struct Raw {
        union {
            char buf[16];
            const char* ptr;
        };
        uint64_t size;
        uint64_t cap;
    } raw{};
    if (!ReadVal(addr, &raw) || raw.size == 0 || raw.size > 512) return "";
    std::string out(raw.size, '\0');
    return CopyChars(&out[0], raw.cap >= 16 ? raw.ptr : (const char*)addr, raw.size) ? out : "";
}

struct AssignCtx {
    uintptr_t fn;
    void* cstring;
    const char* chars;
    size_t length;
};

void CallAssign(void* p, void*) {
    auto* x = (AssignCtx*)p;
    ((void* (*)(void*, const char*, size_t))x->fn)(x->cstring, x->chars, x->length);
}

enum class Kind { Str, Int, Fixed, Enum, Bool };

struct EnumNames {
    const char* const* keys;  // value -> the engine's name (a loc key, or the script token)
    int count;
    int unset;  // the value the settings file leaves out
};

template <size_t N>
constexpr EnumNames Names(const char* const (&keys)[N], int unset) {
    return { keys, (int)N, unset };
}

struct Field {
    const char* name;  // the settings file's token
    Kind kind;
    std::ptrdiff_t off;  // in SGalaxyConfigSettingsSP
    EnumNames names;
};

// clustered / ironman are members of the SP struct; the SDK places them in CSettings
constexpr std::ptrdiff_t kSpOff = sdk::ent::CSettings::galaxy_config_sp;
static_assert(sdk::ent::SGalaxyConfigSettingsSP::ironman > kSpOff, "SGalaxyConfigSettingsSP offsets are CSettings-relative");

const std::vector<Field>& Fields() {
    static const std::vector<Field> fields = {
        { "template", Kind::Str, gs::template_ },
        { "shape", Kind::Str, gs::shape },
        { "num_empires", Kind::Int, gs::num_empires },
        { "num_advanced_empires", Kind::Int, gs::num_advanced_empires },
        { "num_fallen_empires", Kind::Int, gs::num_fallen_empires },
        { "num_marauder_empires", Kind::Int, gs::num_marauder_empires },
        { "num_nomad_empires", Kind::Int, gs::num_nomad_empires },
        { "num_guaranteed_colonies", Kind::Int, gs::num_guaranteed_colonies },
        { "mid_game_start", Kind::Int, gs::mid_game_start },
        { "end_game_start", Kind::Int, gs::end_game_start },
        { "victory_year", Kind::Int, gs::victory_year },
        { "colonizable_planet_odds", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_colonizable_planet_odds },
        { "resource_abundance", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_resource_abundance },
        { "primitive_odds", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_primitive_odds },
        { "crises", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_crises },
        { "technology", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_technology },
        { "traditions", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_traditions },
        { "naval_capacity_mult", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_naval_capacity_mult },
        { "command_limit_galaxy_mult", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_command_limit_galaxy_mult },
        { "logistic_ceiling_v2", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_logistic_ceiling_v2 },
        { "growth_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_growth_scale },
        { "logistics_fleet_upkeep_costs", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_logistics_fleet_upkeep_costs },
        { "logistics_planet_deficit_costs", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_logistics_planet_deficit_costs },
        { "num_wormhole_pairs", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_num_wormhole_pairs },
        { "num_gateways", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_num_gateways },
        { "num_hyperlanes", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_num_hyperlanes },
        { "fallen_empire_strength_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_fallen_empire_strength_scale },
        { "voidworms_scaling", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_voidworms_scaling },
        { "cutholoids_scaling", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cutholoids_scaling },
        { "cosmic_storm_devastation", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cosmic_storm_devastation },
        { "cosmic_storm_spawn_cooldown_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cosmic_storm_spawn_cooldown_scale },
        { "cosmic_storm_early_game_spawn_chance_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cosmic_storm_early_game_spawn_chance_scale },
        { "cosmic_storm_mid_game_spawn_chance_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cosmic_storm_mid_game_spawn_chance_scale },
        { "cosmic_storm_late_game_spawn_chance_scale", Kind::Fixed, sdk::rt::SGalaxyConfigSettings_cosmic_storm_late_game_spawn_chance_scale },
        { "cosmic_storm_early_game_spawn_max_cap", Kind::Int, gs::cosmic_storm_early_game_spawn_max_cap },
        { "cosmic_storm_mid_game_spawn_max_cap", Kind::Int, gs::cosmic_storm_mid_game_spawn_max_cap },
        { "cosmic_storm_late_game_spawn_max_cap", Kind::Int, gs::cosmic_storm_late_game_spawn_max_cap },
        // the serializer's unset values: difficulty 2 (ensign) and tech scaling 2 (normal), else 0
        { "difficulty", Kind::Enum, gs::difficulty, Names(sdk::enums::Difficulty, 2) },
        { "aggressiveness", Kind::Enum, gs::aggressiveness, Names(sdk::enums::Aggressiveness, 0) },
        { "crisis_type", Kind::Enum, gs::crisis_type, Names(sdk::enums::CrisisType, 0) },
        { "scaling", Kind::Enum, gs::scaling, Names(sdk::enums::Scaling, 0) },
        { "technology_difficulty_scale", Kind::Enum, gs::technology_difficulty_scale, Names(sdk::enums::TechScaling, 2) },
        { "advanced_starts_near_player", Kind::Bool, gs::advanced_starts_near_player },
        { "random_empires", Kind::Bool, gs::random_empires },
        { "random_advanced_empires", Kind::Bool, gs::random_advanced_empires },
        { "random_fallen_empires", Kind::Bool, gs::random_fallen_empires },
        { "random_marauder_empires", Kind::Bool, gs::random_marauder_empires },
        { "random_nomad_empires", Kind::Bool, gs::random_nomad_empires },
        { "caravaneers_enabled", Kind::Bool, gs::caravaneers_enabled },
        { "lgate_enabled", Kind::Bool, gs::lgate_enabled },
        { "difficulty_adjusted_ai_modifiers", Kind::Bool, gs::difficulty_adjusted_ai_modifiers },
        { "clustered", Kind::Bool, sdk::ent::SGalaxyConfigSettingsSP::clustered - kSpOff },
        { "ironman", Kind::Bool, sdk::ent::SGalaxyConfigSettingsSP::ironman - kSpOff },
    };
    return fields;
}

// "DIFFICULTY_GRAND_ADMIRAL" -> "grand_admiral": the names without the prefix they all share
std::vector<std::string> ShortNames(const EnumNames& e) {
    std::string prefix = e.count ? e.keys[0] : "";
    for (int i = 1; i < e.count; ++i) {
        size_t n = 0;
        while (n < prefix.size() && e.keys[i][n] == prefix[n]) ++n;
        prefix.resize(n);
    }
    prefix.resize(prefix.find_last_of('_') == std::string::npos ? 0 : prefix.find_last_of('_') + 1);
    std::vector<std::string> out;
    for (int i = 0; i < e.count; ++i) {
        std::string s = std::string(e.keys[i]).substr(prefix.size());
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        out.push_back(s);
    }
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

GalaxySetupManager& GalaxySetupManager::Get() {
    static GalaxySetupManager instance;
    return instance;
}

bool GalaxySetupManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    return true;
}

uintptr_t GalaxySetupManager::Settings() const {
    void* settings = nullptr;
    return base_address_ && ReadVal(base_address_ + sdk::glob::CSettings_pInstance, &settings) && settings
               ? (uintptr_t)settings + kSpOff : 0;
}

nlohmann::json GalaxySetupManager::GetJson() {
    const uintptr_t sp = Settings();
    if (!sp) return { {"error", "The game's settings are not loaded"} };
    nlohmann::json values = nlohmann::json::object(), choices = nlohmann::json::object();
    for (const Field& f : Fields()) {
        const uintptr_t a = sp + f.off;
        nlohmann::json v;
        switch (f.kind) {
        case Kind::Str: {
            std::string s = Text(a);
            if (!s.empty()) v = s;
            break;
        }
        case Kind::Int: {
            int32_t i = -1;
            if (ReadVal(a, &i) && i != -1) v = i;
            break;
        }
        case Kind::Fixed: {
            int64_t x = kFixedUnset;
            if (ReadVal(a, &x) && x != kFixedUnset) v = (double)x / kFixedScale;
            break;
        }
        case Kind::Enum: {
            uint32_t e = 0;
            const std::vector<std::string> names = ShortNames(f.names);
            if (ReadVal(a, &e)) v = e < names.size() ? nlohmann::json(names[e]) : nlohmann::json(e);
            nlohmann::json list = nlohmann::json::array();
            for (int i = 0; i < f.names.count; ++i) {
                list.push_back({ {"value", names[i]}, {"name", SafeLocalize(base_address_, f.names.keys[i])} });
            }
            choices[f.name] = list;
            break;
        }
        case Kind::Bool: {
            uint8_t b = 0;
            if (ReadVal(a, &b)) v = b != 0;
            break;
        }
        }
        values[f.name] = v;
    }
    return { {"settings", values}, {"choices", choices},
             {"note", "null: the galaxy size's (template's) own value. Multipliers and odds are 1.0 = normal"} };
}

nlohmann::json GalaxySetupManager::Set(const nlohmann::json& settings) {
    const uintptr_t sp = Settings();
    if (!sp) return { {"error", "The game's settings are not loaded"} };
    if (!settings.is_object() || settings.empty()) return { {"error", "Pass the settings to change as an object"} };
    struct Write {
        const Field* f;
        int64_t number;
        std::string text;
    };
    std::vector<Write> writes;
    std::string known;
    for (const Field& f : Fields()) known += std::string(known.empty() ? "" : ", ") + f.name;
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        const Field* f = nullptr;
        for (const Field& x : Fields()) {
            if (it.key() == x.name) f = &x;
        }
        if (!f) return { {"error", "Unknown galaxy setting " + it.key() + " (known: " + known + ")"} };
        const nlohmann::json& v = it.value();
        Write w{ f, 0, "" };
        switch (f->kind) {
        case Kind::Str:
            if (!v.is_null() && !v.is_string()) return { {"error", it.key() + " is a key (string)"} };
            w.text = v.is_null() ? "" : v.get<std::string>();
            break;
        case Kind::Int:
            if (!v.is_null() && !v.is_number_integer()) return { {"error", it.key() + " is a whole number"} };
            w.number = v.is_null() ? -1 : v.get<int64_t>();
            if (w.number < -1 || w.number > 100000) return { {"error", it.key() + " is out of range"} };
            break;
        case Kind::Fixed:
            if (!v.is_null() && !v.is_number()) return { {"error", it.key() + " is a number (1.0 = normal)"} };
            if (!v.is_null() && (v.get<double>() < 0 || v.get<double>() > 1000)) return { {"error", it.key() + " is out of range"} };
            w.number = v.is_null() ? kFixedUnset : (int64_t)std::llround(v.get<double>() * kFixedScale);
            break;
        case Kind::Enum: {
            const std::vector<std::string> names = ShortNames(f->names);
            if (v.is_null()) {
                w.number = f->names.unset;
            } else if (v.is_number_integer() && v.get<int64_t>() >= 0 && v.get<int64_t>() < f->names.count) {
                w.number = v.get<int64_t>();
            } else if (v.is_string()) {
                w.number = -1;
                for (int i = 0; i < f->names.count; ++i) {
                    if (Lower(v.get<std::string>()) == names[i] || Lower(v.get<std::string>()) == Lower(f->names.keys[i])) w.number = i;
                }
            } else {
                w.number = -1;
            }
            if (w.number < 0) {
                std::string list;
                for (const std::string& n : names) list += (list.empty() ? "" : ", ") + n;
                return { {"error", it.key() + " is one of: " + list} };
            }
            break;
        }
        case Kind::Bool:
            if (!v.is_null() && !v.is_boolean()) return { {"error", it.key() + " is true or false"} };
            w.number = v.is_boolean() && v.get<bool>();
            break;
        }
        writes.push_back(w);
    }
    for (const Write& w : writes) {
        const uintptr_t a = sp + w.f->off;
        bool ok = true;
        switch (w.f->kind) {
        case Kind::Str: {
            AssignCtx ctx{ base_address_ + sdk::fn::CString_Assign, (void*)(a - kCStringHeader), w.text.c_str(), w.text.size() };
            ok = CommandBuilder::Get().CallGuarded(&CallAssign, &ctx);
            break;
        }
        case Kind::Int: ok = WriteVal<int32_t>(a, (int32_t)w.number); break;
        case Kind::Fixed: ok = WriteVal<int64_t>(a, w.number); break;
        case Kind::Enum: ok = WriteVal<uint32_t>(a, (uint32_t)w.number); break;
        case Kind::Bool: ok = WriteVal<uint8_t>(a, (uint8_t)w.number); break;
        }
        if (!ok) return { {"error", std::string("Writing ") + w.f->name + " failed"} };
    }
    nlohmann::json out = GetJson();
    out.erase("choices");
    out["changed"] = writes.size();
    return out;
}

}  // namespace bridge
