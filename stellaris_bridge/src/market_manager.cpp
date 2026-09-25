#include "market_manager.hpp"
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

static bool SafeReadI64(const void* addr, int64_t* out) {
    __try {
        *out = *(const int64_t*)addr;
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

static bool SafeLocalizeCall(MarketManager::FnLocalize fn_localize,
                             MarketManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(MarketManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

MarketManager& MarketManager::Get() {
    static MarketManager instance;
    return instance;
}

bool MarketManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);

    return true;
}

void* MarketManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112F50), &mgr) && mgr) {
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

std::string MarketManager::LocalizeKey(const std::string& key) {
    if (key.empty() || !fn_localize_) return key;

    RawPdxString in_key{};
    in_key.size = key.size();
    in_key.capacity = 15;
    if (key.size() < 16) {
        memcpy(in_key.buf, key.data(), key.size());
    } else {
        return key;
    }

    RawPdxString out_str{};
    if (!SafeLocalizeCall(fn_localize_, fn_free_pdx_str_, &in_key, &out_str)) {
        return key;
    }

    std::string result;
    if (out_str.size > 0 && out_str.size < 4096) {
        if (out_str.capacity < 16) {
            char temp[16]{ 0 };
            size_t len = out_str.size < 16 ? (size_t)out_str.size : 15;
            memcpy(temp, out_str.buf, len);
            result = std::string(temp, len);
        } else if (out_str.heap_ptr) {
            size_t len = out_str.size < 512 ? (size_t)out_str.size : 512;
            result = std::string(out_str.heap_ptr, len);
        }
    }

    if (fn_free_pdx_str_) {
        SafeFreePdxStr(fn_free_pdx_str_, &out_str);
    }

    return result.empty() ? key : result;
}

void* MarketManager::FindStrategicResource(const std::string& resource_key) {
    if (!base_address_) return nullptr;

    void* res_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3150E78), &res_db) || !res_db) {
        return nullptr;
    }

    uint32_t cnt = 0;
    void* arr = nullptr;
    if (!SafeReadU32((const void*)((uintptr_t)res_db + 0x14), &cnt) || !cnt ||
        !SafeReadPtr((const void*)((uintptr_t)res_db + 0x08), &arr) || !arr) {
        return nullptr;
    }

    std::string q_upper = resource_key;
    std::transform(q_upper.begin(), q_upper.end(), q_upper.begin(), ::tolower);

    for (uint32_t i = 0; i < cnt; ++i) {
        void* res_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &res_ptr) || !res_ptr) continue;

        std::string key;
        SafeReadPdxString((const void*)((uintptr_t)res_ptr + 0x30), key);
        std::string key_lower = key;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

        if (key_lower == q_upper) {
            return res_ptr;
        }
    }
    return nullptr;
}

