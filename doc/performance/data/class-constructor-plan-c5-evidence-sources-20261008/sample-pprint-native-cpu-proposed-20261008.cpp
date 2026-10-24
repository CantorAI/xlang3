// Diagnostic only: sample ONE child owned by this process. Never attach to a
// supplied PID, change privileges, or treat suspended execution as a speed score.
// Build separately with VS18 x64 cl, /std:c++17 /EHsc /O2 /MT, dbghelp.lib psapi.lib.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dbghelp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _M_X64
#error This diagnostic requires an x64 Windows compiler.
#endif

namespace {
constexpr wchar_t kChild[] = L"D:\\CantorAI\\xlang3\\build-repro\\main-verify-20261006\\Release\\xlang3.exe";
constexpr wchar_t kRoot[] = L"D:\\CantorAI\\xlang3";
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = other.value; other.value = nullptr; }
        return *this;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
void check(bool okay, const char* what) {
    if (!okay) throw std::runtime_error(std::string(what) + ": Win32=" + std::to_string(GetLastError()));
}
std::string utf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    check(size > 0, "WideCharToMultiByte size");
    std::string output(size, '\0');
    check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), size, nullptr, nullptr) == size, "WideCharToMultiByte");
    return output;
}
std::string json_string(const std::string& input) {
    std::ostringstream out; out << '"';
    for (unsigned char c : input) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
        else out << static_cast<char>(c);
    }
    out << '"'; return out.str();
}
std::wstring quoted(const std::wstring& value) {
    std::wstring out = L"\""; size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\'); out += L'"'; return out;
}
std::wstring full_path(const std::wstring& value) {
    std::vector<wchar_t> buffer(32768);
    const DWORD count = GetFullPathNameW(value.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    check(count && count < buffer.size(), "GetFullPathNameW"); return std::wstring(buffer.data(), count);
}
std::uint64_t ticks(FILETIME value) { return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
bool cpu_times(HANDLE thread, std::uint64_t& user, std::uint64_t& kernel) {
    FILETIME created{}, ended{}, k{}, u{};
    if (!GetThreadTimes(thread, &created, &ended, &k, &u)) return false;
    user = ticks(u); kernel = ticks(k); return true;
}
double seconds_since(LARGE_INTEGER start, LARGE_INTEGER frequency) {
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
}
struct Thread {
    DWORD id; Handle handle; std::uint64_t user = 0, kernel = 0, observed_ticks = 0, selected_ticks = 0, samples = 0;
    Thread(DWORD tid, Handle h) : id(tid), handle(std::move(h)) {}
};
void discover_threads(DWORD pid, std::vector<Thread>& threads) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    check(static_cast<bool>(snapshot), "CreateToolhelp32Snapshot");
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot.value, &entry)) return;
    do {
        if (entry.th32OwnerProcessID != pid || std::any_of(threads.begin(), threads.end(), [&](const Thread& t) { return t.id == entry.th32ThreadID; })) continue;
        Handle handle(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID));
        if (handle && GetProcessIdOfThread(handle.value) == pid) {
            Thread candidate(entry.th32ThreadID, std::move(handle));
            if (cpu_times(candidate.handle.value, candidate.user, candidate.kernel)) threads.emplace_back(std::move(candidate));
        }
    } while (Thread32Next(snapshot.value, &entry));
}
// StackWalk64 and DbgHelp state live only in this sampler process. No symbol
// formatting, file output or vector allocation occurs while a child is paused.
struct ResumeOnce {
    HANDLE thread; bool& failed;
    ~ResumeOnce() { if (ResumeThread(thread) == static_cast<DWORD>(-1)) failed = true; }
};
struct Captured { std::array<DWORD64, 32> pcs{}; unsigned count = 0; DWORD error = 0; DWORD prior_suspend = 0; bool resume_failed = false; double pause_seconds = 0; };
Captured capture(HANDLE process, HANDLE thread, bool stack_enabled, LARGE_INTEGER frequency) {
    Captured output; LARGE_INTEGER started{}; QueryPerformanceCounter(&started);
    const DWORD previous = SuspendThread(thread); output.prior_suspend = previous;
    if (previous == static_cast<DWORD>(-1)) { output.error = GetLastError(); return output; }
    {
        ResumeOnce resume{thread, output.resume_failed};
        // Respect an existing suspension: undo only our increment, never sample
        // or resume a debugger/other owner's suspension count.
        if (previous == 0) {
            CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
            if (!GetThreadContext(thread, &context)) output.error = GetLastError();
            else {
                output.pcs[output.count++] = context.Rip;
                if (stack_enabled) {
                    STACKFRAME64 frame{};
                    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
                    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
                    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
                    for (unsigned attempt = 0; attempt < 32 && output.count < output.pcs.size(); ++attempt) {
                        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !frame.AddrPC.Offset) break;
                        if (frame.AddrPC.Offset == output.pcs[output.count - 1]) continue;
                        output.pcs[output.count++] = frame.AddrPC.Offset;
                    }
                }
            }
        }
    }
    output.pause_seconds = seconds_since(started, frequency); return output;
}
std::string complete_stdout(HANDLE reader) {
    LARGE_INTEGER size{}, zero{}; check(GetFileSizeEx(reader, &size), "stdout GetFileSizeEx");
    check(size.QuadPart <= 1024 * 1024, "stdout larger than bounded diagnostic output");
    check(SetFilePointerEx(reader, zero, nullptr, FILE_BEGIN), "stdout rewind");
    std::string raw(static_cast<size_t>(size.QuadPart), '\0'); DWORD count = 0;
    check(raw.empty() || ReadFile(reader, raw.data(), static_cast<DWORD>(raw.size()), &count, nullptr), "stdout ReadFile");
    raw.resize(count); const auto newline = raw.rfind('\n'); return newline == std::string::npos ? std::string{} : raw.substr(0, newline + 1);
}
bool boundary(const std::string& raw, const char* value) { return raw.find(std::string("\"status\": \"") + value + "\"") != std::string::npos; }
void modules(HANDLE process, std::ofstream& out, const char* phase) {
    std::vector<HMODULE> handles(256); DWORD needed = 0;
    if (!EnumProcessModulesEx(process, handles.data(), static_cast<DWORD>(handles.size() * sizeof(HMODULE)), &needed, LIST_MODULES_64BIT)) {
        out << "{\"event\":\"module_error\",\"phase\":" << json_string(phase) << ",\"win32_error\":" << GetLastError() << "}\n"; out.flush(); return;
    }
    if (needed > handles.size() * sizeof(HMODULE)) {
        handles.resize(needed / sizeof(HMODULE));
        check(EnumProcessModulesEx(process, handles.data(), static_cast<DWORD>(handles.size() * sizeof(HMODULE)), &needed, LIST_MODULES_64BIT), "EnumProcessModulesEx resize");
    }
    out << "{\"event\":\"modules\",\"phase\":" << json_string(phase) << ",\"items\":["; bool first = true;
    for (size_t i = 0; i < std::min<size_t>(needed / sizeof(HMODULE), handles.size()); ++i) {
        MODULEINFO info{}; std::array<wchar_t, 32768> name{};
        if (!GetModuleInformation(process, handles[i], &info, sizeof(info))) continue;
        const DWORD length = GetModuleFileNameExW(process, handles[i], name.data(), static_cast<DWORD>(name.size()));
        if (!length || length >= name.size()) continue;
        if (!first) out << ','; first = false;
        out << "{\"path\":" << json_string(utf8(std::wstring(name.data(), length))) << ",\"base\":" << reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll) << ",\"size\":" << info.SizeOfImage << '}';
    }
    out << "]}\n"; out.flush();
}
struct Symbols {
    HANDLE process; bool initialized = false;
    explicit Symbols(HANDLE target) : process(target) {}
    ~Symbols() { if (initialized) SymCleanup(process); }
};
}

