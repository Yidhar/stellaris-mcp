#include "contacts_manager.hpp"
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

static bool SafeLocalizeCall(ContactsManager::FnLocalize fn_localize,
                             ContactsManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(ContactsManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

ContactsManager& ContactsManager::Get() {
    static ContactsManager instance;
    return instance;
}

bool ContactsManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);

    return true;
}

void* ContactsManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    if (mgr && (uintptr_t)mgr >= 0x10000) {
        void* countries_arr = nullptr;
        uint32_t count = 0;
        if (SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) && countries_arr &&
            SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &count) && count > 0) {
            void* country_0 = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &country_0) && country_0) {
                return country_0;
            }
        }
    }
    return nullptr;
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
    SafeReadPtr((const void*)((uintptr_t)country + 0x2C50), &rel_arr);
    SafeReadU32((const void*)((uintptr_t)country + 0x2C58), &rel_cnt);

    std::string q_mode = mode;
    std::transform(q_mode.begin(), q_mode.end(), q_mode.begin(), ::tolower);
    if (q_mode.empty()) q_mode = "all";

    nlohmann::json empires = nlohmann::json::array();
    nlohmann::json first_contacts = nlohmann::json::array();

    uint32_t known_count = 0;

    for (uint32_t i = 0; i < rel_cnt && rel_arr; ++i) {
        void* rel = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)rel_arr + i * 8), &rel) || !rel) continue;

        uint32_t other_ref = 0;
        SafeReadU32((const void*)((uintptr_t)rel + 0x0C), &other_ref);
        uint32_t other_id = other_ref & 0xFFFFFF;

        // Skip self (country 0)
        if (other_id == 0 || other_id >= c_cap) continue;

        void* other_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)carr + other_id * 16 + 8), &other_ptr) || !other_ptr) continue;

        uint8_t f0 = 0, f1 = 0, f2 = 0;
        SafeReadU8((const void*)((uintptr_t)rel + 0x120), &f0);
        SafeReadU8((const void*)((uintptr_t)rel + 0x121), &f1);
        SafeReadU8((const void*)((uintptr_t)rel + 0x122), &f2);

        bool has_comm = (f0 & 0x08) != 0;
        bool has_contact = (f2 & 0x01) != 0;
        bool comm_pact = (f1 & 0x08) != 0;
        bool res_pact = (f1 & 0x10) != 0;
        bool mig_pact = (f1 & 0x20) != 0;

        // Read country name
        std::string name_key;
        SafeReadPdxString((const void*)((uintptr_t)other_ptr + 0x1538), name_key);
        if (name_key.empty()) {
            SafeReadPdxString((const void*)((uintptr_t)other_ptr + 0x1500), name_key);
        }

        std::string loc_name = LocalizeKey(name_key);
        if (loc_name.empty()) {
            loc_name = "Empire " + std::to_string(other_id);
        }

        // Country type
        std::string country_type = "default";
        void* ct_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)other_ptr + 0x1A0), &ct_ptr) && ct_ptr) {
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
    SafeReadPtr((const void*)((uintptr_t)country + 0x2C50), &rel_arr);
    SafeReadU32((const void*)((uintptr_t)country + 0x2C58), &rel_cnt);

    uint32_t known = 0;
    uint32_t pending = 0;
    for (uint32_t i = 0; i < rel_cnt && rel_arr; ++i) {
        void* rel = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)rel_arr + i * 8), &rel) || !rel) continue;

        uint32_t other_ref = 0;
        SafeReadU32((const void*)((uintptr_t)rel + 0x0C), &other_ref);
        uint32_t other_id = other_ref & 0xFFFFFF;
        if (other_id == 0 || other_id >= c_cap) continue;

        uint8_t f0 = 0, f2 = 0;
        SafeReadU8((const void*)((uintptr_t)rel + 0x120), &f0);
        SafeReadU8((const void*)((uintptr_t)rel + 0x122), &f2);

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