bool MarketManager::IsResourceUnlocked(void* country, const std::string& key) {
    if (key == "energy" || key == "minerals" || key == "food" || key == "consumer_goods" || key == "alloys") {
        return true;
    }
    if (!country) return false;

    // Check researched technologies in CTechnology (+0x16E0 + 0x20)
    void* tech_arr = nullptr;
    uint32_t tech_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x16E0 + 0x20), &tech_arr) && tech_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x16E0 + 0x28), &tech_cnt)) {
        std::string req_tech = "";
        if (key == "volatile_motes") req_tech = "volatile_motes";
        else if (key == "exotic_gases") req_tech = "exotic_gases";
        else if (key == "rare_crystals") req_tech = "rare_crystals";
        else if (key == "sr_living_metal") req_tech = "living_metal";
        else if (key == "sr_zro") req_tech = "zro";
        else if (key == "sr_dark_matter") req_tech = "dark_matter";

        for (uint32_t i = 0; i < tech_cnt; ++i) {
            void* p_tech = nullptr;
            // Each entry in std::vector<ResearchedTechEntry> is 0x28 bytes, with CTechnology* at +0x10
            uintptr_t entry_addr = (uintptr_t)tech_arr + i * 0x28;
            if (!SafeReadPtr((const void*)(entry_addr + 0x10), &p_tech) || !p_tech) continue;
            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tech + 0x20), t_key);
            if (!req_tech.empty() && t_key.find(req_tech) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

nlohmann::json MarketManager::GetMarketInfo() {
    if (!base_address_) return { {"error", "Bridge base address not set"} };

    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not found"} };

    // Standard market fee in Stellaris is 30% (0.30)
    double market_fee = 0.30;
    bool is_galactic_market = false;

    // Check idler for CMarketView if present
    void* idler = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113180), &idler);
    if (idler) {
        void* mview = nullptr;
        SafeReadPtr((const void*)((uintptr_t)idler + 0xDC0), &mview);
        if (mview) {
            // Check if galactic market
            uint8_t gm_flag = 0;
            SafeCopyChars((char*)&gm_flag, (const char*)((uintptr_t)mview + 0x548), 1);
            if (gm_flag) is_galactic_market = true;
        }
    }

    void* res_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3150E78), &res_db) || !res_db) {
        return { {"error", "Resource database not found"} };
    }

    uint32_t cnt = 0;
    void* arr = nullptr;
    if (!SafeReadU32((const void*)((uintptr_t)res_db + 0x14), &cnt) || !cnt ||
        !SafeReadPtr((const void*)((uintptr_t)res_db + 0x08), &arr) || !arr) {
        return { {"error", "Failed to read resource database"} };
    }

    // Read player stockpiles from [Country + 0x2B40] + 0x30
    std::unordered_map<std::string, double> stockpiles;
    void* bal_ptr = nullptr;
    void* stock_arr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x2B40), &bal_ptr) && bal_ptr) {
        if (SafeReadPtr((const void*)((uintptr_t)bal_ptr + 0x30), &stock_arr) && stock_arr) {
            for (uint32_t i = 0; i < cnt; ++i) {
                int64_t val_raw = 0;
                if (SafeReadI64((const void*)((uintptr_t)stock_arr + i * 8), &val_raw)) {
                    void* res_ptr = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &res_ptr);
                    if (res_ptr) {
                        std::string k;
                        SafeReadPdxString((const void*)((uintptr_t)res_ptr + 0x30), k);
                        if (!k.empty()) {
                            stockpiles[k] = std::round((val_raw / 100000.0) * 100.0) / 100.0;
                        }
                    }
                }
            }
        }
    }

    nlohmann::json res_list = nlohmann::json::array();

    for (uint32_t i = 0; i < cnt; ++i) {
        void* res_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &res_ptr) || !res_ptr) continue;

        int64_t base_amt_raw = 0;
        int64_t base_price_raw = 0;
        SafeReadI64((const void*)((uintptr_t)res_ptr + 0x158), &base_amt_raw);
        SafeReadI64((const void*)((uintptr_t)res_ptr + 0x160), &base_price_raw);

        if (base_amt_raw <= 0 || base_price_raw <= 0) {
            // Not a tradable market resource
            continue;
        }

        std::string key;
        SafeReadPdxString((const void*)((uintptr_t)res_ptr + 0x30), key);
        if (key.empty()) continue;

        double batch_size = base_amt_raw / 100000.0;
        double batch_price = base_price_raw / 100000.0;
        double unit_base_price = (batch_size > 0.0) ? (batch_price / batch_size) : 1.0;

        // Trade fee
        double buy_price_per_unit = unit_base_price * (1.0 + market_fee);
        double sell_price_per_unit = unit_base_price * (1.0 - market_fee);

        double cur_stock = stockpiles.count(key) ? stockpiles[key] : 0.0;

        bool is_unlocked = IsResourceUnlocked(country, key);

        // Only return resources that the player has unlocked
        if (!is_unlocked) {
            continue;
        }

        res_list.push_back({
            {"key", key},
            {"localized_name", LocalizeKey(key)},
            {"batch_size", batch_size},
            {"unit_base_price", unit_base_price},
            {"buy_price_per_unit", buy_price_per_unit},
            {"sell_price_per_unit", sell_price_per_unit},
            {"stockpile", cur_stock}
        });
    }

    return {
        {"is_galactic_market", is_galactic_market},
        {"market_fee_percent", market_fee * 100.0},
        {"settlement_currency", "trade"},
        {"tradable_resources_count", res_list.size()},
        {"resources", res_list},
        {"active_monthly_trades_count", 0},
        {"monthly_trades", nlohmann::json::array()}
    };
}

nlohmann::json MarketManager::GetSummaryJson() {
    double market_fee = 0.30;
    bool is_galactic_market = false;
    void* idler = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3113180), &idler) && idler) {
        void* mview = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)idler + 0xDC0), &mview) && mview) {
            uint8_t gm_flag = 0;
            SafeCopyChars((char*)&gm_flag, (const char*)((uintptr_t)mview + 0x548), 1);
            if (gm_flag) is_galactic_market = true;
        }
    }

    void* country = GetPlayerCountry();
    uint32_t normal_count = 0;
    if (country) {
        // Core 5 are always unlocked: energy, minerals, food, consumer_goods, alloys
        normal_count = 5;
        for (const char* k : { "volatile_motes", "exotic_gases", "rare_crystals", "sr_living_metal", "sr_zro", "sr_dark_matter" }) {
            if (IsResourceUnlocked(country, k)) {
                normal_count++;
            }
        }
    }

    return {
        {"is_galactic_market", is_galactic_market},
        {"market_fee_percent", market_fee * 100.0},
        {"settlement_currency", "trade"},
        {"normally_tradable_commodities_count", normal_count}
    };
}

