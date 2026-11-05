/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "opensim/server/nini_config_view.hpp"
#include "opensim/framework/configuration_loader.hpp"
#include <filesystem>
#include <map>
#include <string>
namespace opensim::framework {
struct ConfigXmlNativeHttpServices;
class NiniXmlResolverPolicy;
}
namespace opensim::server {
// Host-authored startup profile. Full original loader defaults remain in the
// framework API; this profile explicitly selects no master and original config directory scan.
struct ConfigurationStartupOptions {
    framework::NiniWriterText inimaster = framework::nini_writer_text(u"none");
    framework::NiniWriterText inidirectory = framework::nini_writer_text(u"config");
    std::shared_ptr<framework::NiniConfigSourceBase> argv_overrides;
    std::shared_ptr<framework::RegionApplicationConfigurationState> application;
    std::function<void(framework::RegionLoaderLogLevel, framework::NiniWriterText,
                       std::vector<framework::NiniWriterText>, framework::RegionLoaderLogCall)>
        log;
    std::function<void(int)> exit;
    std::function<std::shared_ptr<framework::NiniConfigSourceBase>(framework::NiniWriterText)> xml_uri_source;
    // Native URL composition when no custom xml_uri_source is supplied. Policy
    // providers remain explicit until default OS proxy/trust/auth handling is complete.
    std::shared_ptr<framework::ConfigXmlNativeHttpServices> xml_http_services;
    std::shared_ptr<framework::NiniXmlResolverPolicy> xml_resolver_policy;
};
class ConfigurationStartupExit : public std::runtime_error {
  public:
    explicit ConfigurationStartupExit(int value)
        : runtime_error("Configuration startup requested exit " + std::to_string(value)), code(value) {}
    int code;
};
class IniConfig {
  public:
    IniConfig() = default;
    // Retain the original graph, config ownership and aliases. Loader callers
    // decide when to perform the explicit startup replacement phase.
    static IniConfig from_native_source(std::shared_ptr<framework::NiniConfigSourceBase>,
                                        std::filesystem::path directory = {});
    bool has_section(const std::string& name) const {
        return view_.has_section(name);
    }
    bool contains(const std::string& section, const std::string& key) const;
    void set(const std::string& section, const std::string& key, std::string value);
    // Explicit single-file compatibility reader; include orchestration belongs to load_startup.
    static IniConfig load(const std::filesystem::path& path);
    static IniConfig load_startup(const std::filesystem::path&, ConfigurationStartupOptions options = {});
    std::string get(const std::string& section, const std::string& key,
                    const std::string& fallback = "") const;
    int integer(const std::string& section, const std::string& key, int fallback) const;
    bool boolean(const std::string& section, const std::string& key, bool fallback) const;
    std::map<std::string, std::string> section(const std::string& name) const;
    std::vector<std::pair<std::string, std::string>> ordered_section(const std::string& name) const;
    std::shared_ptr<framework::NiniConfigSourceBase> native_source() const {
        return view_.native_source();
    }
    std::shared_ptr<framework::NiniConfig> native_section(const std::string& name) const {
        return view_.native_section(name);
    }
    void replace_key_values() {
        view_.replace_key_values();
    }
    std::filesystem::path directory;

  private:
    explicit IniConfig(NiniConfigView view) : view_(std::move(view)) {}
    NiniConfigView view_;
};
std::string database_path(const IniConfig& config, const std::string& service, const std::string& native_key);
}
