// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/filesystem_asset_loader.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/services/user_account.hpp"
#include <pugixml.hpp>
#include <bit>
#include <filesystem>
#include <fstream>
#include <unordered_set>
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
    pugi::xml_document doc;
    auto parsed = doc.load_buffer(bytes.data(), bytes.size());
    if (!parsed)
        throw AssetXmlError(parsed.description());
    int roots = 0;
    for (auto node : doc.children())
        if (node.type() == pugi::node_element)
            ++roots;
    if (roots != 1)
        throw AssetXmlError("Multiple XML root elements");
    auto root = doc.document_element();
    if (std::string_view(root.name()) != "Nini")
        throw std::invalid_argument("Did not find Nini XML root node");
    std::vector<AssetXmlConfig> result;
    std::unordered_set<std::string> sections;
    for (auto section : root.children("Section")) {
        auto name = section.attribute("Name");
        if (!name)
            throw std::invalid_argument("Missing asset XML section name");
        bool first = sections.insert(name.value()).second;
        AssetXmlConfig config;
        for (auto key : section.children("Key")) {
            auto key_name = key.attribute("Name"), value = key.attribute("Value");
            if (!key_name || !value)
                throw std::invalid_argument("Missing asset XML key attribute");
            if (!config.emplace(key_name.value(), value.value()).second)
                throw std::invalid_argument("Duplicate asset XML key");
        }
        if (first)
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
    auto asset = std::make_shared<framework::AssetBase>(framework::parse_sdk_uuid(id), name, type,
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
                if (found != config.end() && !parse_account_integer(found->second, number))
                    throw std::invalid_argument("Invalid asset type integer");
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
