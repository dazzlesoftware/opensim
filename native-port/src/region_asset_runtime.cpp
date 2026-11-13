// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_asset_runtime.hpp"
#include "fs_asset_service.hpp"
#include <pugixml.hpp>
#include "opensim/framework/texture_entry.hpp"
#include <charconv>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "opensim/services/get_assets_module.hpp"
#include "opensim/services/hg_asset_service.hpp"
#include "opensim/services/local_asset_connector.hpp"
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/services/flotsam_cache_files.hpp"
#include "opensim/services/flotsam_cache_cleanup.hpp"
#include "opensim/services/flotsam_cache_maintenance.hpp"
#include "opensim/services/flotsam_console_files.hpp"
#include "opensim/framework/status_date_format.hpp"
#include "opensim/services/flotsam_file_access.hpp"
#include "opensim/services/native_flotsam_config.hpp"
#include "opensim/services/native_flotsam_datetime.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/http_uri.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/user_account.hpp"
#include <bit>
#include <algorithm>
#include <exception>
#include <chrono>
#include <cstdio>
#include <thread>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32) || defined(__GLIBC__)
#include <malloc.h>
#endif
namespace opensim::services {
std::string serialize_baked_appearance(const framework::WearableCacheItems &cache) {
    pugi::xml_document document;
    auto root = document.append_child("BakedAppearance");
    for (const bool extended : {false, true})
        for (const auto &item : cache) {
            if (!item || !item->texture_asset || (item->texture_index > 26) != extended)
                continue;
            auto bake = root.append_child(extended ? "BESetA" : "BakedTexture");
            bake.append_attribute("TextureIndex") = item->texture_index;
            bake.append_attribute("CacheId") = item->cache_id.str().c_str();
            const auto &asset = *item->texture_asset;
            auto row = bake.append_child("AssetBase");
            const auto add = [&](const char *field, const std::string &value) {
                row.append_child(field).text().set(value.c_str());
            };
            if (asset.data) {
                network::Value bytes;
                bytes.binary_data = asset.data;
                add("Data", network::binary_base64(bytes));
            }
            row.append_child("FullID").append_child("Guid").text().set(asset.full_id().str().c_str());
            add("ID", asset.id());
            add("Name", asset.name().value_or(""));
            add("Description", asset.description().value_or(""));
            if (const auto creator = asset.creator_id())
                add("CreatorID", *creator);
            add("Type", std::to_string(asset.type()));
            add("Local", asset.local() ? "true" : "false");
            add("Temporary", asset.temporary() ? "true" : "false");
            add("UploadAttempts", std::to_string(asset.upload_attempts));
            std::string flags;
            for (const auto &[bit, name] :
                 {std::pair{framework::Maptile, "Maptile"}, std::pair{framework::Rewritable, "Rewritable"},
                  std::pair{framework::Collectable, "Collectable"}}) {
                if (asset.flags() & bit) {
                    if (!flags.empty())
                        flags += ' ';
                    flags += name;
                }
            }
            add("Flags", flags.empty() ? "Normal" : flags);
        }
    std::ostringstream output;
    document.save(output, "", pugi::format_raw | pugi::format_no_declaration);
    return output.str();
}
namespace {
std::string date_xml(framework::AssetDateTime date) {
    using namespace std::chrono;
    using Ticks = duration<std::int64_t, std::ratio<1, 10000000>>;
    const auto instant = sys_days{year{1} / January / 1} + Ticks{date.ticks()};
    const auto day = floor<days>(instant);
    const year_month_day calendar{day};
    const hh_mm_ss clock{instant - day};
    char text[48];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02uT%02d:%02d:%02d",
                  int(calendar.year()), unsigned(calendar.month()), unsigned(calendar.day()),
                  int(clock.hours().count()), int(clock.minutes().count()), int(clock.seconds().count()));
    std::string value(text);
    if (const auto fraction = clock.subseconds().count()) {
        std::snprintf(text, sizeof(text), ".%07lld", static_cast<long long>(fraction));
        value += text;
        while (value.back() == '0')
            value.pop_back();
    }
    if (date.kind() == 1)
        value += 'Z';
    else if (date.kind() == 2) {
        const auto runtime = framework::shared_native_invariant_datetime_context();
        const auto minutes = runtime->local_rule_snapshot()->local_offset_no_throw(date) / 600000000;
        const auto magnitude = minutes < 0 ? -minutes : minutes;
        std::snprintf(text, sizeof(text), "%c%02d:%02d", minutes < 0 ? '-' : '+',
                      static_cast<int>(magnitude / 60), static_cast<int>(magnitude % 60));
        value += text;
    }
    return value;
}

std::string asset_flags_xml(std::int32_t flags) {
    std::string value;
    for (const auto &[flag, name] :
         {std::pair{framework::Maptile, "Maptile"}, std::pair{framework::Rewritable, "Rewritable"},
          std::pair{framework::Collectable, "Collectable"}}) {
        if (!(flags & flag))
            continue;
        if (!value.empty())
            value += ' ';
        value += name;
    }
    return value.empty() ? "Normal" : value;
}

std::string asset_xml(const framework::AssetMetadata &metadata, const framework::AssetBase *asset) {
    pugi::xml_document document;
    auto row = document.append_child(asset ? "AssetBase" : "AssetMetadata");
    const auto add = [&](const char *name, const std::string &value) {
        row.append_child(name).text().set(value.c_str());
    };
    if (asset && asset->data)
        add("Data", network::binary_base64(network::osd_from_binary(asset->data)));
    row.append_child("FullID").append_child("Guid").text().set(metadata.full_id().str().c_str());
    add("ID", metadata.id());
    if (metadata.name)
        add("Name", *metadata.name);
    if (metadata.description)
        add("Description", *metadata.description);
    if (!asset)
        add("CreationDate", date_xml(metadata.creation_date));
    add("Type", std::to_string(metadata.type));
    if (asset) {
        add("UploadAttempts", std::to_string(asset->upload_attempts));
    } else {
        add("ContentType", metadata.content_type());
        if (metadata.sha1)
            add("SHA1", network::binary_base64(network::osd_from_binary(metadata.sha1)));
    }
    add("Local", metadata.local ? "true" : "false");
    add("Temporary", metadata.temporary ? "true" : "false");
    if (metadata.creator_id)
        add("CreatorID", *metadata.creator_id);
    add("Flags", asset_flags_xml(metadata.flags));
    std::ostringstream output;
    row.print(output, "", pugi::format_raw);
    return output.str();
}
}

std::string serialize_remote_asset(const framework::AssetBase &asset) {
    const auto metadata = asset.metadata();
    if (!metadata)
        throw std::invalid_argument("Cannot serialize an asset without metadata");
    return asset_xml(*metadata, &asset);
}

std::string serialize_remote_asset_metadata(const framework::AssetMetadata &metadata) {
    return asset_xml(metadata, nullptr);
}

