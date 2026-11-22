#include "Adapters/Inbound/EquipEventAdapter.h"
#include "Adapters/Inbound/GameEventAdapter.h"
#include "Application/InputController.h"
#include "Config/ConfigAdapter.h"
#include "Config/StyleConfig.h"
#include "Hooks.h"
#include "PCH.h"
#include "Persistence/SaveSpellDB.h"
#include "Persistence/SpellSettingsDB.h"
#include "UI/Menu.h"
#include "UI/Strings.h"
#include "UI/TextureManager.h"

#ifdef MAGIC_DEBUG_LOGGING
    // Windows.h must come before DbgHelp.h, so include sorting stays off for these two.
    // clang-format off
    #include <Windows.h>
    #include <DbgHelp.h>
// clang-format on

    #include <cstring>
    #include <string>
#endif

namespace {
    static std::string g_pendingEssPath;
    static std::string g_currentEssPath;
    static bool g_dbLoaded = false;

    void EnsureSaveSpellDBLoaded() {
        if (!g_dbLoaded) {
            IntegratedMagic::SaveSpellDB::Get().LoadFromDisk();
            g_dbLoaded = true;
        }
    }

    IntegratedMagic::SaveSpellSlots ReadSlotsFromConfig() {
        auto& adapter = IntegratedMagic::Config::MagicConfigAdapter::Get();
        IntegratedMagic::SaveSpellSlots s{};
        adapter.ReadAllSlots(s.left, s.right, s.shout);
        return s;
    }

    void ApplySlotsToConfig(const IntegratedMagic::SaveSpellSlots& s) {
        IntegratedMagic::Config::MagicConfigAdapter::Get().ApplyAllSlots(s.left, s.right, s.shout);
    }

    std::string ExtractKey(std::string s) {
        if (auto pos = s.find_last_of("\\/"); pos != std::string::npos) s = s.substr(pos + 1);
        if (s.size() >= 4) {
            auto tail = s.substr(s.size() - 4);
            for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
            if (tail == ".ess") s.resize(s.size() - 4);
        }
        return s;
    }

    std::string GetSaveKeyFromMsg(const SKSE::MessagingInterface::Message* msg) {
        if (!msg || !msg->data || msg->dataLen <= 0) return {};
        auto* p = reinterpret_cast<const char*>(msg->data);
        std::size_t n = 0;
        while (n < (std::size_t)msg->dataLen && p[n] != '\0') ++n;
        std::string raw(p, n);
        std::string key = ExtractKey(std::move(raw));
        return IntegratedMagic::SaveSpellDB::NormalizeKey(std::move(key));
    }

    bool ReadPostLoadOk(const SKSE::MessagingInterface::Message* message) {
        if (!message) {
            return true;
        }
        if (const auto raw = reinterpret_cast<std::uintptr_t>(message->data); raw == 0u || raw == 1u) {
            return raw != 0u;
        }
        if (message->data && message->dataLen == sizeof(bool)) {
            return *reinterpret_cast<const bool*>(message->data);
        }
        return message->data != nullptr;
    }

    void InitializeLogger() {
        if (auto path = SKSE::log::log_directory()) {
            *path /= "IntegratedMagic.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("global", sink);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("Logger iniciado.");
        }
    }

