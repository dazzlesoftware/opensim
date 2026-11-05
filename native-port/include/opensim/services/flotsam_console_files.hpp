// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/flotsam_cache_console.hpp"
namespace opensim::services {
class FlotsamConsoleFilesError : public std::runtime_error {
public:
    enum class Kind {
        ArgumentNull,
        Argument,
        ArgumentOutOfRange,
        PathTooLong,
        Unauthorized,
        DirectoryNotFound,
        FileNotFound,
        Io,
        OperationCanceled
    };
    Kind kind;
    std::uint32_t code;
    std::optional<std::string> parameter;
    std::string message;
    FlotsamConsoleFilesError(Kind, const std::string&, std::uint32_t code,
                             std::optional<std::string> parameter = {});
    const char* source_type() const noexcept;
    std::uint32_t hresult() const noexcept {
        return code;
    }
};
// Source Directory.GetFiles(root,"*.fac"), preserving native enumeration order,
// long-name matching and the original root's spelling in returned paths.
std::vector<std::optional<std::string>> native_flotsam_status_files(const std::optional<std::string>& root);
// Source File.GetLastWriteTime, including local DateTime kind/ambiguous DST bit
// and the missing-file1601 sentinel. Formatting remains a required dependency.
framework::AssetDateTime native_flotsam_last_write(const std::optional<std::string>& path);
framework::AssetDateTime
native_flotsam_last_write(const std::optional<std::string>& path,
                          const std::shared_ptr<framework::NativeInvariantDateTimeContext>&);
// Binds only file enumeration and timestamps; does not install a date parser,
// formatter, scene gather, task executor, cleanup controller or file-cache host.
void bind_flotsam_console_files(FlotsamConsoleBackend&,
                                std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime = {});
}