namespace {
framework::AssetDateTime parse_metadata_date(std::string value) {
    value = framework::trim_utf8(value);
    if (value.size() < 19 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':')
        throw std::invalid_argument("Invalid asset metadata creation date");
    const bool utc = value.ends_with('Z');
    if (utc)
        value.pop_back();
    framework::AssetDateTime date;
    const auto context = framework::shared_native_invariant_datetime_context();
    if (!framework::try_parse_invariant_datetime_utf8(value, date, context->callbacks()))
        throw std::invalid_argument("Invalid asset metadata creation date");
    return utc ? framework::AssetDateTime(date.ticks(), 1) : date;
}

bool is_nil(pugi::xml_node node) {
    for (const auto attribute : node.attributes()) {
        const std::string_view name = attribute.name();
        if (!name.ends_with(":nil"))
            continue;
        const auto ns = "xmlns:" + std::string(name.substr(0, name.size() - 4));
        for (auto scope = node; scope; scope = scope.parent()) {
            if (auto declaration = scope.attribute(ns.c_str())) {
                if (std::string_view(declaration.value()) == "http://www.w3.org/2001/XMLSchema-instance")
                    return attribute.as_bool();
                break;
            }
        }
    }
    return false;
}

std::optional<bool> xml_boolean(std::string_view text) {
    const auto value = framework::trim_utf8(text);
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    return std::nullopt;
}

std::string asset_element_text(pugi::xml_node node) {
    std::string text;
    for (const auto part : node.children()) {
        if (part.type() == pugi::node_pcdata || part.type() == pugi::node_cdata)
            text += part.value();
        else if (part.type() == pugi::node_element)
            throw std::invalid_argument("Unexpected element in asset XML value");
    }
    return text;
}

std::shared_ptr<framework::AssetBase> deserialize_asset_wire(std::string_view xml, bool metadata) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return {};
    const auto root = document.document_element();
    if (std::string_view(root.name()) != (metadata ? "AssetMetadata" : "AssetBase") ||
        *root.attribute("xmlns").value() || is_nil(root))
        return {};
    auto asset = std::make_shared<framework::AssetBase>();
    if (metadata)
        asset->metadata(std::make_shared<framework::AssetMetadata>());
    const auto integer = [](pugi::xml_node node) {
        const auto trimmed = framework::trim_utf8(asset_element_text(node));
        auto text = std::string_view(trimmed);
        if (text.starts_with('+') && text.size() > 1 && text[1] >= '0' && text[1] <= '9')
            text.remove_prefix(1);
        int result{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::invalid_argument("Invalid asset integer");
        return result;
    };
    try {
        // XmlSerializer reads each scalar member once.
        std::unordered_set<std::string_view> seen_fields;
        for (const auto node : root.children()) {
            if (*node.attribute("xmlns").value())
                continue;
            const std::string_view field = node.name();
            if (!seen_fields.insert(field).second)
                continue;
            const bool nil = is_nil(node);
            const auto text = [&]() -> std::optional<std::string> {
                return nil ? std::nullopt : std::optional{asset_element_text(node)};
            };
            if ((!metadata && field == "Data") || (metadata && field == "SHA1")) {
                auto &destination = metadata ? asset->metadata()->sha1 : asset->data;
                if (nil) {
                    destination.reset();
                    continue;
                }
                network::Value bytes;
                bytes.kind = network::Value::Kind::Binary;
                bytes.text = asset_element_text(node);
                destination = network::osd_binary(bytes);
            } else if (field == "FullID") {
                framework::Uuid id;
                for (const auto guid : node.children("Guid")) {
                    if (*guid.attribute("xmlns").value())
                        continue;
                    const auto guid_text = framework::trim_utf8(asset_element_text(guid));
                    auto value = std::string_view(guid_text);
                    // This XML field is a System.Guid. The string ID uses the SDK parser.
                    if (value.size() == 38 && value.front() == '(' && value.back() == ')')
                        value = value.substr(1, 36);
                    id = framework::Uuid(value);
                    break;
                }
                asset->full_id(id);
            } else if (field == "ID")
                asset->id(text());
            else if (field == "Name")
                asset->name(text());
            else if (field == "Description")
                asset->description(text());
            else if (field == "CreatorID")
                asset->creator_id(text());
            else if (field == "Type") {
                const int type = integer(node);
                if (type < -128 || type > 127)
                    return {};
                asset->type(static_cast<std::int8_t>(type));
            } else if (metadata && field == "CreationDate")
                asset->metadata()->creation_date = parse_metadata_date(asset_element_text(node));
            else if (metadata && field == "ContentType") {
                if (const auto type = text())
                    asset->metadata()->content_type(*type);
            } else if (!metadata && field == "UploadAttempts")
                asset->upload_attempts = integer(node);
            else if (field == "Local" || field == "Temporary") {
                const auto value = xml_boolean(asset_element_text(node));
                if (!value)
                    return {};
                if (field == "Local")
                    asset->local(*value);
                else
                    asset->temporary(*value);
            } else if (field == "Flags") {
                std::istringstream values(asset_element_text(node));
                std::string flag;
                std::int32_t flags = framework::Normal;
                while (values >> flag) {
                    if (flag == "Maptile")
                        flags |= framework::Maptile;
                    else if (flag == "Rewritable")
                        flags |= framework::Rewritable;
                    else if (flag == "Collectable")
                        flags |= framework::Collectable;
                    else if (flag != "Normal")
                        return {};
                }
                asset->flags(flags);
            }
        }
    } catch (const std::exception &) {
        return {};
    }
    return asset;
}
}

std::shared_ptr<framework::AssetBase> deserialize_remote_asset(std::string_view xml) {
    return deserialize_asset_wire(xml, false);
}