    void GlobalMessageHandler(SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        switch (message->type) {
            case SKSE::MessagingInterface::kPreLoadGame: {
                g_pendingEssPath = GetSaveKeyFromMsg(message);

                MAGIC_DEBUG_LOG("[SaveLoad] kPreLoadGame: raw key='{}'", g_pendingEssPath);

                break;
            }
            case SKSE::MessagingInterface::kDataLoaded: {
                IntegratedMagic::Strings::Load();
                IntegratedMagic::GetMagicConfig().Load();
                IntegratedMagic::SpellSettingsDB::Get().Load();
                IntegratedMagic::MENU::Register();
                Application::InputController::Get().OnConfigChanged();

                CastGuardEvents::Get().Register();
                IntegratedMagic::EquipSink::RegisterEquipListener();
                break;
            }
            case SKSE::MessagingInterface::kPostLoadGame: {
                const bool ok = ReadPostLoadOk(message);

                MAGIC_DEBUG_LOG("[SaveLoad] kPostLoadGame: ok={} pendingEssPath='{}'", ok, g_pendingEssPath);

                if (ok && !g_pendingEssPath.empty()) {
                    EnsureSaveSpellDBLoaded();
                    g_currentEssPath = g_pendingEssPath;
                    IntegratedMagic::SaveSpellSlots slots{};
                    const bool found = IntegratedMagic::SaveSpellDB::Get().TryGet(g_currentEssPath, slots);

                    MAGIC_DEBUG_LOG("[SaveLoad] TryGet key='{}' found={}", g_currentEssPath, found);

                    if (found) {
                        MAGIC_DEBUG_LOG("[SaveLoad] slots size: left={} right={} shout={}", slots.left.size(),
                                        slots.right.size(), slots.shout.size());
                        for (std::size_t i = 0; i < slots.left.size(); ++i)
                            MAGIC_DEBUG_LOG("[SaveLoad]   slot[{}] left={:#010x} right={:#010x} shout={:#010x}", i,
                                            slots.left[i], slots.right[i],
                                            i < slots.shout.size() ? slots.shout[i] : 0u);

                        ApplySlotsToConfig(slots);
                    } else {
                        MAGIC_DEBUG_LOG("[SaveLoad] key not found in DB, clearing slots");

                        ApplySlotsToConfig(IntegratedMagic::SaveSpellSlots{});
                    }
                }

                else {
                    MAGIC_DEBUG_LOG("[SaveLoad] kPostLoadGame: skipped (ok={} pendingEmpty={})", ok,
                                    g_pendingEssPath.empty());
                }

                g_pendingEssPath.clear();
                break;
            }
            case SKSE::MessagingInterface::kSaveGame: {
                std::string key = GetSaveKeyFromMsg(message);

                MAGIC_DEBUG_LOG("[SaveLoad] kSaveGame: raw key='{}'", key);

                if (key.empty()) key = g_currentEssPath;

                MAGIC_DEBUG_LOG("[SaveLoad] kSaveGame: final key='{}'", key);

                if (!key.empty()) {
                    EnsureSaveSpellDBLoaded();
                    const auto slots = ReadSlotsFromConfig();

                    MAGIC_DEBUG_LOG("[SaveLoad] saving slots size: left={} right={} shout={}", slots.left.size(),
                                    slots.right.size(), slots.shout.size());

                    IntegratedMagic::SaveSpellDB::Get().Upsert(key, slots);
                    IntegratedMagic::SaveSpellDB::Get().SaveToDisk();
                }
                break;
            }
            case SKSE::MessagingInterface::kDeleteGame: {
                std::string key = GetSaveKeyFromMsg(message);
                if (key.empty()) {
                    key = g_currentEssPath;
                }
                if (!key.empty()) {
                    EnsureSaveSpellDBLoaded();
                    IntegratedMagic::SaveSpellDB::Get().Erase(key);
                    IntegratedMagic::SaveSpellDB::Get().SaveToDisk();
                    if (key == g_currentEssPath) {
                        g_currentEssPath.clear();
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    // The plugin load. The [Startup] breadcrumbs (debug build) name the step a start-up fault happened in: the logger
    // flushes every line, so the last one in the log is the step that was running (spec 010).
    bool LoadPlugin(const SKSE::LoadInterface* skse) {
        SKSE::Init(skse, {.trampoline = true, .trampolineSize = IntegratedMagic::Hooks::kHookSpaceBytes});
        InitializeLogger();
        MAGIC_DEBUG_LOG("[Startup] StyleConfig::Load");
        IntegratedMagic::StyleConfig::Get().Load();
        MAGIC_DEBUG_LOG("[Startup] RegisterListener");
        if (const auto mi = SKSE::GetMessagingInterface()) {
            mi->RegisterListener(GlobalMessageHandler);
        }
        // The game module base is never 0 here, unless a static object reached the game at DLL load (research R7).
        MAGIC_DEBUG_LOG("[Startup] Install_Hooks (game module base=0x{:X})", REL::Module::get().base());
        IntegratedMagic::Hooks::Install_Hooks();
        MAGIC_DEBUG_LOG("[Startup] SKSEPluginLoad done");
        return true;
    }

#ifdef MAGIC_DEBUG_LOGGING
    // Start-up fault report (debug build only; spec 010, contracts/load-fault-report.md). SKSE catches a fault that
    // escapes SKSEPlugin_Load and logs only "fatal error occurred while loading plugin". This filter logs the exception
    // code, the faulting module + RVA and a symbolized stack first, then lets SKSE handle the fault exactly as before.
    constexpr DWORD kCppExceptionCode = 0xE06D7363;
    constexpr int kMaxFaultFrames = 24;
    bool g_loadFaultReported = false;

    const char* FaultName(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
                return "access violation";
            case EXCEPTION_BREAKPOINT:
                return "breakpoint";
            case EXCEPTION_STACK_OVERFLOW:
                return "stack overflow";
            case kCppExceptionCode:
                return "C++ exception";
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                return "illegal instruction";
            default:
                return "other";
        }
    }

    // File name part of a path. Plain char handling: nothing here may throw inside the exception filter.
    const char* BaseName(const char* path) {
        const char* slash = std::strrchr(path, '\\');
        const char* fwd = std::strrchr(path, '/');
        if (fwd && (!slash || fwd > slash)) slash = fwd;
        return slash ? slash + 1 : path;
    }

    // "<module>+0x<rva>", or the raw address when no module contains it.
    std::string DescribeModule(DWORD64 addr) {
        HMODULE mod = nullptr;
        constexpr DWORD kFlags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (!GetModuleHandleExW(kFlags, reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
            return std::format("0x{:016X}", addr);
        }
        char path[MAX_PATH]{};
        const char* name = GetModuleFileNameA(mod, path, MAX_PATH) != 0 ? BaseName(path) : "unknown";
        return std::format("{}+0x{:X}", name, addr - reinterpret_cast<DWORD64>(mod));
    }

    // DescribeModule, plus " <symbol>+0x<disp> (<file>:<line>)" when dbghelp resolves it from the PDB.
    std::string DescribeAddress(DWORD64 addr) {
        std::string out = DescribeModule(addr);
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), addr, &displacement, symbol)) {
            out += std::format(" {}+0x{:X}", symbol->Name, displacement);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &lineDisplacement, &line) && line.FileName) {
                out += std::format(" ({}:{})", BaseName(line.FileName), line.LineNumber);
            }
        }
        return out;
    }

