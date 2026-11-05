// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
namespace opensim::framework {
enum class FullPathFailure {
    ArgumentNull,
    Argument,
    PathTooLong,
    Io,
    MissingFile,
    MissingDirectory,
    Unauthorized,
    OperationCanceled,
    ArgumentOutOfRange
};
// Symbolic errno categories keep the Unix provider testable on every host.
// Successful bytes contain the native getcwd payload before UTF8 decoding.
enum class UnixPathError {
    Success,
    NoEntry,
    NotDirectory,
    AccessDenied,
    BadDescriptor,
    NotPermitted,
    NameTooLong,
    WouldBlock,
    Canceled,
    FileTooLarge,
    AlreadyExists,
    Other
};
struct UnixCwdResult {
    std::string bytes;
    UnixPathError error = UnixPathError::Success;
    std::string system_message;
    int native_code = 0;
};
using UnixCwdProvider = std::function<UnixCwdResult()>;
class FullPathError : public std::runtime_error {
  public:
    FullPathFailure kind;
    std::optional<std::string> parameter;
    // Present only when normalization failed through the Windows API.
    std::optional<std::uint32_t> win32_code;
    FullPathError(FullPathFailure kind, const std::string& text, std::optional<std::string> parameter = {},
                  std::optional<std::uint32_t> win32_code = {})
        : runtime_error(text), kind(kind), parameter(std::move(parameter)), win32_code(win32_code) {}
};
struct FullPathBackend {
    std::function<std::u16string(const std::u16string&)> normalize;
    std::function<std::u16string(const std::u16string&)> expand_short;
};
// UTF8 boundary for Path.GetFullPath(string). No environment or tilde expansion.
// Backend errors propagate; validation precedes provider calls. Extended Windows
// device paths bypass normalization, as in the original runtime.
std::string get_full_path(const std::optional<std::string>&, std::shared_ptr<FullPathBackend> backend = {});
std::shared_ptr<FullPathBackend> native_full_path_backend();
// Portable Unix Path.GetFullPath core. Absolute inputs never call the cwd
// provider. Relative inputs replacement-decode its null-terminated byte payload.
std::string get_full_path_unix(const std::optional<std::string>&, UnixCwdProvider);
}
