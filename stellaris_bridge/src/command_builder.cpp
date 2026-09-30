#include "command_builder.hpp"
#include "commands.hpp"
#include "MinHook.h"

namespace bridge {

namespace {

using FnFactory = void* (*)();
using FnEngineAlloc = void* (*)(size_t);
using FnScalarDtor = void* (*)(void* self, unsigned flags);
using FnIsValid = bool (*)(void* self, void* reason_pdx_string);
using FnFreePdxString = void (*)(void* pdx_string);

constexpr size_t kSlotIsValid = 8;  // CCommand::IsValid(CString*) const

// MSVC std::string layout; command string payloads (sdk offsets) point at one of these.
struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

// Engine CString (IsValid's reason out-param, localisation results): a 16-byte header followed
// by a std::string. Same layout as PdxLocResult in common.hpp.
struct RawCString {
    uint64_t header[2];
    RawPdxString str;
};
static_assert(sizeof(RawCString) == 0x30, "CString is 0x30 bytes");

// ---- pure virtual call guard -------------------------------------------------------------
// A pure virtual call makes the CRT call abort(), which SEH cannot catch. While the bridge is
// inside one of the SEH-guarded engine calls below, the hooked _purecall raises this exception
// instead, so a bad object turns into an error rather than a dead game.
constexpr DWORD kPureVirtualCallException = 0xE0505643;  // 'PVC'
thread_local int t_engine_call_depth = 0;
thread_local DWORD t_last_exception = 0;

using FnPurecall = void (*)();
FnPurecall g_original_purecall = nullptr;

void PurecallDetour() {
    if (t_engine_call_depth > 0) {
        RaiseException(kPureVirtualCallException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    g_original_purecall();
}

int RecordException(DWORD code) {
    t_last_exception = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

std::string LastExceptionText() {
    if (t_last_exception == kPureVirtualCallException) {
        return "pure virtual call inside the engine (an object handed to it has a wrong vtable)";
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "exception 0x%08lX", (unsigned long)t_last_exception);
    return buf;
}

// SEH wrappers stay free of C++ objects with destructors (C2712). Each one marks the thread as
// being inside a guarded engine call for PurecallDetour.

bool ReadTokenGetter(uintptr_t vtable, uint32_t* out_token) {
    __try {
        const uint8_t* fn = *(const uint8_t**)(vtable + 10 * sizeof(void*));
        if (fn[0] != 0xB8 || fn[5] != 0xC3) {  // mov eax, imm32; ret
            return false;
        }
        *out_token = *(const uint32_t*)(fn + 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* CallFactory(uintptr_t factory) {
    void* obj = nullptr;
    ++t_engine_call_depth;
    __try {
        obj = ((FnFactory)factory)();
    } __except (RecordException(GetExceptionCode())) {
        obj = nullptr;
    }
    --t_engine_call_depth;
    return obj;
}

bool CallPost(Commands::FnPostCommand post, void* cmd, bool force) {
    bool ok = true;
    ++t_engine_call_depth;
    __try {
        post(cmd, force);
    } __except (RecordException(GetExceptionCode())) {
        ok = false;
    }
    --t_engine_call_depth;
    return ok;
}

void CallDestroy(void* cmd) {
    ++t_engine_call_depth;
    __try {
        auto dtor = *(FnScalarDtor*)(*(uintptr_t*)cmd);  // vtable slot 0: scalar deleting dtor
        dtor(cmd, 1);
    } __except (RecordException(GetExceptionCode())) {
    }
    --t_engine_call_depth;
}

bool CallIsValid(void* cmd, void* reason, bool* out_ok) {
    bool ok = true;
    ++t_engine_call_depth;
    __try {
        auto fn = *(FnIsValid*)(*(uintptr_t*)cmd + kSlotIsValid * sizeof(void*));
        *out_ok = fn(cmd, reason);
    } __except (RecordException(GetExceptionCode())) {
        ok = false;
    }
    --t_engine_call_depth;
    return ok;
}

void CallFreePdxString(uintptr_t fn, void* str) {
    __try {
        ((FnFreePdxString)fn)(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool CallRawGuarded(uintptr_t fn) {
    bool ok = true;
    ++t_engine_call_depth;
    __try {
        ((void (*)())fn)();
    } __except (RecordException(GetExceptionCode())) {
        ok = false;
    }
    --t_engine_call_depth;
    return ok;
}

bool CallPredicate4(uintptr_t fn, void* a, void* b, void* c, void* d, bool* out) {
    bool ok = true;
    ++t_engine_call_depth;
    __try {
        *out = ((bool (*)(void*, void*, void*, void*))fn)(a, b, c, d);
    } __except (RecordException(GetExceptionCode())) {
        ok = false;
    }
    --t_engine_call_depth;
    return ok;
}

bool CallTextGuarded(CommandBuilder::FnTextCall call, void* ctx, void* out) {
    bool ok = true;
    ++t_engine_call_depth;
    __try {
        call(ctx, out);
    } __except (RecordException(GetExceptionCode())) {
        ok = false;
    }
    --t_engine_call_depth;
    return ok;
}

void* CallAlloc(uintptr_t fn, size_t n) {
    __try {
        return ((FnEngineAlloc)fn)(n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

std::string Hex(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

} // namespace

// ------------------------------------------------------------------ CommandBuilder

CommandBuilder& CommandBuilder::Get() {
    static CommandBuilder instance;
    return instance;
}

void CommandBuilder::Init(uintptr_t base_address) {
    base_ = base_address;
    auto dos = (PIMAGE_DOS_HEADER)base_;
    auto nt = (PIMAGE_NT_HEADERS)(base_ + dos->e_lfanew);
    exe_timestamp_ = nt->FileHeader.TimeDateStamp;
    sdk_matches_ = exe_timestamp_ == sdk::kExeTimestamp;
    if (sdk_matches_) {
        LOGF("[CMD_BUILDER] SDK matches stellaris.exe (TimeDateStamp 0x%08X)", exe_timestamp_);
    } else {
        LOGF("[CMD_BUILDER] SDK MISMATCH: exe TimeDateStamp 0x%08X, SDK dumped from 0x%08X. "
             "Commands are disabled; run tools/sdk_dumper/dump.py.", exe_timestamp_, sdk::kExeTimestamp);
    }
}

bool CommandBuilder::InstallGuards() {
    if (!base_ || !sdk_matches_) {
        return false;
    }
    void* target = (void*)(base_ + sdk::fn::CRT_purecall);
    if (MH_CreateHook(target, (LPVOID)&PurecallDetour, (LPVOID*)&g_original_purecall) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        LOG("[CMD_BUILDER] could not hook _purecall; pure virtual calls will still abort the game");
        return false;
    }
    LOGF("[CMD_BUILDER] pure virtual call guard installed on _purecall (0x%llX)", (unsigned long long)sdk::fn::CRT_purecall);
    return true;
}

nlohmann::json CommandBuilder::SelfTestPurecallGuard() {
    if (!g_original_purecall) {
        return { {"error", {{"code", -32090}, {"message", "guard not installed; refusing to call _purecall"}}}} ;
    }
    // Calls the engine's _purecall inside a guarded wrapper: with the guard working this raises
    // the bridge's exception and comes back; without it the CRT would abort the game.
    t_last_exception = 0;
    bool returned_normally = CallRawGuarded(base_ + sdk::fn::CRT_purecall);
    bool caught = !returned_normally && t_last_exception == kPureVirtualCallException;
    return { {"success", caught}, {"guard_caught_pure_virtual_call", caught}, {"detail", LastExceptionText()} };
}

void* CommandBuilder::EngineAlloc(size_t n) const {
    if (!base_ || !sdk_matches_) return nullptr;
    return CallAlloc(base_ + sdk::kRvaEngineAlloc, n);
}

NativeCommand CommandBuilder::Create(const sdk::CmdSpec& spec) {
    if (!base_) {
        return NativeCommand(&spec, nullptr, "CommandBuilder not initialized");
    }
    if (!sdk_matches_) {
        return NativeCommand(&spec, nullptr,
            "SDK was generated for a different stellaris.exe (exe " + Hex(exe_timestamp_) + ", SDK " +
            Hex(sdk::kExeTimestamp) + "); run tools/sdk_dumper/dump.py");
    }
    uintptr_t vtable = base_ + spec.vtable_rva;
    uint32_t token = 0;
    if (!ReadTokenGetter(vtable, &token) || token != spec.token) {
        return NativeCommand(&spec, nullptr,
            std::string(spec.name) + ": vtable " + Hex(spec.vtable_rva) + " does not report token " + Hex(spec.token));
    }
    void* obj = nullptr;
    if (spec.factory_rva) {
        obj = CallFactory(base_ + spec.factory_rva);
    } else if (spec.size >= 0x20) {
        // No engine factory (the UI builds this one on the stack). Reproduce what every
        // factory does for the CCommand header; payload fields start zeroed.
        obj = CallAlloc(base_ + sdk::kRvaEngineAlloc, spec.size);
        if (obj) {
            memset(obj, 0, spec.size);
            *(uintptr_t*)obj = vtable;
            *(uint32_t*)((uintptr_t)obj + 0x08) = 0xFFFFFFFF;
            *(uint32_t*)((uintptr_t)obj + 0x12) = 0xFFFF;
        }
    }
    if (!obj) {
        return NativeCommand(&spec, nullptr, std::string(spec.name) + ": engine factory failed (" + LastExceptionText() + ")");
    }
    if (*(uintptr_t*)obj != vtable) {
        // Not our object type; do not try to destroy something we do not understand.
        return NativeCommand(&spec, nullptr, std::string(spec.name) + ": factory returned an object with an unexpected vtable");
    }
    return NativeCommand(&spec, obj, {});
}

NativeCommand CommandBuilder::Adopt(const sdk::CmdSpec& spec, void* engine_object) {
    if (!engine_object) {
        return NativeCommand(&spec, nullptr, std::string(spec.name) + ": no command object to adopt");
    }
    if (!base_ || !sdk_matches_) {
        return NativeCommand(&spec, nullptr, "SDK was generated for a different stellaris.exe; run tools/sdk_dumper/dump.py");
    }
    uintptr_t vtable = base_ + spec.vtable_rva;
    uint32_t token = 0;
    if (*(uintptr_t*)engine_object != vtable || !ReadTokenGetter(vtable, &token) || token != spec.token) {
        // Not the expected type: leave it alone rather than destroy something unknown.
        return NativeCommand(&spec, nullptr, std::string(spec.name) + ": adopted object is not this command type");
    }
    return NativeCommand(&spec, engine_object, {});
}

// ------------------------------------------------------------------ NativeCommand

NativeCommand::NativeCommand(const sdk::CmdSpec* spec, void* obj, std::string error)
    : spec_(spec), obj_(obj), error_(std::move(error)) {
    if (!error_.empty()) {
        LOGF("[CMD_BUILDER] %s", error_.c_str());
    }
}

NativeCommand::NativeCommand(NativeCommand&& other) noexcept
    : spec_(other.spec_), obj_(other.obj_), error_(std::move(other.error_)) {
    other.obj_ = nullptr;
}

NativeCommand::~NativeCommand() {
    if (obj_) {
        CallDestroy(obj_);
    }
}

bool NativeCommand::CheckRange(std::ptrdiff_t off, size_t n) {
    if (!obj_ || !error_.empty()) {
        return false;
    }
    if (off < 0x20 || (size_t)off + n > spec_->size) {  // never touch the CCommand header
        error_ = std::string(spec_->name) + ": field write at " + Hex((uint64_t)off) + " outside payload (size " + Hex(spec_->size) + ")";
        LOGF("[CMD_BUILDER] %s", error_.c_str());
        return false;
    }
    return true;
}

NativeCommand& NativeCommand::SetBytes(std::ptrdiff_t off, const void* data, size_t n) {
    if (CheckRange(off, n)) {
        memcpy((void*)((uintptr_t)obj_ + off), data, n);
    }
    return *this;
}

NativeCommand& NativeCommand::SetString(std::ptrdiff_t off, const std::string& value) {
    if (!CheckRange(off, sizeof(RawPdxString))) {
        return *this;
    }
    auto* str = (RawPdxString*)((uintptr_t)obj_ + off);
    // Factories leave strings empty and inline (capacity 15), so nothing needs freeing here.
    if (value.size() <= 15) {
        memset(str->buf, 0, sizeof(str->buf));
        memcpy(str->buf, value.data(), value.size());
        str->capacity = 15;
    } else {
        if (value.size() >= 4096) {  // MSVC uses an over-aligned block with a header for big strings
            error_ = std::string(spec_->name) + ": string too long";
            return *this;
        }
        char* heap = (char*)CommandBuilder::Get().EngineAlloc(value.size() + 1);
        if (!heap) {
            error_ = std::string(spec_->name) + ": engine allocation for string failed";
            return *this;
        }
        memcpy(heap, value.data(), value.size());
        heap[value.size()] = '\0';
        str->heap_ptr = heap;
        str->capacity = value.size();
    }
    str->size = value.size();
    return *this;
}

// Copies an engine CString reason out (rich-text markup stripped) and frees its heap buffer.
static void TakeReasonText(RawCString& text, std::string* reason, uint64_t max_size = 4096) {
    if (reason && text.str.size > 0 && text.str.size < max_size) {
        const char* p = text.str.capacity > 15 ? text.str.heap_ptr : text.str.buf;
        if (p) {
            *reason = RenderPdxMarkup(p, (size_t)text.str.size);
        }
    }
    if (text.str.capacity > 15) {
        CallFreePdxString(CommandBuilder::Get().Base() + kRvaFreePdxString, &text);
    }
}

bool CommandBuilder::CallGuarded(FnTextCall call, void* ctx) {
    if (!base_ || !sdk_matches_) {
        return false;
    }
    if (!CallTextGuarded(call, ctx, nullptr)) {
        LOGF("[CMD_BUILDER] engine call failed: %s", LastExceptionText().c_str());
        return false;
    }
    return true;
}

static bool SafeReadPtr(const void* addr, void** out) {
    __try {
        *out = *(void* const*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CommandBuilder::RunConsoleCommand(const std::string& line, std::string* error) {
    if (!base_ || !sdk_matches_) {
        if (error) *error = "SDK does not match the running exe";
        return false;
    }
    if (line.empty() || line.size() >= 4096) {
        if (error) *error = "command line must be 1..4095 characters";
        return false;
    }
    void* console = nullptr;
    if (!SafeReadPtr((const void*)(base_ + sdk::glob::CConsole_pInstance), &console) || !console) {
        if (error) *error = "CConsole::_pInstance is null (console not created yet)";
        return false;
    }

    // Engine CString: 16-byte header (zero) + MSVC std::string. A long line lives in an engine
    // heap block, like command string payloads; RunCommandNow only reads it.
    RawCString cmd{};
    if (line.size() <= 15) {
        memcpy(cmd.str.buf, line.data(), line.size());
        cmd.str.capacity = 15;
    } else {
        char* heap = (char*)EngineAlloc(line.size() + 1);
        if (!heap) {
            if (error) *error = "engine allocation for the command line failed";
            return false;
        }
        memcpy(heap, line.data(), line.size());
        heap[line.size()] = '\0';
        cmd.str.heap_ptr = heap;
        cmd.str.capacity = line.size();
    }
    cmd.str.size = line.size();

    struct Ctx { uintptr_t fn; void* console; void* cmd; };
    Ctx ctx{ base_ + sdk::fn::CConsole_RunCommandNow, console, &cmd };
    const bool ok = CallTextGuarded([](void* c, void*) {
        auto* p = (Ctx*)c;
        ((void (*)(void*, const void*))p->fn)(p->console, p->cmd);
    }, &ctx, nullptr);
    if (cmd.str.capacity > 15) {
        CallFreePdxString(base_ + kRvaFreePdxString, &cmd);
    }
    if (!ok) {
        if (error) *error = "engine raised: " + LastExceptionText();
        LOGF("[CMD_BUILDER] console command failed: %s", LastExceptionText().c_str());
    }
    return ok;
}

bool CommandBuilder::CallForText(FnTextCall call, void* ctx, std::string* text) {
    if (!base_ || !sdk_matches_) {
        return false;
    }
    // The callee constructs the CString in place (return slot) or appends to it.
    RawCString out{};
    out.str.capacity = 15;
    if (!CallTextGuarded(call, ctx, &out)) {
        LOGF("[CMD_BUILDER] engine text call failed: %s", LastExceptionText().c_str());
        return false;
    }
    TakeReasonText(out, text, 1u << 20);
    return true;
}

bool CommandBuilder::CallPredicate(uintptr_t fn_rva, void* self, void* a, void* b, std::string* reason) {
    if (!base_ || !sdk_matches_) {
        if (reason) *reason = "SDK was generated for a different stellaris.exe; run tools/sdk_dumper/dump.py";
        return false;
    }
    RawCString text{};
    text.str.capacity = 15;
    bool result = false;
    if (!CallPredicate4(base_ + fn_rva, self, a, b, reason ? &text : nullptr, &result)) {
        if (reason) *reason = "engine check failed: " + LastExceptionText();
        return false;
    }
    TakeReasonText(text, result ? nullptr : reason);
    return result;
}

bool NativeCommand::IsValid(std::string* reason) {
    if (!obj_ || !error_.empty()) {
        if (reason) *reason = error_.empty() ? "command was not created" : error_;
        return false;
    }
    RawCString text{};
    text.str.capacity = 15;
    bool ok = false;
    if (!CallIsValid(obj_, &text, &ok)) {
        std::string what = std::string(spec_->name) + ": IsValid failed: " + LastExceptionText();
        if (reason) *reason = what;
        LOGF("[CMD_BUILDER] %s", what.c_str());
        return false;
    }
    TakeReasonText(text, ok ? nullptr : reason);
    return ok;
}

bool NativeCommand::Post(Check check) {
    if (!obj_ || !error_.empty()) {
        if (error_.empty()) error_ = "command was not created";
        return false;
    }
    auto post = Commands::Get().GetPostCommand();
    if (!post) {
        error_ = "PostCommand not resolved";
        return false;
    }
    if (check == Check::IsValid) {
        std::string why;
        if (!IsValid(&why)) {
            error_ = std::string(spec_->name) + " rejected by the engine" + (why.empty() ? "" : ": " + why);
            LOGF("[CMD_BUILDER] %s", error_.c_str());
            return false;  // still ours: the destructor frees it
        }
    }
    void* cmd = obj_;
    obj_ = nullptr;  // the engine owns it from here, even if PostCommand faults part-way
    const bool force = check == Check::None;
    if (!CallPost(post, cmd, force)) {
        error_ = std::string(spec_->name) + ": PostCommand failed: " + LastExceptionText();
        LOGF("[CMD_BUILDER] %s", error_.c_str());
        return false;
    }
    LOGF("[CMD_BUILDER] posted %s%s", spec_->name, force ? " (force)" : "");
    return true;
}

} // namespace bridge
