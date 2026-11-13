// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/inventory_asset_caps.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/network/osd_value.hpp"
namespace opensim::services {
namespace {
std::string escape(std::string_view s) {
    std::string out;
    for (char c : s)
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
        }
    return out;
}
std::string str(std::string_view s) {
    return s.empty() ? "<string />" : "<string>" + escape(s) + "</string>";
}
std::string error(std::string_view text, framework::Uuid identifier = {}) {
    return "<llsd><map><key>message</key>" + str(text) + "<key>identifier</key><uuid>" + identifier.str() +
           "</uuid></map></llsd>";
}
network::HttpResponse response() {
    network::HttpResponse result;
    result.content_type = "text/html;charset=UTF-8";
    return result;
}
}
InventoryAssetUpload::InventoryAssetUpload(framework::Uuid a, framework::Uuid i, framework::Uuid o,
                                           std::uint8_t t, std::string p, std::string ip,
                                           InventoryAssetCapsBackend b)
    : agent_(a), item_(i), object_(o), asset_type_(t), path_(std::move(p)), remote_ip_(std::move(ip)),
      backend_(std::move(b)) {
    if (backend_.timer)
        backend_.timer(path_, 30000);
}
InventoryAssetUpload::~InventoryAssetUpload() {
    try {
        expire();
    } catch (...) {
    }
}
bool InventoryAssetUpload::active() const {
    std::lock_guard lock(mutex_);
    return active_;
}
bool InventoryAssetUpload::stop() {
    {
        std::lock_guard lock(mutex_);
        if (!active_)
            return false;
        active_ = false;
    }
    if (backend_.remove_upload)
        backend_.remove_upload(path_);
    if (backend_.timer)
        backend_.timer(path_, 0);
    return true;
}
void InventoryAssetUpload::expire() {
    stop();
}
network::HttpResponse InventoryAssetUpload::handle(const network::HttpRequest &request) {
    auto result = response();
    if (request.method != "POST") {
        result.status = 404;
        return result;
    }
    if (request.body.empty()) {
        result.status = 400;
        return result;
    }
    if (request.body.size() > 10000000) {
        result.status = 413;
        return result;
    }
    try {
        if (!stop()) {
            if (backend_.remove_upload)
                backend_.remove_upload(path_);
            throw std::runtime_error("Expired uploader timer");
        }
        auto actual = framework::IpAddress::parse(request.remote_ip),
             expected = framework::IpAddress::parse(remote_ip_);
        if (!actual || !expected || actual->v6() != expected->v6() || actual->bytes() != expected->bytes() ||
            (actual->v6() && actual->scope_id() != expected->scope_id())) {
            result.status = 401;
            return result;
        }
        if (!backend_.update) {
            result.status = 410;
            return result;
        }
        // BunchOfCaps.ValidateAssetData always returns true, for every type/data.
        auto data =
            std::span(reinterpret_cast<const std::uint8_t *>(request.body.data()), request.body.size());
        auto asset = backend_.update(agent_, item_, object_, data);
        std::string body;
        if (asset.is_zero())
            body = error("Failed to update inventory item asset", item_);
        else
            body =
                "<llsd><map><key>new_asset</key>" + str(asset.str()) + "<key>new_inventory_item</key><uuid>" +
                item_.str() +
                "</uuid>"
                "<key>state</key><string>complete</string>"
                "<key>new_next_owner_mask</key><integer>0</integer>"
                "<key>new_group_mask</key><integer>0</integer>"
                "<key>new_everyone_mask</key><integer>0</integer>"
                "<key>inventory_item_flags</key><integer>0</integer>"
                "</map></llsd>";
        if (backend_.dump)
            backend_.dump(data);
        result.body = std::move(body);
        result.status = 200;
    } catch (...) {
        result.status = 500;
    }
    return result;
}
UpdateInventoryAssetCore::UpdateInventoryAssetCore(framework::Uuid a, std::string origin,
                                                   std::optional<std::uint8_t> type,
                                                   InventoryAssetCapsBackend b, std::string name)
    : agent_(a), origin_(std::move(origin)), type_(type), backend_(std::move(b)),
      region_name_(std::move(name)) {
    if (!backend_.object_exists || !backend_.can_edit_inventory || !backend_.new_path ||
        !backend_.register_upload || !backend_.remove_upload || !backend_.timer)
        throw std::invalid_argument("Incomplete inventory upload backend");
}
network::HttpResponse UpdateInventoryAssetCore::handle(const network::HttpRequest &request,
                                                       const network::Value &map) const {
    auto result = response();
    if (!type_)
        return result;
    if (backend_.log)
        backend_.log("[CAPS]: UpdateInventoryItemAsset Request in region: " + region_name_ + "\n");
    framework::Uuid item, object;
    try {
        if (map.contains("item_id"))
            item = network::osd_uuid(map.get("item_id"));
        if (map.contains("task_id"))
            object = network::osd_uuid(map.get("task_id"));
    } catch (...) {
    }
    if (item.is_zero()) {
        result.body = error("failed to recode request");
        return result;
    }
    if (!object.is_zero()) {
        if (!backend_.object_exists(object)) {
            result.body = error("object not found");
            return result;
        }
        if (!backend_.can_edit_inventory(object, agent_)) {
            result.body = error("No permissions to edit objec");
            return result;
        }
    }
    auto path = backend_.new_path();
    auto upload_backend = backend_;
    auto callback = backend_.update;
    upload_backend.update = [callback](auto a, auto i, auto o, auto data) {
        return callback ? callback(a, i, o, data) : framework::Uuid{};
    };
    auto upload = std::make_shared<InventoryAssetUpload>(agent_, item, object, *type_, path,
                                                         request.remote_ip, std::move(upload_backend));
    backend_.register_upload(upload);
    result.body =
        "<llsd><map><key>uploader</key>" + str(origin_ + path) +
        "<key>state</key><string>upload</string><key>upload_price</key><integer>0</integer></map></llsd>";
    return result;
}
UpdateInventoryAssetRoute::UpdateInventoryAssetRoute(std::string p,
                                                     std::shared_ptr<UpdateInventoryAssetCore> c,
                                                     Deserialize d)
    : path_(std::move(p)), core_(std::move(c)),
      deserialize_(d ? std::move(d) : FetchInventory2Route::deserialize_xml_json) {
    if (!core_)
        throw std::invalid_argument("Null inventory upload core");
}
network::HttpResponse UpdateInventoryAssetRoute::handle(const network::HttpRequest &request) {
    ++requests_received;
    auto result = response();
    if (request.method != "POST") {
        result.status = 404;
        return result;
    }
    network::Value map;
    try {
        map = deserialize_(request.body);
        if (map.view().kind != network::Value::Kind::Map)
            throw std::invalid_argument("Expected map");
    } catch (...) {
        result.status = 400;
        return result;
    }
    try {
        result = core_->handle(request, map);
    } catch (...) {
        result.status = 500;
    }
    ++requests_handled;
    return result;
}
}
