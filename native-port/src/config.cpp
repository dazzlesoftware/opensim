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
// INI document loading derives from Nini, copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#include "opensim/server/config.hpp"
#include "opensim/server/config_xml_startup.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/native_configuration_loader.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/config_uri.hpp"
#include <iostream>
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/sqlite_connection.hpp"
namespace opensim::server {
namespace {
using Text = framework::NiniWriterText;
Text startup_text(std::u16string value) {
    return framework::nini_writer_text(std::move(value));
}
std::shared_ptr<framework::RegionApplicationConfigurationState> default_application_state() {
    static const auto state = std::make_shared<framework::RegionApplicationConfigurationState>();
    return state;
}
void startup_log(framework::RegionLoaderLogLevel, Text format, std::vector<Text> args,
                 framework::RegionLoaderLogCall call) {
    auto value = format ? *format : std::u16string{};
    if (call == framework::RegionLoaderLogCall::Format) {
        // ConfigurationLoader's consumed formats have positional arguments and no
        // alignment/format specifiers; inserted user paths are never parsed as formats.
        std::u16string rendered;
        for (std::size_t n = 0; n < value.size();) {
            bool found = false;
            for (std::size_t i = 0; i < args.size(); ++i) {
                auto number = std::to_string(i);
                std::u16string token = u"{";
                token.append(number.begin(), number.end());
                token += u'}';
                if (value.compare(n, token.size(), token) == 0) {
                    if (args[i])
                        rendered += *args[i];
                    n += token.size();
                    found = true;
                    break;
                }
            }
            if (!found)
                rendered += value[n++];
        }
        value = std::move(rendered);
    }
    std::clog << framework::utf8(value) << '\n' << std::flush;
}
}
IniConfig IniConfig::load_startup(const std::filesystem::path& path, ConfigurationStartupOptions options) {
    auto arena =
        std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto paths = framework::native_directory_entries_backend();
    auto selected = startup_text(path.u16string());
    std::filesystem::path directory;
    if (framework::is_http_configuration_uri(selected))
        directory = std::filesystem::current_path();
    else {
        selected = paths->full_path(selected);
        directory = std::filesystem::path(*selected).parent_path();
    }
    auto app = options.application ? std::move(options.application) : default_application_state();
    auto backend = std::make_shared<framework::RegionConfigurationBackend>(
        framework::native_local_region_configuration_backend(arena, app, paths));
    framework::bind_native_startup_configuration_services(*backend);
    auto config_dir = startup_text(directory.u16string());
    backend->config_directory = [config_dir] { return config_dir; };
    backend->log = options.log ? std::move(options.log) : decltype(backend->log)(startup_log);
    backend->exit = options.exit
                        ? std::move(options.exit)
                        : decltype(backend->exit)([](int code) { throw ConfigurationStartupExit(code); });
    if (options.xml_uri_source)
        backend->xml_uri_source = std::move(options.xml_uri_source);
    else if (options.xml_http_services)
        backend->xml_uri_source = native_startup_xml_source(arena, *options.xml_http_services,
                                                            std::move(options.xml_resolver_policy));
    else
        backend->xml_uri_source = [](Text) -> std::shared_ptr<framework::NiniConfigSourceBase> {
            throw framework::NiniConfigError(
                framework::NiniConfigError::Kind::Exception,
                u"Remote XML configuration is not available in the native startup profile");
        };
    auto argv = arena->create_source(startup_text(u"NativeProfileStartupSource"));
    for (auto value : {u"On", u"True", u"Yes"})
        argv->alias()->add_alias(startup_text(value), true);
    for (auto value : {u"Off", u"False", u"No"})
        argv->alias()->add_alias(startup_text(value), false);
    auto startup = argv->add_config(startup_text(u"Startup"));
    startup->set_text(startup_text(u"inimaster"), std::move(options.inimaster));
    startup->set_text(startup_text(u"inifile"), selected);
    startup->set_text(startup_text(u"inidirectory"), std::move(options.inidirectory));
    if (options.argv_overrides)
        argv->merge(options.argv_overrides);
    framework::RegionConfigurationLoader loader(backend);
    std::shared_ptr<framework::RegionLoaderConfigSettings> settings;
    std::shared_ptr<void> network;
    auto result = loader.load_config_settings(argv, {}, settings, network);
    // The loader has already completed environment expansion and final argv
    // replacement. Keep its graph; do not expand again or flatten config owners.
    return from_native_source(result->source, std::move(directory));
}
IniConfig IniConfig::from_native_source(std::shared_ptr<framework::NiniConfigSourceBase> source,
                                        std::filesystem::path directory) {
    IniConfig result(NiniConfigView(std::move(source)));
    result.directory = std::move(directory);
    return result;
}
bool IniConfig::contains(const std::string& name, const std::string& key) const {
    return view_.contains(name, key);
}
void IniConfig::set(const std::string& name, const std::string& key, std::string value) {
    view_.set(name, key, value);
}
IniConfig IniConfig::load(const std::filesystem::path& path) {
    IniConfig result(NiniConfigView::load_raw(path));
    result.directory = std::filesystem::absolute(path).parent_path();
    // Load the complete source before checking effective keys. Earlier syntax
    // failures win; a later replacement section can remove an earlier include.
    auto configs = result.native_source()->configs();
    for (std::int32_t i = 0; i < configs->count(); ++i)
        for (const auto& key : configs->at(i)->get_keys())
            if (framework::invariant_starts_with(framework::utf8(*key), "Include-"))
                throw std::runtime_error(
                    "INI Include-* loading is not ported yet; use an explicit native configuration");
    // ConfigurationLoader performs this after all startup merges. This explicit
    // single-file host entry point completes that phase once before module use.
    result.replace_key_values();
    return result;
}
std::string IniConfig::get(const std::string& name, const std::string& key,
                           const std::string& fallback) const {
    return view_.get(name, key, fallback);
}
int IniConfig::integer(const std::string& name, const std::string& key, int fallback) const {
    return view_.integer(name, key, fallback);
}
bool IniConfig::boolean(const std::string& name, const std::string& key, bool fallback) const {
    return view_.boolean(name, key, fallback);
}
std::map<std::string, std::string> IniConfig::section(const std::string& name) const {
    return view_.section(name);
}
std::vector<std::pair<std::string, std::string>> IniConfig::ordered_section(const std::string& name) const {
    return view_.ordered_section(name);
}
std::string database_path(const IniConfig& config, const std::string& service,
                          const std::string& native_key) {
    auto path = config.get(service, "ConnectionString", config.get("NativeServer", native_key, "native.db"));
    if (path.find('=') != std::string::npos)
        path = services::sqlite_connection_path(services::parse_sqlite_connection(path));
    if (path == ":memory:")
        return path;
    auto resolved = framework::utf8_path(path);
    if (resolved.is_relative())
        resolved = config.directory / resolved;
    auto utf8 = resolved.lexically_normal().u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}
}
