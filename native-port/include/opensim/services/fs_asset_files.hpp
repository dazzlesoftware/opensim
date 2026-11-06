// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace opensim::services {
class FsAssetFiles {
public:
    FsAssetFiles(std::filesystem::path base, std::filesystem::path spool, bool osgrid_layout);
    static std::string digest(std::string_view bytes);
    std::filesystem::path relative_path(std::string_view hash) const;
    std::optional<std::string> read(std::string_view hash) const;
    bool stage(std::string_view hash, std::string_view bytes) const;
    std::size_t flush() const;

private:
    std::filesystem::path spool_path(std::string_view hash) const;

    std::filesystem::path base_;
    std::filesystem::path spool_;
    bool osgrid_layout_;
};
}
