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
#include "opensim/services/library_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_set>
namespace opensim::services {
using framework::Uuid;
namespace {
const Uuid Owner("11111111-1111-0000-0000-000100bba000"), Root("00000112-000f-0000-0000-000100bba000");
using Config = std::map<std::string, std::string>;
std::string text(const Config& config, const std::string& key, const std::string& fallback) {
    const auto found = config.find(key);
    return found == config.end() ? fallback : found->second;
}
Uuid library_uuid(const Config& config, const std::string& key, const Uuid& fallback) {
    const auto found = config.find(key);
    return found == config.end() ? fallback
                                 : framework::construct_sdk_uuid(framework::utf16_replacing(found->second));
}
std::int64_t number(const Config& config, const std::string& key, std::int64_t fallback, bool wide = false) {
    const auto found = config.find(key);
    if (found == config.end())
        return fallback;
    const auto value = framework::utf16_replacing(found->second);
    if (wide)
        return framework::invariant_convert_to_int64(value);
    return framework::invariant_convert_to_int32(value);
}
int mask(const Config& config, const std::string& key, int fallback) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(config, key, fallback, true)));
}
int now() {
    return static_cast<std::int32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}
}
struct LibraryService::State {
    std::shared_ptr<InventoryFolderTree> root;
    std::unordered_map<Uuid, std::shared_ptr<InventoryFolderTree>, framework::UuidHash> folders;
    std::unordered_map<Uuid, std::shared_ptr<InventoryItem>, framework::UuidHash> items;
    Log log;
    void report(const std::string& message) {
        if (log)
            log(message);
    }
    void load(const std::filesystem::path& path,
              const std::function<void(const Config&, const std::filesystem::path&)>& action) {
        const auto path_text = [&] {
            const auto utf8 = path.generic_u8string();
            return std::string(utf8.begin(), utf8.end());
        };
        std::error_code status_error;
        if (!std::filesystem::is_regular_file(path, status_error)) {
            report("Library file does not exist: " + path_text());
            return;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            report("Library file does not exist: " + path_text());
            return;
        }
        const std::string bytes((std::istreambuf_iterator<char>(file)), {});
        const auto arena = std::make_shared<framework::NiniConfigArena>(
            framework::native_nini_invariant_config_backend());
        // The reader consumes local file bytes directly. Legacy document settings
        // preserve attribute whitespace and expand internal entity references.
        const auto uri_reader = std::make_shared<framework::NiniXmlUriReaderBackend>();
        uri_reader->get_temp_resolver = []() -> std::shared_ptr<framework::NiniXmlUriReaderResolver> {
            throw std::logic_error("Library XML is loaded from an open local file");
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
            report("Library XML error: " + path_text());
            return;
        }
        const auto configs = source->configs();
        for (std::int32_t index = 0; index < configs->count(); ++index) {
            const auto section = configs->at(index);
            Config config;
            for (const auto& key : section->get_keys())
                config.emplace(framework::utf8(*key), framework::utf8(*section->get(key)));
            action(config, path);
        }
    }
    void read_folder(const Config& config) {
        auto folder = std::make_shared<InventoryFolderTree>();
        folder->id = library_uuid(config, "folderID", Root);
        folder->name = text(config, "name", "unknown");
        folder->parent = library_uuid(config, "parentFolderID", Root);
        folder->owner = Owner;
        folder->type = static_cast<std::int16_t>(number(config, "type", 8));
        folder->version = static_cast<std::uint16_t>(number(config, "version", 1));
        const auto parent = folders.find(folder->parent);
        if (parent == folders.end()) {
            report("Library folder parent missing: " + folder->id.str());
            return;
        }
        const auto parent_folder = parent->second;
        if (!folders.emplace(folder->id, folder).second)
            throw std::invalid_argument("Duplicate library folder ID");
        parent_folder->add_child(folder);
    }
    void read_item(const Config& config) {
        auto value = std::make_shared<InventoryItem>();
        value->owner = Owner;
        value->creator_id = Owner.str();
        value->creation_date = now();
        value->id = library_uuid(config, "inventoryID", Root);
        value->asset_id = library_uuid(config, "assetID", value->id);
        value->parent = library_uuid(config, "folderID", Root);
        value->name = text(config, "name", "");
        value->description = text(config, "description", value->name);
        value->inventory_type = static_cast<int>(number(config, "inventoryType", 0));
        value->asset_type = static_cast<int>(number(config, "assetType", value->inventory_type));
        value->current_permissions = mask(config, "currentPermissions", 0x9e000);
        value->next_permissions = mask(config, "nextPermissions", 0x9e000);
        value->everyone_permissions = mask(config, "everyonePermissions", 0x9a000);
        value->base_permissions = mask(config, "basePermissions", 0x9e000);
        // The source reads group permissions from basePermissions as well.
        value->group_permissions = mask(config, "basePermissions", 0);
        value->flags = static_cast<int>(number(config, "flags", 0));
        const auto parent = folders.find(value->parent);
        if (parent == folders.end()) {
            report("Library item parent missing: " + value->id.str());
            return;
        }
        for (const auto& existing : parent->second->items())
            if (existing->id == value->id) {
                report("Duplicate library item in folder: " + value->id.str());
                return;
            }
        parent->second->add_item(value);
        items[value->id] = value;
    }
    void initialize(const std::string& control, const std::string& name) {
        root = std::make_shared<InventoryFolderTree>();
        root->owner = Owner;
        root->id = Root;
        root->name = name;
        root->type = 8;
        root->version = 1;
        folders.emplace(Root, root);
        load(framework::utf8_path(control), [&](const Config& config, const auto& path) {
            if (config.contains("RootVersion")) {
                root->version = static_cast<std::uint16_t>(number(config, "RootVersion", root->version));
                return;
            }
            const auto base = path.parent_path();
            load(base / framework::utf8_path(text(config, "foldersFile", "")),
                 [&](const Config& row, const auto&) { read_folder(row); });
            load(base / framework::utf8_path(text(config, "itemsFile", "")),
                 [&](const Config& row, const auto&) { read_item(row); });
        });
    }
};
LibraryService::LibraryService(const std::string& control, const std::string& name, Log log) {
    static std::mutex mutex;
    static std::shared_ptr<State> singleton;
    std::lock_guard lock(mutex);
    if (singleton) {
        state_ = singleton;
        return;
    }
    state_ = std::make_shared<State>();
    singleton = state_;
    state_->log = std::move(log);
    state_->initialize(control, name);
}
std::shared_ptr<InventoryFolderTree> LibraryService::root() const {
    return state_->root;
}
std::vector<std::shared_ptr<InventoryFolderTree>> LibraryService::ordered_folders() const {
    std::vector<std::shared_ptr<InventoryFolderTree>> result{state_->root};
    const auto walk = [&](auto&& recurse, const std::shared_ptr<InventoryFolderTree>& node)
        -> std::vector<std::shared_ptr<InventoryFolderTree>> {
        auto children = node->children();
        auto rows = children;
        for (const auto& child : children) {
            auto descendants = recurse(recurse, child);
            rows.insert(rows.end(), descendants.begin(), descendants.end());
        }
        return rows;
    };
    auto descendants = walk(walk, state_->root);
    result.insert(result.end(), descendants.begin(), descendants.end());
    std::unordered_set<Uuid, framework::UuidHash> ids;
    for (const auto& folder : result)
        if (!ids.insert(folder->id).second)
            throw std::invalid_argument("Duplicate library traversal folder ID");
    return result;
}
std::unordered_map<Uuid, std::shared_ptr<InventoryFolderTree>, framework::UuidHash>
LibraryService::all_folders() const {
    std::unordered_map<Uuid, std::shared_ptr<InventoryFolderTree>, framework::UuidHash> result;
    for (const auto& folder : ordered_folders())
        result.emplace(folder->id, folder);
    return result;
}
std::shared_ptr<InventoryItem> LibraryService::item(const Uuid& id) const {
    const auto found = state_->items.find(id);
    return found == state_->items.end() ? nullptr : found->second;
}
std::optional<std::vector<std::shared_ptr<InventoryItem>>>
LibraryService::multiple_items(const std::vector<Uuid>& ids) const {
    std::vector<std::shared_ptr<InventoryItem>> result;
    for (const auto& id : ids)
        if (auto value = item(id))
            result.push_back(value);
    return result.empty() ? std::nullopt : std::optional{std::move(result)};
}
std::shared_ptr<InventoryItem> LibraryService::create_item(const Uuid& id, const Uuid& asset,
                                                           const std::string& name,
                                                           const std::string& description, int asset_type,
                                                           int inventory_type, const Uuid& parent) const {
    auto result = std::make_shared<InventoryItem>();
    result->owner = Owner;
    result->creator_id = Owner.str();
    result->id = id;
    result->asset_id = asset;
    result->name = name;
    result->description = description;
    result->asset_type = asset_type;
    result->inventory_type = inventory_type;
    result->parent = parent;
    result->base_permissions = result->current_permissions = result->next_permissions = 0x9e000;
    result->everyone_permissions = 0x9a000;
    result->creation_date = now();
    return result;
}
}
