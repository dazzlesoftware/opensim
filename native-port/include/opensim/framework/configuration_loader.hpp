// Copyright (c) Contributors, http://opensimulator.org/; BSD: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/glob.hpp"
#include <exception>
namespace opensim::framework {
struct RegionLoaderConfigSettings {
    NiniWriterText physics_engine, mesh_engine_name, clientstack_dll, libraries_xml_file;
};
struct RegionLoaderConfigSource {
    std::shared_ptr<NiniConfigSourceBase> source;
};
enum class RegionLoaderLogCall { Message, Format };
enum class RegionLoaderLogLevel { Info, Warn, Error, Fatal };
struct RegionApplicationConfigurationState {
    NiniWriterText ini_file_path = nini_writer_text(u"");
};
struct RegionConfigurationBackend {
    std::shared_ptr<RegionApplicationConfigurationState> application;
    std::function<NiniWriterText()> config_directory;
    std::function<NiniWriterText(NiniWriterText, NiniWriterText)> combine;
    std::function<NiniWriterText(NiniWriterText)> full_path, extension;
    std::function<bool(NiniWriterText)> file_exists, directory_exists, is_http_uri;
    std::function<std::vector<NiniWriterText>(NiniWriterText)> directory_files, glob;
    std::function<bool(NiniWriterText, NiniWriterText)> starts_with_current;
    std::function<std::shared_ptr<NiniConfigSourceBase>()> empty_ini_source;
    std::function<std::shared_ptr<NiniConfigSourceBase>(NiniWriterText)> ini_file_source;
    // Must implement XmlReader.Create(uri,DtdProcessing.Ignore)+actual XmlConfigSource.
    std::function<std::shared_ptr<NiniConfigSourceBase>(NiniWriterText)> xml_uri_source;
    std::function<void(std::shared_ptr<NiniConfigSourceBase>)> merge_environment, replace_key_values;
    std::function<std::shared_ptr<void>()> create_network_info;
    std::function<void(std::shared_ptr<void>, std::shared_ptr<NiniConfigSourceBase>)> load_network_info;
    // Preserve Message vs Format even with zero args (URI braces are literal in Fatal).
    std::function<void(RegionLoaderLogLevel, NiniWriterText, std::vector<NiniWriterText>, RegionLoaderLogCall)>
        log;
    // Production exit must not return; callbacks that return retain original control flow.
    std::function<void(int)> exit;
    std::function<NiniWriterText(std::exception_ptr)> exception_message;
};
class RegionConfigurationLoader {
    std::shared_ptr<RegionConfigurationBackend> backend_;

protected:
    std::shared_ptr<RegionLoaderConfigSettings> config_settings_;
    std::shared_ptr<void> network_info_;
    std::shared_ptr<RegionLoaderConfigSource> config_;

public:
    explicit RegionConfigurationLoader(std::shared_ptr<RegionConfigurationBackend>);
    virtual ~RegionConfigurationLoader() = default;
    NiniWriterText ini_file_path() const {
        return backend_->application->ini_file_path;
    }
    std::shared_ptr<RegionLoaderConfigSource> config() const {
        return config_;
    }
    std::shared_ptr<RegionLoaderConfigSettings> config_settings() const {
        return config_settings_;
    }
    std::shared_ptr<void> network_info() const {
        return network_info_;
    }
    std::shared_ptr<RegionLoaderConfigSource>
    load_config_settings(std::shared_ptr<NiniConfigSourceBase> argv_source,
                         std::shared_ptr<NiniConfigSourceBase> unused_env_source,
                         std::shared_ptr<RegionLoaderConfigSettings>&, std::shared_ptr<void>&);
    void add_includes(std::shared_ptr<RegionLoaderConfigSource>, std::vector<NiniWriterText>&);
    bool is_uri(NiniWriterText);
    bool read_config(std::shared_ptr<RegionLoaderConfigSource>, NiniWriterText);
    virtual void read_config_settings();
};
// Binds only consumed native local file/path/glob/INI operations. Culture, URI,
// XML, environment, log/exit and network callbacks remain explicit requirements.
RegionConfigurationBackend
native_local_region_configuration_backend(std::shared_ptr<NiniConfigArena>,
                                          std::shared_ptr<RegionApplicationConfigurationState>,
                                          std::shared_ptr<DirectoryEntriesBackend> directories = {});
}
