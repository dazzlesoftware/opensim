// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/asset_endpoint.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include <pugixml.hpp>
#include <sstream>

namespace opensim::server {
namespace {
constexpr std::string_view asset_path = "/assets";
constexpr std::string_view exists_path = "/get_assets_exist";

std::string xml(pugi::xml_document& document) {
    std::ostringstream output;
    document.save(output, "", pugi::format_raw);
    return output.str();
}

std::string boolean_xml(bool value) {
    return value ? "<boolean>true</boolean>" : "<boolean>false</boolean>";
}

std::vector<std::string> asset_parameters(std::string_view path) {
    std::vector<std::string> result;
    constexpr std::string_view separators = "/?&";
    for (auto start = path.find_first_not_of(separators); start != std::string_view::npos;) {
        const auto end = path.find_first_of(separators, start);
        result.emplace_back(path.substr(start, end - start));
        start = path.find_first_not_of(separators, end);
    }
    return result;
}
}

AssetEndpoint::AssetEndpoint(std::shared_ptr<services::AssetService> service, const IniConfig& config,
                             const std::string& section)
    : service_(std::move(service)), auth_(config, section),
      redirect_(config.get(section, "RedirectURL")),
      allow_delete_(config.boolean(section, "AllowRemoteDelete", false)),
      allow_delete_all_(config.boolean(section, "AllowRemoteDeleteAllTypes", false)) {}

bool AssetEndpoint::matches(const network::HttpRequest& request) {
    return request.path == exists_path || request.path == asset_path || request.path.starts_with("/assets/");
}

network::HttpResponse AssetEndpoint::handle(const network::HttpRequest& request) {
    network::HttpResponse response;
    response.content_type = "text/plain";
    if (request.path == exists_path) {
        if (request.method != "POST") {
            response.status = 405;
            response.headers["Allow"] = "POST";
            return response;
        }
        // The original connector registers this handler without service authentication.
        pugi::xml_document input;
        if (!input.load_buffer(request.body.data(), request.body.size(),
                               pugi::parse_default | pugi::parse_ws_pcdata)) {
            response.status = 400;
            return response;
        }
        const auto root = input.document_element();
        if (std::string_view(root.name()) != "ArrayOfString" || *root.attribute("xmlns").value()) {
            response.status = 400;
            return response;
        }
        std::vector<std::string> ids;
        for (const auto node : root.children("string")) {
            if (*node.attribute("xmlns").value())
                continue;
            auto& id = ids.emplace_back();
            for (const auto part : node.children()) {
                if (part.type() == pugi::node_pcdata || part.type() == pugi::node_cdata)
                    id += part.value();
                else if (part.type() == pugi::node_element) {
                    response.status = 400;
                    return response;
                }
            }
        }
        pugi::xml_document output;
        auto flags = output.append_child("ArrayOfBoolean");
        for (const bool present : service_->assets_exist(ids))
            flags.append_child("boolean").text().set(present);
        response.content_type = "text/xml";
        response.body = xml(output);
        return response;
    }
    if (request.method != "GET" && request.method != "POST" && request.method != "DELETE") {
        response.status = 405;
        response.headers["Allow"] = "GET, POST, DELETE";
        return response;
    }
    if (!auth_.authenticate(request, response))
        return response;

    const auto parameters = asset_parameters(std::string_view(request.path).substr(asset_path.size()));
    if (request.method == "POST") {
        const auto asset = services::deserialize_remote_asset(request.body);
        if (!asset) {
            response.status = 400;
            return response;
        }
        response.content_type = "text/xml";
        if (!parameters.empty()) {
            response.body = boolean_xml(service_->update_content(parameters.front(), asset->data));
        } else {
            const auto stored = service_->store(asset);
            pugi::xml_document document;
            auto result = document.append_child("string");
            if (stored)
                result.text().set(stored->c_str());
            else {
                result.append_attribute("xmlns:xsi") = "http://www.w3.org/2001/XMLSchema-instance";
                result.append_attribute("xsi:nil") = "true";
            }
            response.body = xml(document);
        }
        return response;
    }
    if (request.method == "DELETE") {
        bool deleted = false;
        if (allow_delete_ && !parameters.empty()) {
            const auto& id = parameters.front();
            const auto asset = service_->get_asset(id);
            if (asset && (allow_delete_all_ || (asset->flags() & framework::Maptile)))
                deleted = service_->erase(id);
        }
        response.content_type = "text/xml";
        response.body = boolean_xml(deleted);
        return response;
    }
    if (parameters.empty())
        return response;

    const auto& id = parameters.front();
    if (parameters.size() == 1) {
        if (const auto asset = service_->get_asset(id)) {
            response.content_type = "text/xml";
            response.body = services::serialize_remote_asset(*asset);
            return response;
        }
    } else if (parameters[1] == "data") {
        if (const auto bytes = service_->get_data(id)) {
            response.content_type = "application/octet-stream";
            response.body.assign(bytes->begin(), bytes->end());
            return response;
        }
    } else if (parameters[1] == "metadata") {
        if (const auto metadata = service_->get_metadata(id)) {
            response.content_type = framework::asset_content_type(metadata->type);
            response.body = services::serialize_remote_asset_metadata(*metadata);
            return response;
        }
    } else {
        response.status = 400;
        return response;
    }
    response.status = 404;
    if (!redirect_.empty()) {
        response.status = 302;
        // BaseHttpServer strips one trailing slash before dispatching the asset handler.
        const auto path = std::string_view(request.path);
        const auto length = path.ends_with('/') ? path.size() - 1 : path.size();
        response.headers["Location"] = redirect_ + request.path.substr(0, length);
    }
    return response;
}
}