int wmain(int argc, wchar_t** argv) {
    // Arguments are fixed child, original child script, original benchmark,
    // fresh stdout/stderr/samples paths. No arbitrary PID or shell is supported.
    if (argc != 7 || _wcsicmp(full_path(argv[1]).c_str(), full_path(kChild).c_str()) != 0) {
        std::fprintf(stderr, "expected fixed child, original script, benchmark, stdout, stderr, samples\n"); return 2;
    }
    std::ofstream output;
    try {
        Handle reservation(CreateFileW(argv[6], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        check(static_cast<bool>(reservation), "reserve fresh samples"); reservation = Handle{};
        output.open(argv[6], std::ios::out | std::ios::binary); check(static_cast<bool>(output), "open samples");
        output.exceptions(std::ios::badbit | std::ios::failbit);
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle stdout_file(CreateFileW(argv[4], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle stderr_file(CreateFileW(argv[5], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        check(stdout_file && stderr_file && input, "fresh child streams");
        Handle job(CreateJobObjectW(nullptr, nullptr)); check(static_cast<bool>(job), "CreateJobObjectW");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "kill-on-close job");
        SIZE_T attribute_bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
        std::vector<unsigned char> attributes(attribute_bytes);
        auto attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        check(InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_bytes) != FALSE, "InitializeProcThreadAttributeList");
        struct AttributesCleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributesCleanup() { DeleteProcThreadAttributeList(value); } } cleanup{attribute_list};
        HANDLE inherited[] = {stdout_file.value, stderr_file.value, input.value};
        check(UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE, "explicit child handles");
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE; startup.StartupInfo.hStdOutput = stdout_file.value;
        startup.StartupInfo.hStdError = stderr_file.value; startup.StartupInfo.hStdInput = input.value; startup.lpAttributeList = attribute_list;
        std::wstring command = quoted(kChild) + L" " + quoted(argv[2]) + L" --benchmark-script " + quoted(argv[3]);
        PROCESS_INFORMATION process_info{};
        check(CreateProcessW(kChild, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, kRoot, &startup.StartupInfo, &process_info) != FALSE, "CreateProcessW fixed child");
        Handle process(process_info.hProcess), initial_thread(process_info.hThread);
        if (!AssignProcessToJobObject(job.value, process.value)) {
            const DWORD error = GetLastError(); TerminateProcess(process.value, 2); WaitForSingleObject(process.value, 15000); SetLastError(error); check(false, "AssignProcessToJobObject");
        }
        Handle reader(CreateFileW(argv[4], GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        check(static_cast<bool>(reader), "independent stdout reader");
        Symbols symbols(process.value); LARGE_INTEGER frequency{}, start{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
        output << "{\"event\":\"launch\",\"pid\":" << process_info.dwProcessId << ",\"child\":" << json_string(utf8(kChild)) << ",\"diagnostic_only\":true,\"sampling_interval_ms\":5,\"cap_seconds\":300}\n"; output.flush();
        check(ResumeThread(initial_thread.value) != static_cast<DWORD>(-1), "resume launched child");
        std::vector<Thread> threads;
        bool started = false, ended = false, timeout = false;
        std::uint64_t sample_count = 0, cpu_zero_windows = 0, capture_errors = 0, externally_suspended = 0, discarded_terminal_races = 0;
        double maximum_pause = 0, total_pause = 0, last_discovery = -1;
        while (WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            const double wall = seconds_since(start, frequency);
            if (wall >= 300) { timeout = true; TerminateJobObject(job.value, 124); break; }
            const auto raw = complete_stdout(reader.value);
            if (boundary(raw, "body_complete") || boundary(raw, "body_failed")) { ended = true; break; }
            if (!started && boundary(raw, "body_start")) {
                started = true; discover_threads(process_info.dwProcessId, threads); last_discovery = wall;
                for (auto& thread : threads) cpu_times(thread.handle.value, thread.user, thread.kernel);
                // Explicit local search path: never request a symbol server or
                // substitute a mismatched Debug PDB for this Release image.
                std::array<wchar_t, 32768> system{};
                const UINT system_length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
                check(system_length > 0 && system_length < system.size(), "GetSystemDirectoryW");
                const std::wstring search = full_path(std::wstring(kChild).substr(0, std::wstring(kChild).find_last_of(L'\\'))) + L";" + system.data();
                SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_NO_PROMPTS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_EXACT_SYMBOLS | SYMOPT_IGNORE_NT_SYMPATH |
                              SYMOPT_PUBLICS_ONLY | SYMOPT_IGNORE_CVREC | SYMOPT_SECURE | SYMOPT_DISABLE_SYMSRV_AUTODETECT);
                symbols.initialized = SymInitializeW(process.value, search.c_str(), TRUE) != FALSE;
                const DWORD unwinder_error = symbols.initialized ? 0 : GetLastError();
                output << "{\"event\":\"window_start\",\"wall_seconds\":" << wall << ",\"stack_unwinder_initialized\":" << (symbols.initialized ? "true" : "false") << ",\"unwinder_error\":" << unwinder_error << "}\n";
                modules(process.value, output, "body-start");
            }
            if (!started) { Sleep(5); continue; }
            if (wall - last_discovery >= .1) { discover_threads(process_info.dwProcessId, threads); last_discovery = wall; }
            Thread* selected = nullptr; std::uint64_t largest_delta = 0;
            for (auto& thread : threads) {
                std::uint64_t user = 0, kernel = 0;
                if (!cpu_times(thread.handle.value, user, kernel) || user < thread.user || kernel < thread.kernel) continue;
                const auto delta = user - thread.user + kernel - thread.kernel;
                thread.user = user; thread.kernel = kernel; thread.observed_ticks += delta;
                if (delta > largest_delta) { largest_delta = delta; selected = &thread; }
            }
            if (!selected) { ++cpu_zero_windows; Sleep(5); continue; }
            auto captured = capture(process.value, selected->handle.value, symbols.initialized, frequency);
            check(!captured.resume_failed, "ResumeThread after context capture");
            maximum_pause = std::max(maximum_pause, captured.pause_seconds); total_pause += captured.pause_seconds;
            if (captured.prior_suspend != 0) { ++externally_suspended; Sleep(5); continue; }
            if (!captured.count) { ++capture_errors; Sleep(5); continue; }
            const auto after = complete_stdout(reader.value);
            if (boundary(after, "body_complete") || boundary(after, "body_failed")) { ++discarded_terminal_races; ended = true; break; }
            ++sample_count; ++selected->samples; selected->selected_ticks += largest_delta;
            output << "{\"event\":\"sample\",\"wall_seconds\":" << seconds_since(start, frequency) << ",\"thread_id\":" << selected->id << ",\"window_cpu_100ns\":" << largest_delta << ",\"pause_seconds\":" << captured.pause_seconds << ",\"pcs\":[";
            for (unsigned i = 0; i < captured.count; ++i) { if (i) output << ','; output << captured.pcs[i]; }
            output << "]}\n"; output.flush(); Sleep(5);
        }
        output << "{\"event\":\"sampling_stop\",\"wall_seconds\":" << seconds_since(start, frequency) << ",\"terminal_output_seen\":" << (ended ? "true" : "false") << "}\n"; output.flush();
        modules(process.value, output, "body-end-or-stop");
        const DWORD remaining = static_cast<DWORD>(std::max(0.0, 300.0 - seconds_since(start, frequency)) * 1000);
        if (WaitForSingleObject(process.value, remaining) == WAIT_TIMEOUT) { timeout = true; TerminateJobObject(job.value, 124); }
        check(WaitForSingleObject(process.value, 15000) == WAIT_OBJECT_0, "child exit after cap");
        const auto raw = complete_stdout(reader.value);
        started = started || boundary(raw, "body_start"); ended = ended || boundary(raw, "body_complete") || boundary(raw, "body_failed");
        DWORD exit_code = 0; check(GetExitCodeProcess(process.value, &exit_code) != FALSE, "GetExitCodeProcess");
        output << "{\"event\":\"terminal\",\"terminal\":true,\"diagnostic_only\":true,\"child_exit_code\":" << exit_code << ",\"timeout\":" << (timeout ? "true" : "false") << ",\"start_seen\":" << (started ? "true" : "false") << ",\"end_seen\":" << (ended ? "true" : "false") << ",\"sample_count\":" << sample_count << ",\"zero_cpu_windows\":" << cpu_zero_windows << ",\"capture_errors\":" << capture_errors << ",\"external_suspensions\":" << externally_suspended << ",\"discarded_terminal_races\":" << discarded_terminal_races << ",\"total_pause_seconds\":" << total_pause << ",\"maximum_pause_seconds\":" << maximum_pause << ",\"threads\":[";
        bool first = true;
        for (const auto& thread : threads) { if (!first) output << ','; first = false; output << "{\"id\":" << thread.id << ",\"observed_cpu_100ns\":" << thread.observed_ticks << ",\"selected_cpu_100ns\":" << thread.selected_ticks << ",\"samples\":" << thread.samples << '}'; }
        output << "]}\n"; output.flush();
        return !timeout && exit_code == 0 && started && ended && sample_count > 0 ? 0 : 1;
    } catch (const std::exception& error) {
        // Kill-on-close job ownership guarantees any still-running child is
        // terminated while unwinding; raw child logs and previous samples remain.
        try { if (output) { output << "{\"event\":\"sampler_failure\",\"terminal\":true,\"error\":" << json_string(error.what()) << "}\n"; output.flush(); } } catch (...) {}
        std::fprintf(stderr, "%s\n", error.what()); return 2;
    }
}
