// Consumed .NET8 Directory.GetFiles/GetDirectories default compatibility APIs.
// MIT source license: native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/file_system_name.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
namespace opensim::framework {
using DirectoryEntriesText = std::shared_ptr<const std::u16string>;
enum class DirectoryEntriesPlatform { Windows, Linux, MacOS };
class DirectoryEntriesError : public std::runtime_error {
public:
    DirectoryEntriesError(std::string source_type, std::int32_t hresult, std::u16string message,
                          std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::int32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter;
};
struct DirectoryEntry {
    std::u16string name;
    bool is_directory = false;
};
class DirectoryEntriesCursor {
public:
    virtual ~DirectoryEntriesCursor() = default;
    virtual std::optional<DirectoryEntry> next() = 0;
    virtual void close() = 0;
};
struct DirectoryEntriesBackend {
    DirectoryEntriesPlatform platform;
    std::function<DirectoryEntriesText(DirectoryEntriesText)> full_path;
    std::function<std::shared_ptr<DirectoryEntriesCursor>(DirectoryEntriesText)> open;
    file_system_name::CasingMode casing = file_system_name::CasingMode::Normal;
    std::optional<file_system_name::CasingBackend> case_backend;
};
struct NormalizedDirectorySearch {
    DirectoryEntriesText directory, expression;
    bool unchanged_directory = true;
};
// Exposed source NormalizeInputs closure for provider/testing and Glob binding.
NormalizedDirectorySearch normalize_directory_search(DirectoryEntriesText directory,
                                                     DirectoryEntriesText expression,
                                                     DirectoryEntriesPlatform);
std::shared_ptr<DirectoryEntriesBackend> native_directory_entries_backend();
std::vector<DirectoryEntriesText> directory_get_files(DirectoryEntriesText path, DirectoryEntriesText pattern,
                                                      std::shared_ptr<DirectoryEntriesBackend> backend = {});
std::vector<DirectoryEntriesText>
directory_get_directories(DirectoryEntriesText path, DirectoryEntriesText pattern,
                          std::shared_ptr<DirectoryEntriesBackend> backend = {});
std::vector<DirectoryEntriesText> directory_get_files(DirectoryEntriesText path);
std::vector<DirectoryEntriesText> directory_get_directories(DirectoryEntriesText path);
}
