#include "hikari/core/log.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

namespace hikari::core {
namespace {

std::mutex& log_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& buffer_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string>& log_buffer() {
    static std::vector<std::string> buffer;
    return buffer;
}

std::atomic_bool g_session_marked = false;

std::filesystem::path log_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::filesystem::path exe(buffer.data(), buffer.data() + length);
    return exe.parent_path() / L"hikari.log";
}

std::string module_for_address(std::uintptr_t address) {
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                                  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCWSTR>(address), &module)) {
        return {};
    }
    wchar_t name[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(module, name, MAX_PATH);
    std::filesystem::path path(std::wstring(name, name + length));
    std::ostringstream out;
    out << path.filename().string() << "+0x" << std::hex
        << (address - reinterpret_cast<std::uintptr_t>(module));
    return out.str();
}

LONG CALLBACK av_tracer(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static std::mutex seen_mutex;
    static std::set<std::uintptr_t> seen;
    const auto rip = info->ContextRecord != nullptr
                         ? static_cast<std::uintptr_t>(info->ContextRecord->Rip)
                         : 0;
    {
        std::lock_guard lock(seen_mutex);
        if (seen.count(rip) != 0 || seen.size() >= 64) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        seen.insert(rip);
    }

    std::vector<std::string> lines;
    {
        std::ostringstream header;
        header << "exception at 0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress)
               << ", rip=0x" << rip << " (access violation)";
        lines.push_back(header.str());
    }

    if (info->ContextRecord != nullptr) {
        CONTEXT context = *info->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        for (int depth = 0; depth < 20; ++depth) {
            if (!::StackWalk64(IMAGE_FILE_MACHINE_AMD64, ::GetCurrentProcess(), ::GetCurrentThread(),
                               &frame, &context, nullptr, nullptr, nullptr, nullptr) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            std::ostringstream line;
            line << "  frame " << depth << ": 0x" << std::hex << frame.AddrPC.Offset;
            const std::string module = module_for_address(static_cast<std::uintptr_t>(frame.AddrPC.Offset));
            line << (module.empty() ? " in <unknown>" : " in " + module);
            lines.push_back(line.str());
        }
    }

    std::ofstream file(log_path(), std::ios::app);
    if (file) {
        for (const auto& line : lines) {
            file << "[Hikari][AV] " << line << '\n';
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void log_msg(const std::string& message) {
    {
        std::lock_guard lock(buffer_mutex());
        auto& buffer = log_buffer();
        if (buffer.size() > 2000) {
            buffer.erase(buffer.begin(), buffer.begin() + 500);
        }
        buffer.push_back(message);
    }

    std::lock_guard lock(log_mutex());
    std::ofstream file(log_path(), std::ios::app);
    if (!file) {
        return;
    }
    if (!g_session_marked.exchange(true, std::memory_order_relaxed)) {
        const char* quad = std::getenv("HIKARI_NATIVE_QUAD");
        file << "===== Hikari session start: pid=" << ::GetCurrentProcessId()
             << " quad=" << (quad != nullptr ? quad : "<unset>") << " =====\n";
    }
    file << "[Hikari] " << message << '\n';
}

std::string get_recent_logs() {
    std::lock_guard lock(buffer_mutex());
    std::ostringstream out;
    const auto& buffer = log_buffer();
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        if (i != 0) {
            out << "\r\n";
        }
        out << buffer[i];
    }
    return out.str();
}

void clear_logs() {
    std::lock_guard lock(buffer_mutex());
    log_buffer().clear();
}

void install_av_tracer() {
    static std::atomic_bool installed = false;
    if (!installed.exchange(true, std::memory_order_relaxed)) {
        ::AddVectoredExceptionHandler(1, av_tracer);
    }
}

} // namespace hikari::core
