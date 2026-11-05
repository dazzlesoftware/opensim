// Source-derived .NET8 CoreCLR Environment + PAL process environment.
// .NET process-environment behavior: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <mutex>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
namespace opensim::framework {
RuntimeEnvironmentText runtime_environment_empty_text() {
    return runtime_empty_text();
}
RuntimeEnvironmentError::RuntimeEnvironmentError(std::string t, std::int32_t h, std::u16string m,
                                                 std::optional<std::u16string> p)
    : runtime_error(utf8(m)), source_type(std::move(t)), hresult(h), message(std::move(m)),
      parameter(std::move(p)) {
}
namespace {
using Text = RuntimeEnvironmentText;
Text text(std::u16string value) {
    return value.empty() ? runtime_environment_empty_text()
                         : std::make_shared<const std::u16string>(std::move(value));
}
[[noreturn]] void env_error(std::string type, std::uint32_t h, std::u16string message,
                            std::optional<std::u16string> p = {}) {
    throw RuntimeEnvironmentError(std::move(type), std::bit_cast<std::int32_t>(h), std::move(message),
                                  std::move(p));
}
void validate_variable(const Text& name) {
    if (!name)
        env_error("System.ArgumentNullException", 0x80004003u,
                  u"Value cannot be null. (Parameter 'variable')", u"variable");
    if (name->empty())
        env_error("System.ArgumentException", 0x80070057u,
                  u"The value cannot be an empty string. (Parameter 'variable')", u"variable");
    if ((*name)[0] == u'\0')
        env_error("System.ArgumentException", 0x80070057u,
                  u"The first char in the string is the null character. (Parameter 'variable')", u"variable");
    if (name->find(u'=') != name->npos)
        env_error("System.ArgumentException", 0x80070057u,
                  u"Environment variable name cannot contain equal character. (Parameter 'variable')",
                  u"variable");
}
#ifndef _WIN32
// CoreCLR uses the PAL Windows-compatible environment layer on Unix, not
// Environment.Variables.Unix.cs (that shared file excludes FeatureCoreCLR).
// PAL_Initialize snapshots the byte environment before managed entry; later
// managed mutations affect only that private snapshot, with UTF8 W/A conversion.
struct UnixEnvironment {
    std::mutex mutex;
    std::vector<std::string> entries;
    UnixEnvironment() {
#ifdef __APPLE__
        auto source = *_NSGetEnviron();
#else
        auto source = environ;
#endif
        if (source)
            for (auto p = source; *p; ++p)
                entries.emplace_back(*p);
    }
};
UnixEnvironment unix_environment_state;
UnixEnvironment& unix_environment() {
    return unix_environment_state;
}
// minipal/utf8.c consumes the offending second continuation byte before
// rejecting an overlong or surrogate prefix, unlike System.Text.UTF8Encoding.
std::u16string pal_utf16(std::string_view bytes) {
    std::u16string result;
    for (std::size_t i = 0; i < bytes.size();) {
        auto first = static_cast<unsigned char>(bytes[i++]);
        if (first < 0x80) {
            result += first;
            continue;
        }
        int count = first >= 0xc2 && first <= 0xdf   ? 2
                    : first >= 0xe0 && first <= 0xef ? 3
                    : first >= 0xf0 && first <= 0xf4 ? 4
                                                     : 0;
        if (!count) {
            result += u'\ufffd';
            continue;
        }
        std::uint32_t scalar = first & ((1u << (7 - count)) - 1);
        bool bad = false;
        for (int n = 1; n < count; ++n) {
            if (i == bytes.size() || (static_cast<unsigned char>(bytes[i]) & 0xc0) != 0x80) {
                bad = true;
                break;
            }
            auto next = static_cast<unsigned char>(bytes[i++]);
            scalar = (scalar << 6) | (next & 0x3f);
            if (n == 1 && ((count == 3 && (scalar < 0x20 || (scalar >= 0x360 && scalar <= 0x37f))) ||
                           (count == 4 && (scalar < 0x10 || scalar > 0x10f)))) {
                bad = true;
                break;
            }
        }
        if (bad) {
            result += u'\ufffd';
            continue;
        }
        if (scalar <= 0xffff)
            result += static_cast<char16_t>(scalar);
        else {
            scalar -= 0x10000;
            result += static_cast<char16_t>(0xd800 + (scalar >> 10));
            result += static_cast<char16_t>(0xdc00 + (scalar & 0x3ff));
        }
    }
    return result;
}
std::string environment_bytes(Text value) {
    auto result = utf8(*value);
    result.resize(result.find('\0') == result.npos ? result.size() : result.find('\0'));
    return result;
}

#endif
}
RuntimeEnvironmentText runtime_environment_get(Text variable) {
    if (!variable)
        env_error("System.ArgumentNullException", 0x80004003u,
                  u"Value cannot be null. (Parameter 'variable')", u"variable");
#ifdef _WIN32
    std::vector<wchar_t> buffer(128);
    DWORD length;
    while ((SetLastError(ERROR_SUCCESS),
            length = GetEnvironmentVariableW(reinterpret_cast<const wchar_t*>(variable->c_str()),
                                             buffer.data(), static_cast<DWORD>(buffer.size()))) >
           buffer.size())
        buffer.resize(std::max<std::size_t>(length, buffer.size() * 2));
    if (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        return {};
    return text(std::u16string(reinterpret_cast<const char16_t*>(buffer.data()), length));
#else
    auto name = environment_bytes(variable);
    if (name.empty() || name.find('=') != name.npos)
        return {};
    auto& state = unix_environment();
    std::lock_guard guard(state.mutex);
    for (auto& entry : state.entries)
        if (entry.compare(0, name.size(), name) == 0 &&
            (entry.size() == name.size() || entry[name.size()] == '='))
            return text(pal_utf16(entry.size() == name.size()
                                      ? std::string_view{}
                                      : std::string_view(entry).substr(name.size() + 1)));
    return {};

#endif
}
void runtime_environment_set(Text variable, Text value) {
    validate_variable(variable);
    if (!value || value->empty() || (*value)[0] == u'\0')
        value = {};
#ifdef _WIN32
    SetLastError(ERROR_SUCCESS);
    if (!SetEnvironmentVariableW(reinterpret_cast<const wchar_t*>(variable->c_str()),
                                 value ? reinterpret_cast<const wchar_t*>(value->c_str()) : nullptr)) {
        auto code = GetLastError();
        if (code == ERROR_ENVVAR_NOT_FOUND)
            return;
        if (code == ERROR_FILENAME_EXCED_RANGE)
            env_error("System.ArgumentException", 0x80070057u,
                      u"Environment variable name or value is too long.");
        wchar_t* buffer = nullptr;
        auto n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::u16string message;
        if (n) {
            message.assign(reinterpret_cast<char16_t*>(buffer), n);
            LocalFree(buffer);
            while (!message.empty() &&
                   (message.back() == u'\r' || message.back() == u'\n' || message.back() == u' '))
                message.pop_back();
        }
        if (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_NO_SYSTEM_RESOURCES)
            env_error("System.OutOfMemoryException", 0x8007000eu, std::move(message));
        env_error("System.ArgumentException", 0x80070057u, std::move(message));
    }
#else
    auto name = environment_bytes(variable);
    auto& state = unix_environment();
    std::lock_guard guard(state.mutex);
    if (!value) {
        for (std::size_t i = 0; i < state.entries.size(); ++i) {
            auto split = state.entries[i].find('=');
            if (split == state.entries[i].npos)
                split = state.entries[i].size();
            if (split == name.size() && state.entries[i].compare(0, split, name) == 0) {
                state.entries[i] = std::move(state.entries.back());
                state.entries.pop_back();
            }
        }
    } else {
        auto entry = name + "=" + environment_bytes(value);
        for (auto& old : state.entries) {
            auto split = old.find('=');
            if (split == old.npos)
                split = old.size();
            if (split == name.size() && old.compare(0, split, name) == 0) {
                old = std::move(entry);
                return;
            }
        }
        state.entries.push_back(std::move(entry));
    }

#endif
}
}
