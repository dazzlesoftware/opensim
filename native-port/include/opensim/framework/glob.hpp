// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/directory_entries.hpp"
#include <functional>
#include <optional>
namespace opensim::framework {
struct GlobBackend {
    char16_t volume_separator = u':', directory_separator = u'\\', alternate_separator = u'/';
    std::function<std::vector<std::u16string>(const std::u16string&, const std::u16string&)> directories,
        files;
    std::function<std::u16string(const std::u16string&, const std::u16string&)> combine;
};
class GlobNullInputError : public std::runtime_error {
  public:
    GlobNullInputError() : runtime_error("Object reference not set to an instance of an object.") {}
};
// Original Util.Glob control flow, including its root-based relative inputs,
// directory-before-file calls, stable enumeration order and repeated results.
std::vector<std::u16string> glob(const std::optional<std::u16string>& path, const GlobBackend&);
GlobBackend native_glob_backend(std::shared_ptr<DirectoryEntriesBackend> backend = {});
std::vector<std::u16string> glob(const std::optional<std::u16string>& path);
}
