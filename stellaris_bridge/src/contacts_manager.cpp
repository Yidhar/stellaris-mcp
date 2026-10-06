#include "contacts_manager.hpp"
#include "game_state.hpp"
#include "fleet_access.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <windows.h>
#include <cstring>
#include <algorithm>

namespace bridge {

static bool SafeReadPtr(const void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU8(const void* addr, uint8_t* out) {
    __try {
        *out = *(const uint8_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

ContactsManager& ContactsManager::Get() {
    static ContactsManager instance;
    return instance;
}

bool ContactsManager::Init(uintptr_t base_address) {
    base_address_ = base_address;


    return true;
}

void* ContactsManager::GetPlayerCountry() {
    return GameState::Get().GetPlayerCountry();  // the local player's country (not country 0)
}

std::string ContactsManager::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

nlohmann::json ContactsManager::GetContactsInfo(const std::string& mode) {
    if (!base_address_) return { {"error", "Bridge base address not set"} };

    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not found"} };

    void* cmgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &cmgr) || !cmgr || (uintptr_t)cmgr < 0x10000) {
        return { {"error", "Country manager not found"} };
    }

    void* carr = nullptr;
    uint32_t c_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)cmgr + 0x18), &carr);
    SafeReadU32((const void*)((uintptr_t)cmgr + 0x20), &c_cap);

    void* rel_arr = nullptr;
    uint32_t rel_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + sdk::ent::CCountry::relations_manager + 0x18), &rel_arr);
    SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::relations_manager + 0x20), &rel_cnt);
    uint32_t own_id = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)country + sdk::rt::CCountry_id), &own_id);

    std::string q_mode = mode;
    std::transform(q_mode.begin(), q_mode.end(), q_mode.begin(), ::tolower);
    if (q_mode.empty()) q_mode = "all";

    nlohmann::json empires = nlohmann::json::array();
    nlohmann::json first_contacts = nlohmann::json::array();

    uint32_t known_count = 0;

    for (uint32_t i = 0; i < rel_cnt && rel_arr; ++i) {
        void* rel = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)rel_arr + i * 8), &rel) || !rel) continue;

        // a relation of this country (the array also holds unused, zeroed slots)
        uint32_t rel_owner = 0xFFFFFFFF;
        if (!SafeReadU32((const void*)((uintptr_t)rel + sdk::ent::CRelation::owner), &rel_owner) || rel_owner != own_id) continue;
        uint32_t other_ref = 0;
        SafeReadU32((const void*)((uintptr_t)rel + sdk::ent::CRelation::country), &other_ref);
        uint32_t other_id = other_ref & 0xFFFFFF;

        // skip the player's own relation (any country id is valid, 0 included)
        if (other_ref == own_id || other_id >= c_cap) continue;

        void* other_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)carr + other_id * 16 + 8), &other_ptr) || !other_ptr) continue;

        uint8_t f0 = 0, f1 = 0, f2 = 0;
        // the relation's state bits (contact, communications, pacts ...) start at CRelation::contact
        SafeReadU8((const void*)((uintptr_t)rel + sdk::ent::CRelation::contact), &f0);
        SafeReadU8((const void*)((uintptr_t)rel + sdk::ent::CRelation::contact + 1), &f1);
        SafeReadU8((const void*)((uintptr_t)rel + sdk::ent::CRelation::contact + 2), &f2);

        bool has_comm = (f0 & 0x08) != 0;
        bool has_contact = (f2 & 0x01) != 0;
        bool comm_pact = (f1 & 0x08) != 0;
        bool res_pact = (f1 & 0x10) != 0;
        bool mig_pact = (f1 & 0x20) != 0;

        // the empire's name as the game shows it (a generated name's key is a template such as
        // %ADJECTIVE%, filled from its variables); name_key is that key
        std::string name_key;
        SafeReadPdxString((const void*)((uintptr_t)other_ptr + sdk::ent::CCountry::name + 0x18), name_key);
        std::string loc_name = PersistentNameText((const void*)((uintptr_t)other_ptr + sdk::ent::CCountry::name));
        if (loc_name.empty()) loc_name = LocalizeKey(name_key);

        // Country type
        std::string country_type = "default";
        void* ct_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)other_ptr + sdk::ent::CCountry::type), &ct_ptr) && ct_ptr) {
            std::string ct_key;
            SafeReadPdxString((const void*)((uintptr_t)ct_ptr + 0x20), ct_key);
            if (!ct_key.empty()) country_type = ct_key;
        }

        if (has_comm) {
            known_count++;
            empires.push_back({
                {"country_id", other_id},
                {"name", loc_name},
                {"name_key", name_key},
                {"country_type", country_type},
                {"has_communications", true},
                {"treaties", {
                    {"commercial_pact", comm_pact},
                    {"research_agreement", res_pact},
                    {"migration_pact", mig_pact}
                }}
            });
        } else if (has_contact) {
            // First contact in progress
            first_contacts.push_back({
                {"country_id", other_id},
                {"name", loc_name},
                {"country_type", country_type},
                {"status", "first_contact_in_progress"}
            });
        }
    }

    nlohmann::json resp = {
        {"total_known_empires", known_count},
        {"pending_first_contacts_count", first_contacts.size()}
    };

    if (q_mode == "all" || q_mode == "empires") {
        resp["empires"] = empires;
    }
    if (q_mode == "all" || q_mode == "first_contacts") {
        resp["first_contacts"] = first_contacts;
    }

    return resp;
}

nlohmann::json ContactsManager::GetSummaryJson() {
    void* country = GetPlayerCountry();
    if (!country) return { {"known_empires_count", 0}, {"pending_first_contacts_count", 0} };

    void* cmgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &cmgr) || !cmgr || (uintptr_t)cmgr < 0x10000) {
        return { {"known_empires_count", 0}, {"pending_first_contacts_count", 0} };
    }
    uint32_t c_cap = 0;
    SafeReadU32((const void*)((uintptr_t)cmgr + 0x20), &c_cap);

    void* rel_arr = nullptr;
    uint32_t rel_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + sdk::ent::CCountry::relations_manager + 0x18), &rel_arr);
    SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::relations_manager + 0x20), &rel_cnt);
    uint32_t own_id = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)country + sdk::rt::CCountry_id), &own_id);

    uint32_t known = 0;
    uint32_t pending = 0;
    for (uint32_t i = 0; i < rel_cnt && rel_arr; ++i) {
        void* rel = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)rel_arr + i * 8), &rel) || !rel) continue;

        // a relation of this country (the array also holds unused, zeroed slots)
        uint32_t rel_owner = 0xFFFFFFFF;
        if (!SafeReadU32((const void*)((uintptr_t)rel + sdk::ent::CRelation::owner), &rel_owner) || rel_owner != own_id) continue;
        uint32_t other_ref = 0;
        SafeReadU32((const void*)((uintptr_t)rel + sdk::ent::CRelation::country), &other_ref);
        uint32_t other_id = other_ref & 0xFFFFFF;
        if (other_ref == own_id || other_id >= c_cap) continue;

        uint8_t f0 = 0, f2 = 0;
        SafeReadU8((const void*)((uintptr_t)rel + sdk::ent::CRelation::contact), &f0);
        SafeReadU8((const void*)((uintptr_t)rel + sdk::ent::CRelation::contact + 2), &f2);

        if (f0 & 0x08) {
            known++;
        } else if (f2 & 0x01) {
            pending++;
        }
    }

    return {
        {"known_empires_count", known},
        {"pending_first_contacts_count", pending}
    };
}

} // namespace bridge
