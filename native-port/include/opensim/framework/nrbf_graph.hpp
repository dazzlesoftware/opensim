// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opensim::framework {
struct NrbfNode {
    int kind = 0;
    std::uint64_t bits = 0;
    std::string text, library;
    std::shared_ptr<std::vector<std::uint8_t>> bytes;
    std::vector<std::string> names;
    std::vector<std::shared_ptr<NrbfNode>> values;

    std::shared_ptr<NrbfNode> get(const std::string &name) const {
        const auto found = std::find(names.begin(), names.end(), name);
        return found == names.end() ? nullptr : values.at(static_cast<std::size_t>(found - names.begin()));
    }
};

// Structural NRBF reader shared with AssetBase. It does not instantiate CLR
// types. Callers project and validate their own named record schemas.
struct NrbfGraph {
    std::shared_ptr<NrbfNode> root;
    std::map<int, std::shared_ptr<NrbfNode>> objects;
    static std::optional<NrbfGraph> decode(std::span<const std::uint8_t>);
    std::shared_ptr<NrbfNode> resolve(std::shared_ptr<NrbfNode>) const;
    std::shared_ptr<NrbfNode> field(const std::shared_ptr<NrbfNode> &, const std::string &) const;
};
} // namespace opensim::framework
