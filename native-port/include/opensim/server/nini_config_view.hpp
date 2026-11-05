// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_config.hpp"
#include <filesystem>
#include <map>
namespace opensim::server {
// UTF8 compatibility projection over the retained Nini graph. Copies share the
// same source, as source IConfigSource references do. Native handles retain raw
// UTF16 values/errors and the owning arena; maps never become authoritative.
class NiniConfigView {
    std::shared_ptr<framework::NiniConfigSourceBase> source_;

public:
    NiniConfigView();
    explicit NiniConfigView(std::shared_ptr<framework::NiniConfigSourceBase>);
    // Plain IniConfigSource load: no includes, aliases, merging or expansion.
    static NiniConfigView load_raw(const std::filesystem::path&);
    std::shared_ptr<framework::NiniConfigSourceBase> native_source() const {
        return source_;
    }
    std::shared_ptr<framework::NiniConfig> native_section(const std::string&) const;
    bool has_section(const std::string&) const;
    bool contains(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    std::int32_t integer(const std::string& section, const std::string& key, std::int32_t fallback) const;
    bool boolean(const std::string& section, const std::string& key, bool fallback) const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    std::vector<std::pair<std::string, std::string>> ordered_section(const std::string&) const;
    std::map<std::string, std::string> section(const std::string&) const;
    // Explicit startup phase; ordinary Get/Set never implicitly expands values.
    void replace_key_values();
};
}
