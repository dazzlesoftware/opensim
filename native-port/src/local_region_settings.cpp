// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "local_region_settings.hpp"

#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/directory_entries.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/server/config_xml_startup.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace opensim::server {

std::uint32_t read_region_uint32(const std::string &text, std::string_view key) {
    const auto number = framework::invariant_convert_to_int64(framework::utf16_replacing(text));
    if (number < 0 || number > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Region " + std::string(key) + " is out of range");
    return static_cast<std::uint32_t>(number);
}

namespace {

struct RegionConfigSection {
    IniConfig source;
    std::string name;
};

std::vector<RegionConfigSection> download_region_config(const std::string &url) {
    const auto uris = framework::native_config_uri_object_backend();
    const auto uri = uris->create(framework::nini_writer_text(framework::utf16_replacing(url)),
                                  framework::ConfigUriObjectKind::Absolute);
    if (*uri->scheme() != u"http" && *uri->scheme() != u"https")
        throw std::invalid_argument("Region configuration URL must use HTTP or HTTPS");
    network::WebHttpClients clients(network::native_web_http_handler_factory());
    clients.setup(false, false, nullptr, 32);
    framework::ConfigXmlHttpOperation request;
    request.method = "GET";
    auto stream = clients.get_new_global_client(30000)->send_headers(uri, std::move(request), true);
    const auto bytes = stream->copy_to_memory();
    stream->close();
    auto document = framework::decode_stream_reader_text(bytes).text;
    // The managed loader concatenates StreamReader.ReadLine() results.
    std::erase_if(document, [](char16_t ch) { return ch == u'\r' || ch == u'\n'; });
    framework::NiniXmlDom dom;
    framework::native_nini_xml_load(
        dom, framework::NativeNiniXmlReader::from_text(framework::nini_writer_text(std::move(document)),
                                                       framework::NiniXmlReaderSettings::legacy_document()));
    const auto root = dom.document_node()->first_child();
    if (!root || *root->name() != u"Nini")
        return {};

    auto arena = std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto readers = framework::native_nini_xml_url_reader_backend(
        uris, [](auto) -> std::shared_ptr<framework::NativeNiniXmlByteInput> {
            throw std::invalid_argument("External XML entities are unsupported in web region configuration");
        });
    auto xml = framework::native_nini_xml_config_backend(framework::native_xml_document_io(std::move(readers)));
    std::vector<RegionConfigSection> regions;
    for (auto child = root->first_child(); child; child = child->next_sibling()) {
        if (child->node_type() != 1)
            throw std::invalid_argument("Web region configuration contains a non-element child of Nini");

        // RegionInfo constructs a fresh XmlConfigSource from each child's OuterXml.
        auto fragment = std::make_shared<framework::NiniStringWriter>();
        framework::NativeXmlTextWriter writer(framework::native_xml_string_writer_io(fragment), true);
        native_xml_node_write_to(child, writer);
        writer.flush();
        auto wrapped = framework::nini_writer_text(u"<Nini>" + *fragment->to_string() + u"</Nini>");
        auto source = framework::NiniXmlConfigSource::create(
            arena, xml, framework::NativeNiniXmlReader::from_text(std::move(wrapped)));
        const auto name = child->attribute(framework::nini_writer_text(u"Name"));
        regions.push_back({server::IniConfig::from_native_source(std::move(source)),
                           name ? framework::utf8(*name->value()) : std::string{}});
    }
    return regions;
}

std::vector<RegionConfigSection> discover_region_sections(const IniConfig &config) {
    const auto configured_directory = framework::trim_utf8(config.get("Startup", "regionload_regionsdir"));
    const bool explicit_directory = !configured_directory.empty();
    const bool allow_regionless = config.boolean("Startup", "allow_regionless", false);
    const bool from_web = config.get("Startup", "region_info_source", "filesystem") != "filesystem";
    std::vector<RegionConfigSection> regions;
    const auto append_sections = [&](const server::IniConfig &source, bool first_only) {
        const auto sections = source.native_source()->configs();
        const auto count = first_only ? std::min(sections->count(), 1) : sections->count();
        for (int index = 0; index < count; ++index)
            regions.push_back({source, framework::utf8(*sections->at(index)->name())});
    };
    if (from_web) {
        const auto url = framework::trim_utf8(config.get("Startup", "regionload_webserver_url"));
        if (url.empty())
            throw std::invalid_argument("Missing regionload_webserver_url");
        for (int attempt = 0; attempt < 3; ++attempt) {
            try {
                regions = download_region_config(url);
            } catch (const framework::ConfigXmlHttpError &error) {
                if (allow_regionless && error.status == 404)
                    return {};
                throw;
            }
            if (!regions.empty())
                return regions;
            if (allow_regionless)
                return {};
            if (attempt < 2)
                std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        throw std::invalid_argument("Web region configuration returned no regions");
    }

    auto directory = framework::utf8_path(explicit_directory ? configured_directory : "Regions");
    if (directory.is_relative())
        directory = config.directory / directory;
    if ((!explicit_directory || allow_regionless) && !std::filesystem::exists(directory))
        return {};

    const auto directory_text = framework::nini_writer_text(directory.u16string());
    const auto ini_files = framework::directory_get_files(directory_text, framework::nini_writer_text(u"*.ini"));
    const auto xml_files = framework::directory_get_files(directory_text, framework::nini_writer_text(u"*.xml"));
    for (const auto &file : ini_files)
        append_sections(server::IniConfig::load(std::filesystem::path(*file)), false);

    if (!xml_files.empty()) {
        auto arena = std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
        const auto load_xml = server::native_startup_xml_source(std::move(arena), {});
        for (const auto &file : xml_files) {
            const auto path = std::filesystem::path(*file);
            const auto source = server::IniConfig::from_native_source(load_xml(file), path.parent_path());
            append_sections(source, true);
        }
    }
    if (regions.empty()) {
        if (allow_regionless || (!explicit_directory && ini_files.empty() && xml_files.empty()))
            return {};
        throw std::invalid_argument("No region configurations found in " + framework::utf8(directory.u16string()));
    }
    return regions;
}

IniConfig make_region_settings(const IniConfig &config, const RegionConfigSection *definition) {
    auto arena = std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto settings = server::IniConfig::from_native_source(
        arena->create_source(framework::nini_writer_text(u"RegionSettings")), config.directory);
    for (const auto &[key, value] : config.ordered_section("NativeRegion"))
        settings.set("NativeRegion", key, value);
    if (!definition)
        return settings;

    const auto &[region_config, region_name] = *definition;
    const auto id_text = framework::trim_utf8(region_config.get(region_name, "RegionUUID"));
    framework::Uuid region_id;
    if (!framework::try_parse_sdk_uuid(id_text, region_id) || region_id.is_zero())
        throw std::invalid_argument("Region " + region_name + " requires a valid RegionUUID");
    settings.set("NativeRegion", "ID", region_id.str());

    const auto location = region_config.get(region_name, "Location");
    const auto separator = location.find(',');
    if (separator == std::string::npos)
        throw std::invalid_argument("Region Location must contain two grid coordinates");
    const auto suffix = location.find(',', separator + 1);
    const auto parse_coordinate = [](std::string_view coordinate) {
        return std::to_string(read_region_uint32(std::string(coordinate), "Location coordinate"));
    };
    settings.set("NativeRegion", "LocationX", parse_coordinate(std::string_view(location).substr(0, separator)));
    const auto y_end = suffix == std::string::npos ? location.size() : suffix;
    settings.set("NativeRegion", "LocationY",
                 parse_coordinate(std::string_view(location).substr(separator + 1, y_end - separator - 1)));
    settings.set("NativeRegion", "Name", region_name);

    const auto read_size = [&](const char *key) {
        return read_region_uint32(region_config.get(region_name, key, "256"), key);
    };
    const auto size_x = read_size("SizeX");
    const auto size_y = read_size("SizeY");
    const auto size_z = read_region_uint32(region_config.get(region_name, "SizeZ", "4096"), "SizeZ");
    const auto size_x_cells = std::max(1u, size_x / 256);
    const auto size_y_cells = std::max(1u, size_y / 256);
    const auto size = std::min({size_x_cells, size_y_cells, 16u}) * 256;
    settings.set("NativeRegion", "SizeX", std::to_string(size));
    settings.set("NativeRegion", "SizeY", std::to_string(size));
    settings.set("NativeRegion", "SizeZ", std::to_string(size_z));

    for (const auto &[source, target] : {std::pair{"InternalPort", "UDPPort"},
                                         std::pair{"ExternalHostName", "ExternalHost"},
                                         std::pair{"DefaultLanding", "DefaultLanding"},
                                         std::pair{"MaxAgents", "MaxAgents"},
                                         std::pair{"MaxPrims", "MaxPrims"},
                                         std::pair{"ResolveAddress", "ResolveAddress"},
                                         std::pair{"NonPhysicalPrimMin", "NonPhysicalPrimMin"},
                                         std::pair{"NonPhysicalPrimMax", "NonPhysicalPrimMax"},
                                         std::pair{"PhysicalPrimMin", "PhysicalPrimMin"},
                                         std::pair{"PhysicalPrimMax", "PhysicalPrimMax"},
                                         std::pair{"LinksetPrims", "LinksetPrims"},
                                         std::pair{"ClampPrimSize", "ClampPrimSize"},
                                         std::pair{"RegionType", "RegionType"},
                                         std::pair{"MaxPrimsPerUser", "MaxPrimsPerUser"},
                                         std::pair{"TerrainDatabase", "TerrainDatabase"},
                                         std::pair{"EstateDatabase", "EstateDatabase"},
                                         std::pair{"Physics", "Physics"},
                                         std::pair{"PhysicsLibrary", "PhysicsLibrary"},
                                         std::pair{"MaptileStaticUUID", "MaptileStaticUUID"},
                                         std::pair{"MaptileStaticFile", "MaptileStaticFile"}}) {
        if (region_config.contains(region_name, source))
            settings.set("NativeRegion", target, region_config.get(region_name, source));
    }
    if (region_config.contains(region_name, "ScopeID")) {
        const auto scope_text = framework::utf16_replacing(region_config.get(region_name, "ScopeID"));
        const auto scope = framework::construct_sdk_uuid(scope_text);
        settings.set("NativeRegion", "ScopeID", scope.str());
    }
    if (region_config.contains(region_name, "InternalAddress")) {
        const auto address = framework::IpAddress::parse(region_config.get(region_name, "InternalAddress"));
        if (!address)
            throw std::invalid_argument("Region " + region_name + " has an invalid InternalAddress");
        settings.set("NativeRegion", "BindAddress", address->str());
    }
    for (const auto &[key, value] : region_config.ordered_section(region_name)) {
        for (const auto *permission : {"auto_grant_attach_perms", "auto_grant_all_perms"}) {
            if (framework::ordinal_equals_ignore_case(key, permission))
                settings.set("NativeRegion", permission, value);
        }
    }
    return settings;
}

} // namespace

std::vector<IniConfig> load_local_region_settings(const IniConfig &config) {
    if (config.contains("NativeServer", "EnableLocalRegion")) {
        if (config.boolean("NativeServer", "EnableLocalRegion", false))
            return {make_region_settings(config, nullptr)};
        return {};
    }

    const auto definitions = discover_region_sections(config);
    std::vector<IniConfig> settings;
    settings.reserve(definitions.size());
    for (const auto &definition : definitions)
        settings.push_back(make_region_settings(config, &definition));
    return settings;
}

services::GridRegion describe_local_region(const IniConfig &region_settings) {
    services::GridRegion region;
    region.id = framework::Uuid(region_settings.get("NativeRegion", "ID", "11111111-1111-4111-8111-111111111111"));
    region.scope =
        framework::Uuid(region_settings.get("NativeRegion", "ScopeID", "00000000-0000-0000-0000-000000000000"));
    region.name = region_settings.get("NativeRegion", "Name", "Native Region");
    region.external_host = region_settings.get("NativeRegion", "ExternalHost", "127.0.0.1");
    if (*region.external_host == "SYSTEMIP")
        region.external_host = framework::HostResolver::local_host_address()->str();
    else if (region_settings.boolean("NativeRegion", "ResolveAddress", false)) {
        const auto addresses = framework::HostResolver::system_lookup(*region.external_host);
        if (addresses && addresses->size() == 1)
            region.external_host = addresses->front()->str();
    }
    region.estate_owner =
        framework::Uuid(region_settings.get("NativeRegion", "OwnerID", "00000000-0000-0000-0000-000000000000"));
    const auto read_coordinate = [&](const char *key) {
        return read_region_uint32(region_settings.get("NativeRegion", key, "1000"), key);
    };
    const auto coordinates =
        services::GridRegion::from_cells(read_coordinate("LocationX"), read_coordinate("LocationY"));
    region.x = coordinates.x;
    region.y = coordinates.y;
    const auto size_x = region_settings.integer("NativeRegion", "SizeX", 256);
    const auto size_y = region_settings.integer("NativeRegion", "SizeY", 256);
    const auto size = std::clamp(std::min(size_x, size_y) / 256, 1, 16) * 256;
    region.size_x = size;
    region.size_y = size;
    region.access = 13;
    const auto udp_port = region_settings.integer("NativeRegion", "UDPPort", 9000);
    if (udp_port < 0 || udp_port > 65535)
        throw std::invalid_argument("NativeRegion UDPPort must be between 0 and 65535");
    region.internal_endpoint = services::RegionEndpoint{region_settings.get("NativeRegion", "BindAddress", "0.0.0.0"),
                                                        static_cast<std::uint16_t>(udp_port)};
    return region;
}

} // namespace opensim::server