framework::WearableCacheItems deserialize_baked_appearance(std::string_view xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return {};
    const auto root = document.child("BakedAppearance");
    framework::WearableCacheItems result;
    for (const auto bake : root.children()) {
        const std::string_view name = bake.name();
        if (name != "BakedTexture" && name != "BESetA")
            continue;
        const auto index = bake.attribute("TextureIndex").as_uint(45);
        if (index >= framework::TextureEntry::max_faces)
            continue;
        std::ostringstream xml_asset;
        bake.child("AssetBase").print(xml_asset, "", pugi::format_raw);
        auto asset = deserialize_remote_asset(xml_asset.str());
        if (!asset || asset->full_id().is_zero() || !asset->data)
            continue;
        auto item = std::make_shared<framework::WearableCacheItem>();
        item->texture_index = index;
        framework::try_parse_sdk_uuid(bake.attribute("CacheId").value(), item->cache_id);
        item->texture_id = asset->full_id();
        item->texture_asset = std::move(asset);
        result.push_back(std::move(item));
    }
    return result;
}
std::shared_ptr<framework::AssetBase> request_remote_asset(const network::WebHttpClients &clients,
                                                           const std::string &server,
                                                           const framework::Uuid &id) {
    using Clock = std::chrono::steady_clock;
    try {
        auto backend = framework::native_config_uri_object_backend();
        auto url = framework::utf16_replacing(server + "/assets/" + id.str());
        auto uri =
            backend->create(framework::nini_writer_text(url), framework::ConfigUriObjectKind::Absolute);
        framework::ConfigXmlHttpOperation operation;
        operation.method = "GET";
        operation.headers = {{"Keep-Alive", framework::nini_writer_text(u"timeout=30, max=10")},
                             {"Connection", framework::nini_writer_text(u"Keep-Alive")}};
        std::optional<std::int64_t> length;
        operation.response_headers = [&length](const auto &response) {
            std::vector<std::u16string> values;
            for (const auto &field : response.headers)
                if (framework::ordinal_equals_ignore_case(field.name, "Content-Length"))
                    values.push_back(field.value);
            length = framework::config_http_content_length(values);
        };
        auto deadline = std::make_shared<Clock::time_point>(Clock::time_point::max());
        operation.body_interrupted = [deadline] {
            return Clock::now() >= *deadline;
        };
        auto client = clients.get_new_global_client(0);
        auto stream = client->send_headers(std::move(uri), std::move(operation), true);
        struct Close {
            std::shared_ptr<framework::ConfigXmlStream> stream;
            ~Close() {
                try {
                    stream->close();
                } catch (...) {
                }
            }
        } close{stream};
        if (!length || *length == 0)
            return {};
        const auto timeout = network::osd_service_receive_timeout(length, true);
        if (timeout < -1)
            return {};
        if (timeout >= 0)
            *deadline = Clock::now() + std::chrono::milliseconds(timeout);
        const auto body = stream->copy_to_memory();
        if (body.empty())
            return {};
        return deserialize_remote_asset({reinterpret_cast<const char *>(body.data()), body.size()});
    } catch (const std::exception &) {
        return {};
    }
}
namespace {
struct ForeignAssetTarget {
    std::string url;
    framework::Uuid id;
};

std::optional<ForeignAssetTarget> foreign_asset_target(const std::string& value) {
    if (!value.starts_with('h') && !value.starts_with('H'))
        return std::nullopt;
    const framework::HttpUri target(value, true);
    if (!target.resolved() || target.path.size() < 36)
        return std::nullopt;
    auto id = std::string_view(target.path);
    if (id.starts_with('/'))
        id.remove_prefix(1);
    framework::Uuid uuid;
    if (!framework::try_parse_sdk_uuid(framework::utf16(id), uuid))
        return std::nullopt;
    return ForeignAssetTarget{target.url, uuid};
}

class GridAssetService final : public AssetService, public std::enable_shared_from_this<GridAssetService> {
    std::shared_ptr<network::WebHttpClients> clients_;
    std::string uri_, authorization_;
    std::function<bool(std::function<void()>)> schedule_;
    std::mutex pending_mutex_;
    std::unordered_map<std::string, std::vector<std::function<void(Asset)>>> pending_;

    void finish_async(const std::string& id, const Asset& asset, bool scheduled) {
        std::vector<std::function<void(Asset)>> callbacks;
        {
            std::lock_guard lock(pending_mutex_);
            auto entry = pending_.extract(id);
            callbacks = std::move(entry.mapped());
        }
        // The caller that submitted a rejected task receives false directly.
        for (std::size_t index = scheduled ? 0 : 1; index < callbacks.size(); ++index) {
            try {
                callbacks[index](asset);
            } catch (...) {
                // A callback must not prevent other waiters from receiving the result.
            }
        }
    }

