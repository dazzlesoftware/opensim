// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#endif

namespace opensim::network::detail {
inline std::string circuit_process_image(std::uint32_t process_id) noexcept {
#ifdef _WIN32
    try {
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
        if (!process)
            return {};
        char path[1024]{};
        DWORD size = sizeof(path);
        const bool found = QueryFullProcessImageNameA(process, 0, path, &size) != FALSE;
        CloseHandle(process);
        return found ? std::string(path, size) : std::string{};
    } catch (...) {
    }
#else
    (void)process_id;
#endif
    return {};
}

inline std::uint32_t circuit_process_parent(std::uint32_t process_id) noexcept {
#ifdef _WIN32
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD parent = 0;
    if (Process32First(snapshot, &entry))
        do {
            if (entry.th32ProcessID == process_id) {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
    return parent;
#else
    (void)process_id;
    return 0;
#endif
}

inline std::uint32_t circuit_udp_owner(std::uint16_t port) noexcept {
#ifdef _WIN32
    try {
        DWORD size = 0;
        if (GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != ERROR_INSUFFICIENT_BUFFER)
            return 0;
        std::vector<unsigned char> buffer(size);
        if (GetExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != NO_ERROR)
            return 0;
        const auto table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
            if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port)
                return table->table[i].dwOwningPid;
    } catch (...) {
    }
#else
    (void)port;
#endif
    return 0;
}

enum class CircuitTraceMode { Off, Live, Buffered };

inline CircuitTraceMode circuit_trace_mode() noexcept {
    static const auto mode = [] {
        const auto parse = [](std::string_view value) {
            return value == "1"          ? CircuitTraceMode::Live
                   : value == "buffered" ? CircuitTraceMode::Buffered
                                         : CircuitTraceMode::Off;
        };
#ifdef _WIN32
        char *value = nullptr;
        std::size_t length = 0;
        const bool found = _dupenv_s(&value, &length, "OPENSIM_TRACE_CIRCUITS") == 0 && value;
        const auto active = found ? parse(value) : CircuitTraceMode::Off;
        std::free(value);
        return active;
#else
        const auto value = std::getenv("OPENSIM_TRACE_CIRCUITS");
        return value ? parse(value) : CircuitTraceMode::Off;
#endif
    }();
    return mode;
}

inline bool circuit_trace_enabled() noexcept {
    return circuit_trace_mode() != CircuitTraceMode::Off;
}

inline std::mutex &circuit_trace_mutex() noexcept {
    static std::mutex output;
    return output;
}

// A steady clock keeps intervals useful even when the system clock changes.
// Initialize only on the first enabled event, while holding the output mutex.
inline auto circuit_trace_elapsed_us() noexcept {
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

class CircuitTraceBuffer {
  public:
    void append(std::string line) {
        if (lines_.size() == 4096) {
            lines_.pop_front();
            ++dropped_;
        }
        lines_.push_back(std::move(line));
    }

    ~CircuitTraceBuffer() noexcept {
        try {
            std::clog << "[NATIVE CIRCUIT BUFFER] retained=" << lines_.size() << " dropped=" << dropped_ << '\n';
            for (const auto &line : lines_)
                std::clog << line << '\n';
            std::clog.flush();
        } catch (...) {
        }
    }

  private:
    std::deque<std::string> lines_;
    std::size_t dropped_ = 0;
};

inline CircuitTraceBuffer &circuit_trace_buffer() {
    static CircuitTraceBuffer buffer;
    return buffer;
}

template <class... Details>
inline void circuit_trace(std::string_view phase, const framework::Uuid &agent, const Details &...details) noexcept {
    if (!circuit_trace_enabled())
        return;
    try {
        std::lock_guard lock(circuit_trace_mutex());
        const auto elapsed_us = circuit_trace_elapsed_us();
        if (circuit_trace_mode() == CircuitTraceMode::Buffered) {
            std::ostringstream line;
            line << "[NATIVE CIRCUIT] " << phase << " agent=" << agent.str() << " elapsed_us=" << elapsed_us << ' ';
            (line << ... << details);
            circuit_trace_buffer().append(line.str());
            return;
        }
        std::clog << "[NATIVE CIRCUIT] " << phase << " agent=" << agent.str() << " elapsed_us=" << elapsed_us << ' ';
        (std::clog << ... << details);
        std::clog << std::endl;
    } catch (...) {
        // Optional diagnostics must not change connection outcomes.
    }
}
} // namespace opensim::network::detail
