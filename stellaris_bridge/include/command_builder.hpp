#pragma once

#include "common.hpp"
#include "sdk/stellaris_sdk.hpp"

namespace bridge {

// A native command created by the engine's own factory (sdk::cmd::<name>::kSpec), so the
// allocation size, header and engine defaults always match the running build. Callers only
// write payload fields, using offsets from the generated SDK.
//
//   auto cmd = CommandBuilder::Get().Create(sdk::cmd::research_technology_command::kSpec);
//   cmd.Set<uint32_t>(sdk::cmd::research_technology_command::country, country_id)
//      .Set<void*>(sdk::cmd::research_technology_command::technology, tech);
//   if (!cmd.Post()) return error(cmd.error());
class NativeCommand {
public:
    NativeCommand(NativeCommand&& other) noexcept;
    NativeCommand(const NativeCommand&) = delete;
    NativeCommand& operator=(const NativeCommand&) = delete;
    NativeCommand& operator=(NativeCommand&&) = delete;
    ~NativeCommand();  // destroys the command if it was never handed to the engine

    explicit operator bool() const { return obj_ != nullptr && error_.empty(); }
    const std::string& error() const { return error_; }
    void* get() const { return obj_; }

    template <typename T>
    NativeCommand& Set(std::ptrdiff_t off, T value) {
        if (CheckRange(off, sizeof(T))) {
            *(T*)((uintptr_t)obj_ + off) = value;
        }
        return *this;
    }

    // Copies a block (e.g. a POD sub-struct such as SSpeciesRights) into the payload.
    NativeCommand& SetBytes(std::ptrdiff_t off, const void* data, size_t n);

    // Writes a PdxString (MSVC std::string layout: inline buf / heap ptr [16], size, capacity).
    NativeCommand& SetString(std::ptrdiff_t off, const std::string& value);

    // Runs the command's own IsValid(CString* reason) (vtable slot 8) without dispatching it.
    // On failure *reason receives the engine's explanation when it provides one. Main thread only.
    bool IsValid(std::string* reason = nullptr);

    // Hands the command to the engine (PostCommand takes ownership). Main thread only.
    enum class Check {
        // Run IsValid first and fail with the engine's reason instead of letting PostCommand's
        // gate drop the command silently. What the UI does before enabling an action.
        IsValid,
        // Skip IsValid but keep PostCommand's own gate (force=false, as 330 of 332 engine call
        // sites do). For commands that only become valid after an earlier queued command runs.
        EngineGate,
        // PostCommand(force=true): no checks at all. Only when a check is known to be wrong.
        None,
    };
    bool Post(Check check = Check::IsValid);

private:
    friend class CommandBuilder;
    NativeCommand(const sdk::CmdSpec* spec, void* obj, std::string error);
    bool CheckRange(std::ptrdiff_t off, size_t n);

    const sdk::CmdSpec* spec_;
    void* obj_;
    std::string error_;
};

class CommandBuilder {
public:
    static CommandBuilder& Get();

    void Init(uintptr_t base_address);

    // Hooks the engine's _purecall so a pure virtual call inside a guarded engine call becomes
    // an error instead of abort(). Needs MinHook initialized (after HookManager::Init).
    bool InstallGuards();
    // Deliberately triggers a pure virtual call inside a guarded call; only via IPC self_test.
    nlohmann::json SelfTestPurecallGuard();

    // Fails (without touching the engine) when the SDK was dumped from a different exe or when
    // the vtable at spec.vtable_rva does not report spec.token.
    NativeCommand Create(const sdk::CmdSpec& spec);

    // Takes ownership of a command the engine constructed itself (e.g. a Clone of a stack-built
    // command whose embedded objects need engine copy constructors), after checking that it is
    // `spec`'s command type. Validate/post it like any created command.
    NativeCommand Adopt(const sdk::CmdSpec& spec, void* engine_object);

    bool SdkMatchesExe() const { return sdk_matches_; }
    uintptr_t Base() const { return base_; }
    uint32_t ExeTimestamp() const { return exe_timestamp_; }

    // Calls a const engine predicate `bool fn(self, a, b, CString* reason)` (SEH/purecall
    // guarded). On false, `reason` gets the engine's text with markup stripped; passing
    // reason = nullptr passes a null CString*, which such predicates accept.
    bool CallPredicate(uintptr_t fn_rva, void* self, void* a, void* b, std::string* reason);

    // Runs `call(ctx, out)` under the SEH/purecall guard, where `out` is an engine CString that
    // the engine function fills (return slot or append target), and returns its text with
    // rich-text markup stripped. `call` forwards to the engine function with its own argument
    // order; keep it free of C++ objects with destructors.
    using FnTextCall = void (*)(void* ctx, void* out_cstring);
    bool CallForText(FnTextCall call, void* ctx, std::string* text);
    // Runs `call(ctx, nullptr)` under the same guard, for engine queries whose result `call`
    // stores into ctx itself. Returns false if the engine call raised.
    bool CallGuarded(FnTextCall call, void* ctx);

    // Engine operator new; memory handed to engine-owned objects must come from here.
    void* EngineAlloc(size_t n) const;

private:
    CommandBuilder() = default;

    uintptr_t base_{ 0 };
    uint32_t exe_timestamp_{ 0 };
    bool sdk_matches_{ false };
};

} // namespace bridge