    std::optional<std::string> request(std::string method, const std::string &path, std::string body = {}) {
        try {
            auto backend = framework::native_config_uri_object_backend();
            auto url = backend->create(framework::nini_writer_text(framework::utf16_replacing(uri_ + path)),
                                       framework::ConfigUriObjectKind::Absolute);
            framework::ConfigXmlHttpOperation operation;
            operation.method = std::move(method);
            operation.body = std::move(body);
            operation.timeout_milliseconds = 30000;
            if (!operation.body.empty())
                operation.headers.emplace_back("Content-Type", framework::nini_writer_text(u"text/xml"));
            if (!authorization_.empty())
                operation.headers.emplace_back(
                    "Authorization", framework::nini_writer_text(framework::utf16_replacing(authorization_)));
            auto stream = clients_->get_new_global_client(30000)->send_headers(std::move(url),
                                                                               std::move(operation), true);
            const auto bytes = stream->copy_to_memory();
            stream->close();
            return std::string(bytes.begin(), bytes.end());
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }
    static std::string xml(pugi::xml_node node) {
        std::ostringstream output;
        node.print(output, "", pugi::format_raw);
        return output.str();
    }
    static bool boolean_reply(const std::optional<std::string> &reply) {
        pugi::xml_document document;
        if (!reply || !document.load_buffer(reply->data(), reply->size()))
            return false;
        const auto root = document.document_element();
        if (std::string_view(root.name()) != "boolean" || *root.attribute("xmlns").value())
            return false;
        try {
            return xml_boolean(asset_element_text(root)).value_or(false);
        } catch (const std::invalid_argument&) {
            return false;
        }
    }

  public:
    GridAssetService(std::string uri, std::shared_ptr<network::WebHttpClients> clients)
        : clients_(std::move(clients)), uri_(std::move(uri)) {}

    GridAssetService(const server::IniConfig &config, std::shared_ptr<network::WebHttpClients> clients,
                     std::function<bool(std::function<void()>)> schedule)
        : clients_(std::move(clients)), schedule_(std::move(schedule)) {
        if (!config.has_section("AssetService"))
            throw std::invalid_argument("AssetServicesConnector requires an AssetService section");
        auto server = config.get("AssetService", "AssetServerURI");
        if (server.empty())
            server = config.get("Network", "asset_server_url");
        framework::HttpHost host(server, true);
        if (!clients_ || !host.resolved())
            throw std::invalid_argument("AssetServicesConnector requires a resolved AssetServerURI");
        uri_ = std::move(host.uri);
        if (config.get("AssetService", "AuthType", config.get("Network", "AuthType")) ==
            "BasicHttpAuthentication") {
            const auto username =
                config.get("AssetService", "HttpAuthUsername", config.get("Network", "HttpAuthUsername"));
            const auto password =
                config.get("AssetService", "HttpAuthPassword", config.get("Network", "HttpAuthPassword"));
            const auto credentials = username + ":" + password;
            authorization_ =
                "Basic " +
                network::binary_base64(network::osd_from_binary(
                    std::make_shared<std::vector<std::uint8_t>>(credentials.begin(), credentials.end())));
        }
    }
    Asset get_asset(const std::string &id) override {
        const auto reply = request("GET", "/assets/" + id);
        return reply ? deserialize_remote_asset(*reply) : nullptr;
    }
    Asset get_foreign(const std::string &, const std::optional<std::string> &, bool) override {
        return {};
    }
    Asset get_cached(const std::string &) override {
        return {};
    }
    std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string &id) override {
        const auto reply = request("GET", "/assets/" + id + "/metadata");
        const auto asset = reply ? deserialize_asset_wire(*reply, true) : nullptr;
        return asset ? asset->metadata() : nullptr;
    }
    framework::AssetBytes get_data(const std::string &id) override {
        const auto reply = request("GET", "/assets/" + id + "/data");
        return reply && !reply->empty()
                   ? std::make_shared<std::vector<std::uint8_t>>(reply->begin(), reply->end())
                   : nullptr;
    }
    bool get_async(const std::string &id, std::any sender, Retrieved callback) override {
        {
            std::lock_guard lock(pending_mutex_);
            auto& callbacks = pending_[id];
            callbacks.emplace_back([id, sender = std::move(sender), callback = std::move(callback)](Asset asset) {
                callback(id, sender, std::move(asset));
            });
            if (callbacks.size() > 1)
                return true;
        }
        if (schedule_([self = shared_from_this(), id] {
                self->finish_async(id, self->get_asset(id), true);
            }))
            return true;
        finish_async(id, {}, false);
        return false;
    }
    void get(const std::string &, const std::optional<std::string> &, bool, Complete callback) override {
        if (callback)
            callback(nullptr);
    }
    std::vector<bool> assets_exist(const std::vector<std::string> &ids) override {
        pugi::xml_document document;
        auto array = document.append_child("ArrayOfString");
        for (const auto &id : ids)
            array.append_child("string").text().set(id.c_str());
        const auto reply = request("POST", "/get_assets_exist", xml(array));
        pugi::xml_document response;
        if (!reply || !response.load_buffer(reply->data(), reply->size()))
            return std::vector<bool>(ids.size());
        const auto flags = response.document_element();
        if (std::string_view(flags.name()) != "ArrayOfBoolean" ||
            *flags.attribute("xmlns").value() || is_nil(flags))
            return std::vector<bool>(ids.size());
        std::vector<bool> result;
        try {
            for (const auto flag : flags.children("boolean")) {
                if (*flag.attribute("xmlns").value())
                    continue;
                const auto value = xml_boolean(asset_element_text(flag));
                if (!value)
                    return std::vector<bool>(ids.size());
                result.push_back(*value);
            }
        } catch (const std::invalid_argument&) {
            return std::vector<bool>(ids.size());
        }
        return result;
    }
    std::optional<std::string> store(Asset asset) override {
        if (!asset)
            return std::nullopt;
        const auto id = asset->id();
        if (id.empty() || id == framework::Uuid{}.str() || asset->full_id().is_zero()) {
            auto uuid = asset->full_id();
            if (uuid.is_zero())
                framework::try_parse_sdk_uuid(id, uuid);
            asset->full_id(uuid.is_zero() ? framework::Uuid::random() : uuid);
        }
        if (asset->temporary() || asset->local())
            return asset->id();
        const auto reply = request("POST", "/assets/", serialize_remote_asset(*asset));
        pugi::xml_document document;
        if (!reply || !document.load_buffer(reply->data(), reply->size()))
            return std::string{};
        const auto root = document.document_element();
        if (std::string_view(root.name()) != "string" || *root.attribute("xmlns").value() || is_nil(root))
            return std::string{};
        std::string stored;
        try {
            stored = asset_element_text(root);
        } catch (const std::invalid_argument&) {
            return std::string{};
        }
        if (stored.empty() || stored == framework::Uuid{}.str())
            return std::string{};
        asset->id(stored);
        return stored;
    }
    bool update_content(const std::string &id, framework::AssetBytes data) override {
        const auto metadata = get_metadata(id);
        if (!metadata)
            return false;
        auto asset = std::make_shared<framework::AssetBase>();
        asset->metadata(metadata);
        asset->data = std::move(data);
        return boolean_reply(request("POST", "/assets/" + id, serialize_remote_asset(*asset)));
    }
    bool erase(const std::string &id) override {
        return boolean_reply(request("DELETE", "/assets/" + id));
    }
};

std::shared_ptr<FlotsamStaticConfig> shared_cache_configuration() {
    static auto state = std::make_shared<FlotsamStaticConfig>();
    return state;
}
std::shared_ptr<std::recursive_mutex> shared_cache_configuration_gate() {
    static auto gate = std::make_shared<std::recursive_mutex>();
    return gate;
}
bool white(const std::string &text) {
    auto chars = framework::utf16_replacing(text);
    return std::all_of(chars.begin(), chars.end(), framework::unicode_whitespace);
}
struct Backend final : AssetsModuleBackend, AssetConnectorBackend {
    std::function<void(std::string)> log;
    RegionAssetRuntime::StructuredLog structured_log;
    std::shared_ptr<AssetStorageState> storage_state = std::make_shared<AssetStorageState>();
    std::shared_ptr<NativeAssetStorageBackend> storage;
    std::shared_ptr<AssetService> service;
    std::shared_ptr<ConfiguredAssetService> local_service;
    std::shared_ptr<network::WebHttpClients> http;
    framework::ObjectJobEngine callbacks;
    framework::ObjectJobEngine local_reads;
    framework::ObjectJobEngine remote_reads;
    explicit Backend(std::function<void(std::string)> logger, RegionAssetRuntime::StructuredLog typed)
        : log(std::move(logger)), structured_log(std::move(typed)),
          callbacks([](std::any value) { std::any_cast<std::function<void()>>(value)(); },
                    "RegionAssetCallbacks", 1000, 3,
                    [this](const auto &name, const auto &error) { job_error(name, error); }),
          local_reads([](std::any value) { std::any_cast<std::function<void()>>(value)(); },
                    "GetAssetsWorkers", 2000, 2,
                    [this](const auto &name, const auto &error) { job_error(name, error); }),
          remote_reads([](std::any value) { std::any_cast<std::function<void()>>(value)(); },
                    "GetRemoteAssetsWorkers", 2000, 2,
                    [this](const auto &name, const auto &error) { job_error(name, error); }) {
        // Capture the logger by value: storage may outlive the runtime through caps.
        storage = std::make_shared<NativeAssetStorageBackend>(
            [logger = log, typed = structured_log](const auto &level, const auto &text) {
                if (typed)
                    typed(level, text);
                else if (logger)
                    logger(level + ": " + text);
            });
    }
    void emit(const std::string &level, const std::string &text) noexcept {
        try {
            if (structured_log)
                structured_log(level, text);
            else if (log)
                log(text);
        } catch (...) {
        }
    }
    void warn(const std::string &text) override {
        emit("warn", text);
    }
    void error(const std::string &text) override {
        emit("error", text);
    }
    void info(const std::string &text) override {
        emit("info", text);
    }
    void debug(const std::string &text) override {
        emit("debug", text);
    }
    void job_error(const std::string &name, const std::exception &error) override {
        emit("error", name + ": " + error.what());
    }
    void post(std::function<void()> callback, const std::string &) override {
        callbacks.enqueue(std::move(callback));
    }
    bool queue_asset_read(std::function<void()> callback, bool remote) override {
        return (remote ? remote_reads : local_reads).enqueue(std::move(callback));
    }
    std::shared_ptr<AssetService> load_service(const std::string &module,
                                               const server::IniConfig *config) override {
        if (module == "OpenSim.Services.Connectors.dll:AssetServicesConnector") {
            service = make_grid_asset_service(
                *config, http, [this](auto task) { return callbacks.enqueue(std::move(task)); });
            return service;
        }
        if (module == fs_asset_module) {
            service = make_fs_asset_service(*config, storage);
            return service;
        }
        if (module != "OpenSim.Services.AssetService.dll:AssetService")
            throw std::invalid_argument("Unsupported local AssetService module: " + module);
        AssetStorageFactory factory(storage, storage_state);
        local_service = factory.create(config);
        service = local_service;
        return service;
    }
};
struct CacheBackend final : FlotsamCacheModuleBackend {
    std::shared_ptr<NativeAssetStorageBackend> storage;
    std::shared_ptr<FlotsamCacheCleaner> cleaner;
    std::function<void(const std::string&)> cleanup_warning;
    std::mutex cleanup_mutex;
    std::atomic<bool> stopping = false;
    explicit CacheBackend(std::shared_ptr<NativeAssetStorageBackend> value) : storage(std::move(value)) {}
    std::shared_ptr<DefaultAssetLoader> load_loader(const std::string &name) override {
        return storage->load_loader(name);
    }
    void cleanup_elapsed(std::shared_ptr<FlotsamCacheLifecycle> life) override {
        std::lock_guard lock(cleanup_mutex);
        if (stopping || !cleaner)
            return;
        try {
            cleaner->elapsed();
        } catch (const std::exception& error) {
            if (cleanup_warning)
                cleanup_warning(std::string("[FLOTSAM ASSET CACHE] Cleanup failed: ") + error.what());
            std::lock_guard timer_lock(life->timer_mutex);
            life->cleanup_running = false;
            if (!stopping && life->timer_running)
                life->timer->start();
        }
    }
    void expire(framework::AssetDateTime date) {
        std::lock_guard lock(cleanup_mutex);
        if (!stopping && cleaner)
            cleaner->clean_all(date);
    }
    std::int32_t touch(FlotsamCacheMaintenance& maintenance, bool fetch_uncached) {
        std::lock_guard lock(cleanup_mutex);
        return stopping ? 0 : maintenance.touch(fetch_uncached);
    }
    void quiesce(const std::shared_ptr<FlotsamCacheLifecycle>& life) {
        stopping = true;
        life->cleanup_running = false;
        std::lock_guard lock(cleanup_mutex);
    }
};
class LiveCacheAccessEntries final : public FlotsamAccessEntries {
    std::function<std::shared_ptr<FlotsamKeyEntries>()> tracker_;