bool MarketManager::ExecuteInstantTrade(const std::string& resource_key, const std::string& action,
                                        uint32_t units, std::string& out_message) {
    if (!fn_engine_alloc_ || !fn_post_command_) {
        out_message = "Engine functions not initialized";
        return false;
    }

    void* country = GetPlayerCountry();
    if (!country) {
        out_message = "Player country not found";
        return false;
    }

    void* res_ptr = FindStrategicResource(resource_key);
    if (!res_ptr) {
        out_message = "Resource '" + resource_key + "' not found in tradable database";
        return false;
    }

    int64_t base_amt_raw = 0;
    SafeReadI64((const void*)((uintptr_t)res_ptr + 0x158), &base_amt_raw);
    if (base_amt_raw <= 0) {
        out_message = "Resource '" + resource_key + "' is not tradable on the market";
        return false;
    }

    double batch_size = base_amt_raw / 100000.0;
    if (units == 0) units = (uint32_t)batch_size;

    // Calculate batches (at least 1)
    uint32_t batches = units / (uint32_t)batch_size;
    if (batches == 0) batches = 1;
    uint32_t actual_units = batches * (uint32_t)batch_size;

    std::string act = action;
    std::transform(act.begin(), act.end(), act.begin(), ::tolower);

    bool is_buy = (act == "buy" || act == "purchase");
    bool is_sell = (act == "sell");

    if (!is_buy && !is_sell) {
        out_message = "Invalid trade action '" + action + "'. Must be 'buy' or 'sell'";
        return false;
    }

    // Allocate 0x40 bytes for CMarketBuyResourceCommand / CMarketSellResourceCommand
    void* pCmd = fn_engine_alloc_(0x40);
    if (!pCmd) {
        out_message = "Engine memory allocation failed";
        return false;
    }

    memset(pCmd, 0, 0x40);

    // Vtable selection
    uintptr_t vt = is_buy ? (base_address_ + 0x245BD60) : (base_address_ + 0x23C9128);
    uintptr_t vt_trade_data = base_address_ + 0x2334838;

    *(uintptr_t*)pCmd = vt;
    *(uint32_t*)((uintptr_t)pCmd + 0x08) = 0; // player country
    *(uint32_t*)((uintptr_t)pCmd + 0x10) = 0xFFFF0000;

    // STradeData payload
    *(uintptr_t*)((uintptr_t)pCmd + 0x20) = vt_trade_data;
    *(void**)((uintptr_t)pCmd + 0x28) = res_ptr;
    *(uint32_t*)((uintptr_t)pCmd + 0x30) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x34) = 0; // country ref
    *(uint32_t*)((uintptr_t)pCmd + 0x38) = batches; // batch multiplier

    fn_post_command_(pCmd, 0);

    out_message = (is_buy ? "Successfully purchased " : "Successfully sold ") +
                  std::to_string(actual_units) + " units (" + std::to_string(batches) + " batches) of " + resource_key;
    return true;
}

bool MarketManager::SetMonthlyTrade(const std::string& resource_key, const std::string& action,
                                    double amount, double price_limit, bool cancel, std::string& out_message) {
    if (!fn_engine_alloc_ || !fn_post_command_) {
        out_message = "Engine functions not initialized";
        return false;
    }

    void* res_ptr = FindStrategicResource(resource_key);
    if (!res_ptr) {
        out_message = "Resource '" + resource_key + "' not found in tradable database";
        return false;
    }

    std::string act = action;
    std::transform(act.begin(), act.end(), act.begin(), ::tolower);
    bool is_buy = (act == "buy" || act == "purchase");

    // Size 0x50 bytes for CAddMonthlyTradeCommand / CRemoveMonthlyTradeCommand
    void* pCmd = fn_engine_alloc_(0x50);
    if (!pCmd) {
        out_message = "Engine memory allocation failed";
        return false;
    }

    memset(pCmd, 0, 0x50);

    uintptr_t vt = cancel ? (base_address_ + 0x2418BA0) : (base_address_ + 0x2418C58);
    uintptr_t vt_monthly_1 = base_address_ + 0x2334800;
    uintptr_t vt_monthly_2 = base_address_ + 0x2334838;

    *(uintptr_t*)pCmd = vt;
    *(uint32_t*)((uintptr_t)pCmd + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)((uintptr_t)pCmd + 0x0C) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x10) = 0xFFFF0000;
    *(uint16_t*)((uintptr_t)pCmd + 0x14) = 0;
    *(uint8_t*)((uintptr_t)pCmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x18) = 0;

    // SMonthlyTradeData at +0x20
    *(uintptr_t*)((uintptr_t)pCmd + 0x20) = vt_monthly_1;
    *(uintptr_t*)((uintptr_t)pCmd + 0x28) = vt_monthly_2;
    *(void**)((uintptr_t)pCmd + 0x30) = res_ptr;
    *(uint32_t*)((uintptr_t)pCmd + 0x38) = is_buy ? 0 : 1; // 0=buy, 1=sell
    *(uint32_t*)((uintptr_t)pCmd + 0x3C) = 0; // country id 0
    *(uint32_t*)((uintptr_t)pCmd + 0x40) = (uint32_t)amount; // amount (integer)
    *(uint32_t*)((uintptr_t)pCmd + 0x44) = (uint32_t)(price_limit * 100000.0); // price limit fixed point
    *(uint32_t*)((uintptr_t)pCmd + 0x48) = price_limit > 0.0 ? 1 : 0; // has price limit flag

    fn_post_command_(pCmd, 0);

    if (cancel) {
        out_message = "Successfully removed monthly trade order for " + resource_key;
    } else {
        out_message = "Successfully created monthly " + act + " order of " +
                      std::to_string((int)amount) + " " + resource_key;
    }
    return true;
}

} // namespace bridge
