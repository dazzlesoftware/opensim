// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/filesystem_asset_loader.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
namespace opensim::services {
namespace {
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::invalid_argument("Null filesystem asset loader reference");
    return *value;
}
std::string path_text(const std::filesystem::path &path) {
    auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
std::string get(const AssetXmlConfig &c, const std::string &key, std::string fallback) {
    auto found = c.find(key);
    return found == c.end() ? fallback : found->second;
}
}
bool AssetLoaderFiles::exists(const std::string &path) {
    if (path.empty() || path.find('\0') != path.npos)
        return false;
    std::error_code error;
    return std::filesystem::is_regular_file(framework::utf8_path(path), error);
}
std::vector<AssetXmlConfig> AssetLoaderFiles::configurations(const std::string &path) {
    std::ifstream file(framework::utf8_path(path), std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open asset XML file");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (file.bad())
        throw std::runtime_error("Cannot read asset XML file");
    const auto arena = std::make_shared<framework::NiniConfigArena>(
        framework::native_nini_invariant_config_backend());
    const auto uri_reader = std::make_shared<framework::NiniXmlUriReaderBackend>();
    uri_reader->get_temp_resolver = []() -> std::shared_ptr<framework::NiniXmlUriReaderResolver> {
        throw std::logic_error("Asset manifest XML is loaded from an open local file");
    };
    const auto backend = framework::native_nini_xml_config_backend(
        framework::native_xml_document_io(uri_reader));
    std::shared_ptr<framework::NiniXmlConfigSource> source;
    try {
        const auto data = std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        auto reader = framework::NativeNiniXmlReader::from_bytes(
            data, framework::NiniXmlReaderSettings::legacy_document());
        source = framework::NiniXmlConfigSource::create(arena, backend, std::move(reader));
    } catch (const framework::NiniXmlProviderError& error) {
        if (error.source_type != "System.Xml.XmlException")
            throw;
        throw AssetXmlError(error.what());
    }
    std::vector<AssetXmlConfig> result;
    const auto configs = source->configs();
    for (std::int32_t index = 0; index < configs->count(); ++index) {
        const auto section = configs->at(index);
        AssetXmlConfig config;
        for (const auto& key : section->get_keys())
            config.emplace(framework::utf8(*key), framework::utf8(*section->get(key)));
        result.push_back(std::move(config));
    }
    return result;
}
std::string AssetLoaderFiles::full_path(const std::string &path) {
    if (path.empty() || path.find('\0') != path.npos)
        throw std::invalid_argument("Invalid asset path");
    return path_text(std::filesystem::absolute(framework::utf8_path(path)).lexically_normal());
}
std::string AssetLoaderFiles::directory(const std::string &path) {
    return path_text(framework::utf8_path(path).parent_path());
}
std::string AssetLoaderFiles::combine(const std::string &directory, const std::string &path) {
    if (path.empty())
        return directory;
    return path_text(framework::utf8_path(directory) / framework::utf8_path(path));
}
std::int64_t AssetLoaderFiles::length(const std::string &path) {
    if (std::filesystem::is_directory(framework::utf8_path(path)))
        throw std::runtime_error("Asset data path is a directory");
    if (path.find('\0') != path.npos)
        throw std::invalid_argument("Invalid asset path");
    return static_cast<int64_t>(std::filesystem::file_size(framework::utf8_path(path)));
}
framework::AssetBytes AssetLoaderFiles::read_bytes(const std::string &path, std::int32_t count) {
    if (count < 0)
        throw std::out_of_range("Negative asset byte count");
    std::ifstream file(framework::utf8_path(path), std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open asset data");
    auto result = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(count));
    if (count) {
        file.read(reinterpret_cast<char *>(result->data()), count);
        if (file.bad())
            throw std::runtime_error("Cannot read asset data");
        result->resize(static_cast<size_t>(file.gcount()));
    }
    return result;
}
FilesystemAssetLoader::FilesystemAssetLoader(std::shared_ptr<AssetLoaderFiles> files)
    : files_(std::move(files)) {
    required(files_);
}
AssetService::Asset FilesystemAssetLoader::create_asset(const std::string &id, const std::string &name,
                                                        const std::optional<std::string> &path,
                                                        std::int8_t type) {
    const auto asset_id = framework::construct_sdk_uuid(framework::utf16_replacing(id));
    auto asset = std::make_shared<framework::AssetBase>(asset_id, name, type,
                                                        std::string("11111111-1111-0000-0000-000100bba000"));
    if (path && !path->empty())
        load_asset(asset, *path);
    else {
        asset->data = std::make_shared<std::vector<uint8_t>>();
        files_->info("[ASSETS]: Instantiated: [" + name + "]");
    }
    return asset;
}
void FilesystemAssetLoader::load_asset(AssetService::Asset asset, const std::string &path) {
    auto size = files_->length(path);
    if (files_->exists(path)) {
        auto count = std::bit_cast<int32_t>(static_cast<uint32_t>(size));
        auto data = files_->read_bytes(path, count);
        required(asset).data = std::move(data);
    } else
        files_->error("[ASSETS]: file: [" + path + "] not found !");
}
void FilesystemAssetLoader::load_set(const std::string &path, std::vector<AssetService::Asset> &assets) {
    if (files_->exists(path)) {
        try {
            auto configs = files_->configurations(path);
            auto directory = files_->directory(path);
            for (auto &config : configs) {
                auto random = files_->random_uuid();
                auto id = get(config, "assetID", random.str());
                auto name = get(config, "name", "");
                int32_t number = 0;
                auto found = config.find("assetType");
                if (found != config.end())
                    number = framework::invariant_convert_to_int32(framework::utf16_replacing(found->second));
                auto type = std::bit_cast<int8_t>(static_cast<uint8_t>(number));
                auto file = get(config, "fileName", "");
                auto asset =
                    create_asset(id, name,
                                 file.empty() ? std::nullopt
                                              : std::optional<std::string>(files_->combine(directory, file)),
                                 type);
                asset->type(type);
                assets.push_back(std::move(asset));
            }
        } catch (const AssetXmlError &e) {
            files_->error("[ASSETS]: Error loading " + path + " : " + e.what());
        }
    } else
        files_->error("[ASSETS]: Asset set file " + path + " does not exist!");
}
void FilesystemAssetLoader::for_each(const std::string &arguments,
                                     std::function<void(AssetService::Asset)> action) {
    std::vector<AssetService::Asset> assets;
    if (files_->exists(arguments)) {
        std::string set = "ERROR", root = "";
        try {
            auto configs = files_->configurations(arguments);
            root = files_->directory(files_->full_path(arguments));
            for (auto &config : configs) {
                set = get(config, "file", "");
                load_set(files_->combine(root, set), assets);
            }
        } catch (const AssetXmlError &e) {
            files_->error("[ASSETS]: Error loading " + set + " : " + e.what());
        }
    } else
        files_->error("[ASSETS]: Asset set control file " + arguments + " does not exist! No assets loaded.");
    if (!action)
        throw std::invalid_argument("Null asset loader callback");
    for (auto asset : assets)
        action(std::move(asset));
}
}