  public:
    explicit LiveCacheAccessEntries(std::function<std::shared_ptr<FlotsamKeyEntries>()> tracker)
        : tracker_(std::move(tracker)) {}
    bool contains(const std::filesystem::path& path) override {
        const auto entries = tracker_();
        return !entries || entries->contains(framework::utf8(path.u16string()));
    }
    void add(const std::filesystem::path& path, int duration) override {
        if (const auto entries = tracker_())
            entries->add(framework::utf8(path.u16string()), duration);
    }
    void pump() override {
        if (const auto entries = tracker_())
            entries->pump();
    }
};
std::int64_t cache_heap_bytes() {
#if defined(__APPLE__)
    malloc_statistics_t statistics{};
    malloc_zone_statistics(nullptr, &statistics);
    return static_cast<std::int64_t>(statistics.size_in_use);
#elif defined(_MSC_VER)
    _HEAPINFO entry{};
    std::int64_t bytes = 0;
    while (_heapwalk(&entry) == _HEAPOK)
        if (entry._useflag == _USEDENTRY)
            bytes += static_cast<std::int64_t>(entry._size);
    return bytes;
#elif defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 33)
    return static_cast<std::int64_t>(mallinfo2().uordblks);
#else
    return 0;
#endif
#else
    return 0;
#endif
}
struct AssetCircuitDirectory final : AssetsUserManagement {
    RegionAssetRuntime::AssetServerLookup lookup;
    explicit AssetCircuitDirectory(RegionAssetRuntime::AssetServerLookup value) : lookup(std::move(value)) {}
    std::optional<std::string> user_server_url(framework::Uuid agent, const std::string &key) override {
        return key == "AssetServerURI" ? lookup(agent) : std::nullopt;
    }
};
struct Scene final : AssetsModuleScene, AssetConnectorScene, FlotsamCacheScene {
    std::shared_ptr<SceneCapabilityEvents> source;
    std::shared_ptr<AssetService> registered;
    std::shared_ptr<framework::IAssetCache> cache;
    std::optional<std::string> name;
    framework::Uuid id;
    std::atomic<bool> stopping = false;
    Scene(std::shared_ptr<SceneCapabilityEvents> events, std::optional<std::string> region,
          framework::Uuid region_id)
        : source(std::move(events)), name(std::move(region)), id(region_id) {}
    void register_service(std::shared_ptr<AssetService> service) override {
        registered = std::move(service);
    }
    AssetConnectorCache asset_cache() override {
        return {cache, true};
    }
    std::optional<std::string> region_name() override {
        return name;
    }
    SceneCapabilityEvents &events() override {
        return *source;
    }
    bool shutting_down() const override {
        return stopping;
    }
    std::shared_ptr<server::AssetRetrievalService> asset_service() override {
        return registered;
    }
    std::shared_ptr<AssetsUserManagement> directory;
    std::shared_ptr<AssetsUserManagement> user_management() override {
        return directory;
    }
    std::shared_ptr<AssetsExternalCaps> external_caps() override {
        return {};
    }
    void register_cache(std::shared_ptr<framework::IAssetCache> value) override {
        cache = std::move(value);
    }
    void unregister_cache(std::shared_ptr<framework::IAssetCache> value) override {
        if (cache == value)
            cache.reset();
    }
    std::shared_ptr<AssetService> request_asset_service() override {
        return registered;
    }
    framework::Uuid cache_region_id() override {
        return id;
    }
};
}
std::shared_ptr<AssetService>
make_grid_asset_service(const server::IniConfig &config, std::shared_ptr<network::WebHttpClients> clients,
                        std::function<bool(std::function<void()>)> schedule) {
    return std::make_shared<GridAssetService>(config, std::move(clients), std::move(schedule));
}

