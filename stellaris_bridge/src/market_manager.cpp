#include "market_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "command_builder.hpp"
#include "game_state.hpp"
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

    return true;
}

void* MarketManager::GetPlayerCountry() {
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

std::string MarketManager::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

void* MarketManager::FindStrategicResource(const std::string& resource_key) {
    if (!base_address_) return nullptr;

    void* res_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CStrategicResourceDatabase_pInstance), &res_db) || !res_db || (uintptr_t)res_db < 0x10000) {
        return nullptr;
    }
    if (!res_db || (uintptr_t)res_db < 0x10000) {
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
    SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentInGameIdler), &idler);
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
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CStrategicResourceDatabase_pInstance), &res_db) || !res_db || (uintptr_t)res_db < 0x10000) {
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

    nlohmann::json monthly = ReadMonthlyTrades(GameState::Get().GetPlayerCountryId());
    return {
        {"is_galactic_market", is_galactic_market},
        {"market_fee_percent", market_fee * 100.0},
        {"settlement_currency", "trade"},
        {"tradable_resources_count", res_list.size()},
        {"resources", res_list},
        {"active_monthly_trades_count", monthly.size()},
        {"monthly_trades", monthly}
    };
}

std::vector<MarketManager::MonthlyOrder> MarketManager::ReadMonthlyOrders(uint32_t country_id) {
    std::vector<MonthlyOrder> out;

    // CGameState::AccessMarket(): the market lives at g_CurrentGameState + 0xAF0
    // (CAddMonthlyTradeCommand::Execute, 0x1D11B30). Not a serialized member, so not in the SDK.
    constexpr std::ptrdiff_t kGameStateMarket = 0xAF0;
    // CPdxArray<SMonthlyTradeData>: data at CMarket::monthly_trades, count 0xC further; 0x30-byte items.
    constexpr std::ptrdiff_t kItemSize = 0x30;
    namespace m = sdk::ent::SMonthlyTradeData;
    namespace t = sdk::ent::STradeData;

    void* game_state = nullptr;
    void* market = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &game_state) || !game_state ||
        !SafeReadPtr((const void*)((uintptr_t)game_state + kGameStateMarket), &market) || !market) {
        return out;
    }
    void* items = nullptr;
    uint32_t count = 0;
    SafeReadPtr((const void*)((uintptr_t)market + sdk::ent::CMarket::monthly_trades), &items);
    SafeReadU32((const void*)((uintptr_t)market + sdk::ent::CMarket::monthly_trades + 0xC), &count);
    if (!items || count > 4096) {
        return out;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t item = (uintptr_t)items + i * kItemSize;
        uintptr_t trade = item + m::trade_data;
        MonthlyOrder o;
        SafeReadU32((const void*)(trade + t::country), &o.country);
        if (o.country != country_id) continue;
        SafeReadPtr((const void*)(trade + t::resource), &o.resource);
        SafeReadU32((const void*)(trade + t::trade_type), &o.type);
        SafeReadU32((const void*)(item + m::amount), (uint32_t*)&o.amount);
        SafeReadU32((const void*)(item + m::price), (uint32_t*)&o.price);
        SafeReadU32((const void*)(item + m::id), (uint32_t*)&o.id);
        out.push_back(o);
    }
    return out;
}

nlohmann::json MarketManager::ReadMonthlyTrades(uint32_t country_id) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& o : ReadMonthlyOrders(country_id)) {
        std::string key;
        if (o.resource) SafeReadPdxString((const void*)((uintptr_t)o.resource + 0x30), key);
        out.push_back({
            {"order_id", o.id},
            {"resource", key},
            {"localized_name", key.empty() ? key : LocalizeKey(key)},
            {"action", o.type == 0 ? "buy" : o.type == 1 ? "sell" : "unknown"},
            {"amount", o.amount},
            {"max_unit_price", o.price}  // plain trade value; 0 = no limit
        });
    }
    return out;
}

