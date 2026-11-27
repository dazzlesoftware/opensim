// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <charconv>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct ResumeThreadOnExit {
    HANDLE thread;

    ~ResumeThreadOnExit() {
        ResumeThread(thread);
    }
};
} // namespace

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    DWORD process_id = 0;
    const auto end = argv[1] + std::strlen(argv[1]);
    const auto parsed = std::from_chars(argv[1], end, process_id);
    if (parsed.ec != std::errc{} || parsed.ptr != end || process_id == 0 || process_id == GetCurrentProcessId())
        return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
    if (!process) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 3;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitialize(process, nullptr, TRUE)) {
        std::cerr << "SymInitialize failed: " << GetLastError() << '\n';
        CloseHandle(process);
        return 4;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "CreateToolhelp32Snapshot failed: " << GetLastError() << '\n';
        SymCleanup(process);
        CloseHandle(process);
        return 5;
    }
    bool captured = false;
    THREADENTRY32 entry{sizeof(THREADENTRY32)};
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != process_id)
                continue;
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE,
                                       entry.th32ThreadID);
            if (!thread)
                continue;
            if (SuspendThread(thread) == DWORD(-1)) {
                CloseHandle(thread);
                continue;
            }
            std::vector<DWORD64> frames;
            {
                ResumeThreadOnExit resume{thread};
                CONTEXT context{};
                context.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(thread, &context)) {
                    STACKFRAME64 frame{};
#if defined(_M_X64)
                    constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
                    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
                    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
                    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
#elif defined(_M_IX86)
                    constexpr DWORD machine = IMAGE_FILE_MACHINE_I386;
                    frame.AddrPC = {context.Eip, 0, AddrModeFlat};
                    frame.AddrStack = {context.Esp, 0, AddrModeFlat};
                    frame.AddrFrame = {context.Ebp, 0, AddrModeFlat};
#elif defined(_M_ARM64)
                    constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
                    frame.AddrPC = {context.Pc, 0, AddrModeFlat};
                    frame.AddrStack = {context.Sp, 0, AddrModeFlat};
                    frame.AddrFrame = {context.Fp, 0, AddrModeFlat};
#else
#error Unsupported Windows stack context
#endif
                    frames.push_back(frame.AddrPC.Offset);
                    for (int n = 0; n < 40; ++n) {
                        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                                         SymGetModuleBase64, nullptr))
                            break;
                        frames.push_back(frame.AddrPC.Offset);
                    }
                }
            }
            CloseHandle(thread);
            captured = captured || !frames.empty();
            std::cout << "THREAD " << entry.th32ThreadID << "\n";
            for (const auto address : frames) {
                alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
                auto symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = MAX_SYM_NAME;
                DWORD64 displacement = 0;
                const auto base = SymGetModuleBase64(process, address);
                std::cout << std::hex << address << " base=" << base << " offset=" << address - base;
                IMAGEHLP_MODULE64 module{};
                module.SizeOfStruct = sizeof(module);
                if (SymGetModuleInfo64(process, address, &module))
                    std::cout << " module=" << module.ModuleName;
                if (SymFromAddr(process, address, &displacement, symbol))
                    std::cout << " " << symbol->Name << "+" << displacement;
                std::cout << "\n" << std::dec;
            }
            std::cout << std::flush;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    SymCleanup(process);
    CloseHandle(process);
    return captured ? 0 : 6;
}
