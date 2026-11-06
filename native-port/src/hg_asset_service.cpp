// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/hg_asset_service.hpp"
#include "opensim/framework/text.hpp"
#include <pugixml.hpp>
#include <sstream>

namespace opensim::services {
namespace {
using framework::AssetType;
std::bitset<256> denied_types(const std::string& list, const AssetExchangePermissions::Log& log) {
    std::bitset<256> result;
    std::istringstream input(list);
    for (std::string entry; std::getline(input, entry, ',');) {
        if (entry.empty())
            continue;
        const auto name = framework::trim_utf8(entry);
        if (const auto type = framework::asset_type_from_enum_name(name))
            result.set(static_cast<std::uint8_t>(*type));
        else if (log)
            log("warn", "[Asset Permissions]: Invalid AssetType " + entry);
    }
    return result;
}

std::string sanitize_xml(std::string xml) {
    constexpr std::string_view repeated = "xmlns:xmlns:";
    const auto start = xml.find(repeated);
    if (start == std::string::npos || start == 0)
        return xml;
    auto end = start + repeated.size();
    while (std::string_view(xml).substr(end).starts_with("xmlns:"))
        end += 6;
    const auto invalid = xml.substr(start, end - start);
    for (auto position = start; position != std::string::npos; position = xml.find(invalid, position + 6))
        xml.replace(position, invalid.size(), "xmlns:");
    return xml;
}

std::string object_xml(const framework::AssetBytes& data) {
    return sanitize_xml(framework::utf8(framework::sdk_bytes_to_string(*data)));
}

framework::AssetBytes object_bytes(std::string_view xml) {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(xml.begin(), xml.end());
    if (!bytes->empty() && bytes->back() != 0)
        bytes->push_back(0);
    return bytes;
}
}

framework::AssetBytes sanitize_object_asset(const framework::AssetBytes& data) {
    return object_bytes(object_xml(data));
}

AssetExchangePermissions::AssetExchangePermissions(const server::IniConfig& config,
                                                   const std::string& section, Log log)
    : blocked_import_(denied_types(config.get(section, "DisallowImport"), log)),
      blocked_export_(denied_types(config.get(section, "DisallowExport"), log)) {}

bool AssetExchangePermissions::allows_import(std::int8_t type) const {
    return !blocked_import_.test(static_cast<std::uint8_t>(type));
}

bool AssetExchangePermissions::allows_export(std::int8_t type) const {
    return !blocked_export_.test(static_cast<std::uint8_t>(type));
}

HgAssetService::HgAssetService(std::shared_ptr<AssetService> backing, std::string home,
                               AccountName account_name, AssetExchangePermissions permissions)
    : backing_(std::move(backing)), home_(std::move(home)), account_name_(std::move(account_name)),
      permissions_(std::move(permissions)) {}

std::optional<std::string> HgAssetService::creator_name(std::string_view id) {
    framework::Uuid creator;
    if (!framework::try_parse_sdk_uuid(id, creator))
        return {};
    std::optional<std::string> name;
    if (!names_.try_get_value(creator, name)) {
        // UserAccountCache.GetUser looks up and caches the zero UUID on a miss.
        name = account_name_(framework::Uuid{});
        names_.add_or_update(framework::Uuid{}, name, 300);
    }
    return name;
}

void HgAssetService::adjust_creator(framework::AssetMetadata& metadata) {
    if (metadata.creator_id) {
        if (const auto name = creator_name(*metadata.creator_id))
            *metadata.creator_id += ";" + home_ + "/" + *name;
    }
}

framework::AssetBytes HgAssetService::rewrite_object(const framework::AssetBytes& data) {
    const auto xml = object_xml(data);
    if (xml.empty())
        return object_bytes(xml);
    pugi::xml_document document;
    constexpr auto options = pugi::parse_default | pugi::parse_fragment | pugi::parse_comments | pugi::parse_pi;
    if (framework::utf16_replacing(xml).size() > 10000000 ||
        !document.load_buffer(xml.data(), xml.size(), options))
        throw std::invalid_argument("Invalid Hypergrid object asset XML");

    // Walk the document without interpreting or dropping unrelated object fields.
    auto node = document.first_child();
    while (node) {
        const std::string_view tag = node.name();
        const auto colon = tag.rfind(':');
        if (tag.substr(colon == std::string_view::npos ? 0 : colon + 1) == "SceneObjectPart") {
            auto creator = node.child("CreatorID").first_child();
            const std::string_view creator_tag = creator.name();
            if ((creator_tag == "Guid" || creator_tag == "UUID") &&
                creator.first_child().type() == pugi::node_pcdata) {
                creator.set_name("UUID");
                const auto existing = node.child("CreatorData").first_child();
                if (existing.type() != pugi::node_pcdata) {
                    if (const auto name = creator_name(creator.child_value())) {
                        const auto field = colon == std::string_view::npos
                                               ? std::string("CreatorData")
                                               : std::string(tag.substr(0, colon + 1)) + "CreatorData";
                        node.append_child(field.c_str()).text().set((home_ + ";" + *name).c_str());
                    }
                }
            }
        }
        if (const auto child = node.first_child()) {
            node = child;
        } else {
            while (node != document && !node.next_sibling())
                node = node.parent();
            node = node.next_sibling();
        }
    }
    std::ostringstream output;
    document.save(output, "", pugi::format_raw | pugi::format_no_declaration);
    return object_bytes(output.str());
}

AssetService::Asset HgAssetService::prepare_export(Asset asset) {
    if (!asset || !permissions_.allows_export(asset->type()))
        return {};
    if (asset->type() == static_cast<std::int8_t>(AssetType::Object) && asset->data)
        asset->data = rewrite_object(asset->data);
    adjust_creator(*asset->metadata());
    return asset;
}

AssetService::Asset HgAssetService::get_asset(const std::string& id) {
    return prepare_export(backing_->get_asset(id));
}

AssetService::Asset HgAssetService::get_cached(const std::string& id) {
    return prepare_export(backing_->get_cached(id));
}

std::shared_ptr<framework::AssetMetadata> HgAssetService::get_metadata(const std::string& id) {
    auto metadata = backing_->get_metadata(id);
    if (metadata)
        adjust_creator(*metadata);
    return metadata;
}

framework::AssetBytes HgAssetService::get_data(const std::string& id) {
    const auto asset = get_asset(id);
    if (!asset)
        return nullptr;
    if (asset->type() == static_cast<std::int8_t>(AssetType::Object) && asset->data)
        return object_bytes(object_xml(asset->data));
    return asset->data;
}

bool HgAssetService::get_async(const std::string& id, std::any sender, Retrieved callback) {
    auto asset = get_asset(id);
    if (callback)
        callback(id, std::move(sender), std::move(asset));
    return true;
}

std::vector<bool> HgAssetService::assets_exist(const std::vector<std::string>& ids) {
    return backing_->assets_exist(ids);
}

std::optional<std::string> HgAssetService::store(Asset asset) {
    if (!permissions_.allows_import(asset->type()))
        return std::string{};
    if (asset->type() == static_cast<std::int8_t>(AssetType::Object) && asset->data)
        asset->data = object_bytes(object_xml(asset->data));
    return backing_->store(std::move(asset));
}
}