nlohmann::json MarketManager::GetSummaryJson() {
    double market_fee = 0.30;
    bool is_galactic_market = false;
    void* idler = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentInGameIdler), &idler) && idler) {
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

    namespace buy = sdk::cmd::market_buy_resource_command;
    namespace sell = sdk::cmd::market_sell_resource_command;
    namespace trade = sdk::ent::STradeData;
    static_assert(buy::trade_data == sell::trade_data && buy::multiplier == sell::multiplier,
                  "market buy/sell commands are expected to share one payload layout");
    // The factory already constructed the embedded STradeData (vtable + defaults).
    auto cmd = CommandBuilder::Get().Create(is_buy ? buy::kSpec : sell::kSpec);
    cmd.Set<void*>(buy::trade_data + trade::resource, res_ptr)
       .Set<uint32_t>(buy::trade_data + trade::trade_type, is_buy ? 0u : 1u)
       .Set<uint32_t>(buy::trade_data + trade::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(buy::multiplier, batches);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    out_message = (is_buy ? "Successfully purchased " : "Successfully sold ") +
                  std::to_string(actual_units) + " units (" + std::to_string(batches) + " batches) of " + resource_key;
    return true;
}

bool MarketManager::SetMonthlyTrade(const std::string& resource_key, const std::string& action,
                                    double amount, double price_limit, bool cancel, int32_t order_id,
                                    std::string& out_message) {
    if (cancel && order_id < 0) {
        out_message = "cancel requires order_id (the monthly trade order to remove)";
        return false;
    }
    void* res_ptr = nullptr;
    if (!cancel) {  // a cancel is fully described by order_id
        res_ptr = FindStrategicResource(resource_key);
        if (!res_ptr) {
            out_message = "Resource '" + resource_key + "' not found in tradable database";
            return false;
        }
    }

    std::string act = action;
    std::transform(act.begin(), act.end(), act.begin(), ::tolower);
    bool is_buy = (act == "buy" || act == "purchase");

    namespace add = sdk::cmd::add_monthly_trade_command;
    namespace remove = sdk::cmd::remove_monthly_trade_command;
    namespace monthly = sdk::ent::SMonthlyTradeData;
    namespace trade = sdk::ent::STradeData;
    static_assert(add::monthly_trade_data == remove::monthly_trade_data,
                  "add/remove monthly trade commands are expected to share one payload layout");
    constexpr std::ptrdiff_t kMonthly = add::monthly_trade_data;
    constexpr std::ptrdiff_t kTrade = kMonthly + monthly::trade_data;
    const uint32_t country_id = GameState::Get().GetPlayerCountryId();

    MonthlyOrder order;
    if (cancel) {
        // CMarket::RemoveMonthlyTrade only removes an order whose resource, type, amount, price
        // and id all match, so copy the live order instead of trusting the caller's arguments.
        auto orders = ReadMonthlyOrders(country_id);
        auto it = std::find_if(orders.begin(), orders.end(), [&](const MonthlyOrder& o) { return o.id == order_id; });
        if (it == orders.end()) {
            out_message = "No monthly trade order with id " + std::to_string(order_id);
            return false;
        }
        order = *it;
    } else {
        order.resource = res_ptr;
        order.type = is_buy ? 0u : 1u;
        order.amount = (int32_t)amount;
        order.price = (int32_t)price_limit;  // max unit price, plain trade value
        order.id = -1;                       // new order; the engine assigns the id
    }

    auto cmd = CommandBuilder::Get().Create(cancel ? remove::kSpec : add::kSpec);
    cmd.Set<void*>(kTrade + trade::resource, order.resource)
       .Set<uint32_t>(kTrade + trade::trade_type, order.type)
       .Set<uint32_t>(kTrade + trade::country, country_id)
       .Set<int32_t>(kMonthly + monthly::amount, order.amount)
       .Set<int32_t>(kMonthly + monthly::price, order.price)
       .Set<int32_t>(kMonthly + monthly::id, order.id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    if (cancel) {
        out_message = "Successfully removed monthly trade order " + std::to_string(order_id);
    } else {
        out_message = "Successfully created monthly " + act + " order of " +
                      std::to_string((int)amount) + " " + resource_key;
    }
    return true;
}

} // namespace bridge