    // The folder of this DLL, where the debug build's post-build step copies the PDB.
    std::string PluginFolder() {
        HMODULE self = nullptr;
        constexpr DWORD kFlags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        char path[MAX_PATH]{};
        if (!GetModuleHandleExW(kFlags, reinterpret_cast<LPCWSTR>(&PluginFolder), &self) ||
            GetModuleFileNameA(self, path, MAX_PATH) == 0) {
            return {};
        }
        std::string folder{path};
        folder.resize(static_cast<std::size_t>(BaseName(path) - path));
        return folder;
    }

    // Exception filter: logs once, and never handles the fault (always EXCEPTION_CONTINUE_SEARCH).
    LONG ReportLoadFault(EXCEPTION_POINTERS* ep) {
        if (g_loadFaultReported || !ep || !ep->ExceptionRecord || !ep->ContextRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        g_loadFaultReported = true;
        const auto* record = ep->ExceptionRecord;
        const DWORD code = record->ExceptionCode;
        const auto faultAddr = reinterpret_cast<DWORD64>(record->ExceptionAddress);
        MAGIC_DEBUG_LOG("[Startup] load fault: code=0x{:08X} ({}) at {}", code, FaultName(code),
                        DescribeModule(faultAddr));
        if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            const char* access = kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "unknown";
            MAGIC_DEBUG_LOG("[Startup] load fault: access violation, {} at 0x{:016X}", access,
                            static_cast<DWORD64>(record->ExceptionInformation[1]));
        }
        // A stack overflow leaves no stack for dbghelp: the first line is all that can be reported.
        if (code == EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;

        const HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        const std::string searchPath = PluginFolder();
        // Another module may already own dbghelp for this process: then SymInitialize fails, its symbols may still
        // resolve, and its state must not be torn down by SymCleanup.
        const bool ownSymbols =
            SymInitialize(process, searchPath.empty() ? nullptr : searchPath.c_str(), TRUE) != FALSE;
        if (!ownSymbols) {
            MAGIC_DEBUG_LOG("[Startup] load fault: SymInitialize failed (error {}), frames may lack symbols",
                            GetLastError());
        }

        CONTEXT context = *ep->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        for (int i = 0; i < kMaxFaultFrames; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            MAGIC_DEBUG_LOG("[Startup] load fault: frame {}: {}", i, DescribeAddress(frame.AddrPC.Offset));
        }
        if (ownSymbols) SymCleanup(process);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // __try needs a function with no C++ objects to unwind (C2712). The handler never runs: the filter always
    // returns EXCEPTION_CONTINUE_SEARCH, so SKSE handles the fault as it did before.
    bool LoadPluginWithFaultReport(const SKSE::LoadInterface* skse) {
        __try {
            return LoadPlugin(skse);
        } __except (ReportLoadFault(GetExceptionInformation())) {
            return false;
        }
    }
#endif
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse) {
#ifdef MAGIC_DEBUG_LOGGING
    return LoadPluginWithFaultReport(skse);
#else
    return LoadPlugin(skse);
#endif
}