struct RegionAssetRuntime::State {
    std::shared_ptr<FlotsamConsoleBackend> cache_dates;
    std::shared_ptr<framework::ObjectJobEngine> cache_console_jobs;
    std::shared_ptr<FlotsamCacheMaintenance> cache_maintenance;
    std::shared_ptr<Backend> backend;
    std::shared_ptr<Scene> scene;
    std::shared_ptr<LocalAssetConnector> connector;
    std::shared_ptr<GetAssetsModule> module;
    std::shared_ptr<FlotsamCacheModule> cache_module;
    std::shared_ptr<CacheBackend> cache_backend;
    std::shared_ptr<FlotsamInstanceConfig> cache_config;
    NativeFlotsamContainers cache_containers;
    std::shared_ptr<std::vector<FlotsamConfigCommand>> cache_commands =
        std::make_shared<std::vector<FlotsamConfigCommand>>();
    std::shared_ptr<AssetsModuleState> module_state = std::make_shared<AssetsModuleState>();
    std::shared_ptr<network::WebHttpClients> foreign_http;
    AssetExchangePermissions exchange_permissions;
    std::atomic<bool> stopped = false;
    void quiesce_cache_cleanup() {
        if (cache_console_jobs)
            cache_console_jobs->dispose();
        if (cache_backend && cache_module)
            cache_backend->quiesce(cache_module->lifecycle());
        if (cache_console_jobs)
            while (cache_console_jobs->active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void stop() {
        if (stopped.exchange(true))
            return;
        std::exception_ptr first;
        auto attempt = [&](auto action) {
            try {
                action();
            } catch (...) {
                if (!first)
                    first = std::current_exception();
            }
        };
        if (scene)
            scene->stopping = true;
        quiesce_cache_cleanup();
        if (module && scene && int32_t(module_state->scenes) > 0)
            attempt([&] { module->remove_region(scene); });
        // Handler waits for connector callbacks: callback pool and database stay live
        // until the asset request workers have joined.
        auto worker = std::dynamic_pointer_cast<framework::ObjectJobEngine>(module_state->worker.load());
        if (module)
            attempt([&] { module->close(); });
        if (worker)
            while (worker->active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::shared_ptr<framework::ObjectJobEngine> file_worker;
        if (cache_module)
            file_worker = std::dynamic_pointer_cast<framework::ObjectJobEngine>(
                cache_module->shared_state()->worker.load());
        if (cache_module && scene)
            attempt([&] { cache_module->remove_region(scene); });
        if (cache_module)
            attempt([&] { cache_module->close(); });
        if (file_worker)
            while (file_worker->active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (connector && scene)
            attempt([&] { connector->remove_region(scene); });
        if (backend) {
            for (auto* queue : {&backend->local_reads, &backend->remote_reads})
                attempt([&] { queue->dispose(); });
            for (auto* queue : {&backend->local_reads, &backend->remote_reads})
                while (queue->active_threads() != 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (connector)
            attempt([&] { connector->close(); });
        if (backend) {
            attempt([&] { backend->callbacks.dispose(); });
            while (backend->callbacks.active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (backend && backend->local_service && backend->local_service->database)
            attempt([&] { backend->local_service->database->dispose(); });
        module_state->handler.store(nullptr);
        module_state->service.store(nullptr);
        if (scene)
            scene->registered.reset();
        cache_module.reset();
        cache_backend.reset();
        module.reset();
        connector.reset();
        scene.reset();
        backend.reset();
        if (first)
            std::rethrow_exception(first);
    }
    ~State() {
        try {
            stop();
        } catch (...) {
        }
    }
};
RegionAssetRuntime::RegionAssetRuntime(const server::IniConfig &config,
                                       std::shared_ptr<SceneCapabilityEvents> events,
                                       std::optional<std::string> region_name,
                                       std::function<void(std::string)> log, framework::Uuid region_id,
                                       StructuredLog structured_log,
                                       std::shared_ptr<network::WebHttpClients> http,
                                       AssetServerLookup foreign_server, GatherSceneAssets gather_assets)
    : state_(std::make_unique<State>()) {
    if (!events)
        throw std::invalid_argument("Null region asset capability events");
    std::shared_ptr<framework::IAssetCache> cache;
    std::shared_ptr<FlotsamFileStore> file_store;
    std::shared_ptr<FlotsamFileAccess> file_access;
    FlotsamCacheModule::Options cache_options;
    if (config.get("Modules", "AssetCaching") == "FlotsamAssetCache") {
        auto typed_source = native_flotsam_config_source(
            std::shared_ptr<const server::IniConfig>(&config, [](const server::IniConfig *) {}));
        state_->cache_dates = std::make_shared<FlotsamConsoleBackend>();
        bind_native_invariant_flotsam_datetime(*state_->cache_dates);
        state_->cache_config = std::make_shared<FlotsamInstanceConfig>();
        auto common = shared_cache_configuration();
        auto logger = [log, structured_log](const auto &text) {
            if (structured_log)
                structured_log("normal", text);
            else if (log)
                log(text);
        };
        state_->cache_containers = native_flotsam_cache_containers(
            state_->cache_config, common, {}, true, logger, logger,
            [commands = state_->cache_commands](const auto &command) { commands->push_back(command); },
            shared_cache_configuration_gate());
        std::lock_guard configuration_lock(*state_->cache_containers.gate);
        FlotsamCacheConfig initializer(state_->cache_config, common, state_->cache_containers.backend);
        initializer.initialise(typed_source);
        const auto& cache_config = *state_->cache_config;
        if (cache_config.file && cache_config.expiration_ticks > 0 && cache_config.cleanup_ticks > 0 &&
            !gather_assets)
            throw std::invalid_argument(
                "Native Flotsam automatic file expiration requires live scene asset gathering");
        auto settings = state_->cache_containers.entries->live_options();
        auto warn = [log, structured_log](const auto &text) {
            if (structured_log)
                structured_log("warn", text);
            else if (log)
                log(text);
        };
        const auto access_tracker = [common, gate = state_->cache_containers.gate] {
            std::lock_guard lock(*gate);
            return std::static_pointer_cast<FlotsamKeyEntries>(common->access_entries);
        };
        file_access = std::make_shared<FlotsamFileAccess>(
            std::make_shared<LiveCacheAccessEntries>(access_tracker), native_flotsam_access_time_setter());
        const auto file_options = [instance = state_->cache_config, common,
                                   gate = state_->cache_containers.gate] {
            std::lock_guard lock(*gate);
            return FlotsamFileStore::Options{
                std::filesystem::path(framework::utf16_replacing(instance->directory.value_or(""))),
                common->tiers, common->tier_length, common->update_access};
        };
        std::shared_ptr<FlotsamMemoryCache::FileOperations> files;
        if (cache_config.file) {
            const auto shared = shared_flotsam_cache_state();
            const auto disk = native_flotsam_shared_file_backend(shared, warn);
            attach_flotsam_file_access(disk, file_access);
            FlotsamFileStore::Options paths{
                std::filesystem::path(framework::utf16_replacing(cache_config.directory.value_or(""))),
                common->tiers, common->tier_length, common->update_access};
            state_->cache_maintenance = std::make_shared<FlotsamCacheMaintenance>(
                paths.directory, [logger, warn](const std::string& level, const std::string& message) {
                    if (level == "warn")
                        warn(message);
                    else
                        logger(message);
                });
            file_store = std::make_shared<FlotsamFileStore>(std::move(paths), disk, shared->files, file_options);
            files = flotsam_cache_files(file_store, disk, logger, warn);
        } else {
            files = std::make_shared<FlotsamMemoryCache::FileOperations>();
            files->debug = logger;
            files->warn = warn;
            // Another initialization can enable the static access tracker.
            files->touch = [file_options, file_access](const std::string& id) {
                FlotsamFileStore names(file_options(), native_flotsam_file_backend({}));
                file_access->touch(names.file_name(id));
            };
        }
        cache = std::make_shared<FlotsamMemoryCache>(settings, state_->cache_containers.entries, files);
        cache_options.enabled = state_->cache_config->enabled;
        cache_options.file = state_->cache_config->file;
        cache_options.expiration_ticks = state_->cache_config->expiration_ticks;
        cache_options.cleanup_ticks = state_->cache_config->cleanup_ticks;
        cache_options.loader = state_->cache_config->loader;
        cache_options.loader_arguments = state_->cache_config->loader_arguments.value_or("");
    }
    state_->backend = std::make_shared<Backend>(std::move(log), std::move(structured_log));
    state_->backend->http = http;
    state_->scene = std::make_shared<Scene>(std::move(events), std::move(region_name), region_id);
    if (cache) {
        state_->cache_backend = std::make_shared<CacheBackend>(state_->backend->storage);
        state_->cache_module = std::make_shared<FlotsamCacheModule>(std::move(cache_options),
                                                                    std::move(cache), state_->cache_backend);
        state_->cache_module->post_initialise();
        state_->cache_module->add_region(state_->scene);
    }
    const bool region_connector = config.get("Modules", "AssetServices") == "RegionAssetConnector";
    state_->connector = std::make_shared<LocalAssetConnector>(state_->backend, region_connector);
    if (region_connector) {
        const auto module = config.get("AssetService", "LocalGridAssetService");
        if (module.empty())
            throw std::invalid_argument("RegionAssetConnector requires AssetService.LocalGridAssetService");
        const auto foreign = config.get("AssetService", "HypergridAssetService");
        if (!foreign.empty()) {
            if (foreign != "OpenSim.Services.Connectors.dll:HGAssetServiceConnector" || !http)
                throw std::invalid_argument("Unsupported Hypergrid asset service: " + foreign);
            state_->foreign_http = std::move(http);
            state_->exchange_permissions = AssetExchangePermissions(config, "HGAssetService");
        }
        state_->connector->initialise_service(&config, module);
        state_->connector->foreign_url_fetch([this](const auto& value) -> AssetService::Asset {
            const auto target = foreign_asset_target(value);
            if (!target)
                return {};
            const auto key = target->id.str();
            auto asset = state_->connector->get_cached(key);
            if (!asset)
                asset = state_->backend->service->get_asset(key);
            if (!asset && state_->foreign_http)
                asset = GridAssetService(target->url, state_->foreign_http).get_asset(key);
            if (!asset || !state_->exchange_permissions.allows_import(asset->type()))
                return {};
            state_->connector->store(asset);
            return asset;
        });
    } else {
        state_->connector->initialise(&config);
    }
    if (state_->foreign_http) {
        state_->connector->foreign_fetch([this](const auto &id, const auto &server, bool local) {
            framework::Uuid asset;
            return framework::try_parse_sdk_uuid(id, asset) ? get_foreign(asset, server, local) : nullptr;
        });
        state_->connector->foreign_store([http = state_->foreign_http, permissions = state_->exchange_permissions]
                                          (const auto& asset) -> std::optional<std::string> {
            if (!permissions.allows_export(asset->type()))
                return std::string{};
            const auto target = foreign_asset_target(asset->id());
            if (!target)
                return std::string{};
            asset->full_id(target->id);
            return GridAssetService(target->url, http).store(asset);
        });
        state_->connector->foreign_exists([http = state_->foreign_http](const auto& ids) {
            struct Batch {
                std::string url;
                std::vector<std::string> ids;
                std::vector<std::size_t> positions;
            };
            std::vector<Batch> batches;
            std::unordered_map<std::string, std::size_t> by_url;
            for (std::size_t index = 0; index < ids.size(); ++index) {
                const auto target = foreign_asset_target(ids[index]);
                if (!target)
                    continue;
                const auto [group, inserted] = by_url.try_emplace(target->url, batches.size());
                if (inserted)
                    batches.push_back({target->url, {}, {}});
                auto& batch = batches[group->second];
                batch.ids.push_back(target->id.str());
                batch.positions.push_back(index);
            }
            std::vector<bool> result(ids.size());
            for (const auto& batch : batches) {
                const auto flags = GridAssetService(batch.url, http).assets_exist(batch.ids);
                for (std::size_t index = 0; index < batch.positions.size(); ++index)
                    result[batch.positions[index]] = flags.at(index);
            }
            return result;
        });
    }
    if (state_->foreign_http && foreign_server)
        state_->scene->directory = std::make_shared<AssetCircuitDirectory>(std::move(foreign_server));
    state_->connector->add_region(state_->scene);
    if (!state_->scene->registered)
        throw std::invalid_argument(
            "Region assets require configured LocalAssetServicesConnector and local AssetService");
    state_->connector->region_loaded(state_->scene);
    if (state_->cache_module) {
        const auto life = state_->cache_module->lifecycle();
        state_->cache_console_jobs = std::make_shared<framework::ObjectJobEngine>(
            [](std::any work) { std::any_cast<std::function<void()>>(std::move(work))(); },
            "FlotsamConsole", 1000, 1,
            [backend = state_->backend](const auto& job, const auto& error) { backend->job_error(job, error); });
        state_->cache_dates->post = [queue = std::weak_ptr(state_->cache_console_jobs),
                                     cache = std::weak_ptr(state_->cache_backend), life](
                                        std::function<void()> operation, const std::string& name) {
            const auto worker = queue.lock();
            const bool scan = name == "TouchAllSceneAssets" || name == "ExpireSceneAssets";
            if (!worker || !worker->enqueue(std::function<void()>(
                    [operation = std::move(operation), cache, life, scan] {
                        const bool resume_timer = scan && life->timer_running;
                        try {
                            operation();
                        } catch (...) {
                            if (scan) {
                                const auto owner = cache.lock();
                                std::lock_guard lock(life->timer_mutex);
                                life->cleanup_running = false;
                                if (owner && !owner->stopping && resume_timer && !life->timer_running) {
                                    life->timer->start();
                                    life->timer_running = true;
                                }
                            }
                            throw;
                        }
                    })))
                throw std::runtime_error("Flotsam console is stopping");
        };
    }
    if (gather_assets && state_->cache_maintenance) {
        const auto life = state_->cache_module->lifecycle();
        const auto log_cache = [backend = state_->backend](const std::string& level, const std::string& message) {
            backend->emit(level, message);
        };
        state_->cache_backend->cleanup_warning = [log_cache](const auto& message) { log_cache("warn", message); };
        const auto inspectors = scene::native_uuid_gatherer_backend(
            state_->scene->registered, [log_cache](const auto& message) { log_cache("error", message); },
            [log_cache](const auto& message) { log_cache("debug", message); });
        auto gather_scene = [gather = std::move(gather_assets), inspectors,
                             maintenance = state_->cache_maintenance, region_id] {
            scene::UuidGatherer gatherer(inspectors);
            gather(gatherer);
            gatherer.gather_all();
            maintenance->stamp(region_id);
            return gatherer.state();
        };
        auto protected_assets = [gather_scene] {
            FlotsamProtectedAssets assets;
            const auto gathered = gather_scene();
            for (const auto& [id, type] : gathered->gathered)
                assets.insert(id);
            return assets;
        };
        const auto memory_cache = std::static_pointer_cast<FlotsamMemoryCache>(state_->scene->cache);
        const auto& settings = *state_->cache_config;
        const auto common = shared_cache_configuration();
        int warn_at;
        {
            std::lock_guard lock(*state_->cache_containers.gate);
            warn_at = common->warn_at;
        }
        state_->cache_backend->cleaner = std::make_shared<FlotsamCacheCleaner>(
            FlotsamCacheCleaner::Options{
                std::filesystem::path(framework::utf16_replacing(settings.directory.value_or(""))),
                settings.expiration_ticks, warn_at},
            life, state_->cache_module->shared_state(),
            FlotsamCacheCleaner::native_backend(memory_cache, std::move(protected_assets), cache_heap_bytes,
                                                log_cache));
        state_->cache_dates->sleep = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
        state_->cache_dates->clean = [backend = std::weak_ptr(state_->cache_backend)](auto date) {
            if (const auto owner = backend.lock())
                owner->expire(date);
        };
        auto maintenance = std::make_shared<FlotsamCacheMaintenance>(
            file_store->directory(), FlotsamCacheMaintenance::native_backend(
                file_store, file_access, state_->scene->registered, std::move(gather_scene),
                [life = std::weak_ptr(life)] {
                    const auto active = life.lock();
                    return active && active->cleanup_running;
                }, log_cache));
        state_->cache_dates->touch = [backend = std::weak_ptr(state_->cache_backend), maintenance](bool fetch) {
            if (const auto owner = backend.lock())
                return owner->touch(*maintenance, fetch);
            return std::int32_t{0};
        };
    }
    if (state_->cache_module)
        state_->cache_module->region_loaded(state_->scene);
    state_->module = std::make_shared<GetAssetsModule>(state_->backend, state_->module_state);
    state_->module->initialise(&config);
    state_->module->add_region(state_->scene);
    state_->module->region_loaded(state_->scene);
}
std::shared_ptr<AssetService> RegionAssetRuntime::asset_service() const {
    if (!state_ || state_->stopped || !state_->scene || !state_->scene->registered)
        throw std::runtime_error("Region asset service stopped");
    return state_->scene->registered;
}
void RegionAssetRuntime::register_filesystem_console(
    framework::ConsoleCommandResolver &commands, const std::function<void(const std::string &)> &output) const {
    if (state_ && state_->backend)
        register_fs_asset_console(state_->backend->service, commands, output);
}
std::shared_ptr<framework::AssetBase> RegionAssetRuntime::get(const framework::Uuid &id) {
    return asset_service()->get_asset(id.str());
}
std::shared_ptr<framework::AssetBase>
RegionAssetRuntime::get_foreign(const framework::Uuid &id, const std::optional<std::string> &server,
                                bool store_local) {
    if (!state_ || state_->stopped || !state_->scene || !state_->scene->registered)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->foreign_http)
        return state_->scene->registered->get_foreign(id.str(), server, store_local);
    auto cache = state_->scene->cache;
    if (cache)
        if (auto cached = cache->get_cached(id.str()))
            return cached;
    auto asset = state_->backend->service->get_asset(id.str());
    if (!asset && server && !server->empty()) {
        framework::HttpHost remote(*server, true);
        if (remote.resolved())
            asset = request_remote_asset(*state_->foreign_http, remote.uri, id);
        if (asset) {
            if (!state_->exchange_permissions.allows_import(asset->type()))
                asset.reset();
            if (asset && store_local)
                state_->backend->service->store(asset);
        }
    }
    if (cache) {
        if (asset)
            cache->cache(asset);
        else
            cache->cache_negative(id.str());
    }
    return asset;
}
std::optional<std::string> RegionAssetRuntime::store(std::shared_ptr<framework::AssetBase> asset) {
    return asset_service()->store(std::move(asset));
}
bool RegionAssetRuntime::default_assets_command(bool remove) {
    if (!state_ || state_->stopped)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->cache_module)
        return false;
    auto logger = [backend = state_->backend](const std::string &text) {
        backend->emit("normal", text);
    };
    if (remove)
        state_->cache_module->delete_default_assets(logger);
    else
        state_->cache_module->load_default_assets(logger);
    return true;
}
std::vector<std::string> RegionAssetRuntime::cache_command_names() const {
    std::vector<std::string> names;
    if (state_ && !state_->stopped)
        for (const auto &command : *state_->cache_commands)
            names.push_back(command.command);
    return names;
}
std::vector<framework::ConsoleCommandDescription> RegionAssetRuntime::cache_command_descriptions() const {
    std::vector<framework::ConsoleCommandDescription> commands;
    if (state_ && !state_->stopped)
        for (const auto &command : *state_->cache_commands)
            commands.push_back(
                {command.category, command.command, command.usage, command.description, "", command.shared});
    return commands;
}
bool RegionAssetRuntime::cache_console(const std::vector<std::string> &args,
                                       std::function<void(const std::string &)> output) {
    if (!state_ || state_->stopped)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->cache_module || args.size() < 2)
        return false;
    if (!output)
        throw std::invalid_argument("Missing cache console output");
    const auto &action = args[1];
    if (action == "cachedefaultassets" || action == "deletedefaultassets")
        return default_assets_command(action == "deletedefaultassets");
    auto cache = std::dynamic_pointer_cast<FlotsamMemoryCache>(state_->scene->cache);
    if (!cache)
        throw std::runtime_error("Flotsam cache unavailable");
    auto settings = cache->settings();
    struct Output final : FlotsamConsoleOutput {
        std::function<void(const std::string &)> sink;
        explicit Output(std::function<void(const std::string &)> value) : sink(std::move(value)) {}
        void output(const std::string &value) override {
            sink(value);
        }
    };
    if (action == "assets") {
        if (!state_->cache_dates->touch)
            return false;
        FlotsamCacheConsole::touch_assets(state_->cache_module->lifecycle(), state_->cache_dates,
                                          std::make_shared<Output>(std::move(output)));
        return true;
    }
    if (action == "expire") {
        // Manual expiration also requires live scene asset gathering.
        if (settings.file && !state_->cache_backend->cleaner)
            throw std::runtime_error("Flotsam file expiration requires live scene asset gathering");
        auto arguments = std::make_shared<FlotsamCacheConsole::Arguments>();
        for (const auto &value : args)
            arguments->emplace_back(value);
        FlotsamCacheConsole::expire(arguments, state_->cache_config, state_->cache_module->lifecycle(),
                                    state_->cache_dates, std::make_shared<Output>(std::move(output)));
        return true;
    }
    if (action == "status") {
        auto backend = std::make_shared<FlotsamConsoleBackend>();
        backend->post = state_->cache_dates->post;
        backend->memory_count = [cache] { return cache->status().memory_entries; };
        backend->hit_report = [cache] { return cache->hit_report(); };
        if (settings.file) {
            backend->file_count = [maintenance = state_->cache_maintenance](const auto& directory) {
                return maintenance->count(std::filesystem::path(framework::utf16_replacing(directory.value_or(""))));
            };
            bind_flotsam_console_files(*backend);
            backend->format_date = [](auto date, const std::string&) {
                return framework::format_invariant_status_date(date);
            };
        }
        FlotsamCacheConsole::status(state_->cache_config, state_->cache_module->lifecycle(), backend,
                                    std::make_shared<Output>(std::move(output)));
        return true;
    }
    if (action == "clearnegatives") {
        if (settings.negative)
            output("Flotsam cache of negatives cleared (" + std::to_string(cache->clear_negatives()) +
                   " entries)");
        else
            output("Flotsam cache of negatives not enabled");
        return true;
    }
    if (action == "clear") {
        bool memory = args.size() == 2, file = memory;
        for (auto value : args) {
            for (auto &c : value)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            if (value == "memory")
                memory = true;
            else if (value == "file")
                file = true;
        }
        if (memory) {
            if (settings.memory) {
                cache->clear_memory();
                output("Memory cache cleared.");
            } else
                output("Memory cache not enabled.");
        }
        if (file) {
            if (settings.file) {
                state_->cache_maintenance->clear_files();
                output("File cache cleared.");
            } else {
                output("File cache not enabled.");
            }
        }
        if (settings.negative)
            cache->clear_negatives();
        return true;
    }
    return false;
}
RegionAssetRuntime::~RegionAssetRuntime() = default;
std::shared_ptr<framework::IAssetCache> RegionAssetRuntime::asset_cache() const {
    return state_ && state_->scene ? state_->scene->cache : std::shared_ptr<framework::IAssetCache>{};
}
void RegionAssetRuntime::stop() {
    state_->stop();
}
void RegionAssetRuntime::quiesce_cache_cleanup() {
    state_->quiesce_cache_cleanup();
}
}
