// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_new.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include <algorithm>
#include <charconv>
#include <cmath>
namespace opensim::scene {
namespace {
std::string invariant(float value) {
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Infinity" : "Infinity";
    char buffer[128];
    auto encoded = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    if (encoded.ec != std::errc{})
        throw std::runtime_error("Landmark float encoding failed");
    std::string text(buffer, encoded.ptr);
    auto at = text.find('e');
    auto exponent = std::stoi(text.substr(at + 1));
    if (exponent >= -4 && exponent < 9) {
        auto digits = text.substr(0, at);
        std::string sign;
        if (digits.front() == '-') {
            sign = "-";
            digits.erase(0, 1);
        }
        std::erase(digits, '.');
        auto point = exponent + 1;
        if (point <= 0)
            return sign + "0." + std::string(-point, '0') + digits;
        if (static_cast<std::size_t>(point) >= digits.size())
            return sign + digits + std::string(point - digits.size(), '0');
        return sign + digits.substr(0, point) + "." + digits.substr(point);
    }
    text[at] = 'E';
    return text;
}
}
InventoryNewLandmark generate_inventory_landmark(framework::Uuid region, framework::Vector3 position,
                                                 std::uint64_t handle) {
    return {"Landmark version 2\nregion_id " + region.str() + "\nlocal_pos " + invariant(position.x) + " " +
                invariant(position.y) + " " + invariant(position.z) + "\nregion_handle " +
                std::to_string(handle) + "\n",
            std::string{}, std::string{}};
}
InventoryNewController::InventoryNewController(InventoryNewBackend b) : backend_(std::move(b)) {
    if (!backend_.can_create || !backend_.folder || !backend_.folder_type || !backend_.transactions ||
        !backend_.presence || !backend_.landmark || !backend_.create_asset || !backend_.store_asset ||
        !backend_.environment || !backend_.create)
        throw std::invalid_argument("Incomplete new inventory backend");
}
bool InventoryNewController::defined_folder(std::int8_t value) {
    switch (value) {
    case -1:
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 13:
    case 14:
    case 15:
    case 16:
    case 20:
    case 21:
    case 23:
    case 26:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 53:
    case 54:
    case 56:
    case 57:
    case 100:
        return true;
    default:
        return false;
    }
}
void InventoryNewController::create(InventoryNewRequest request) const {
    if (backend_.debug)
        backend_.debug("REQUEST");
    if (!backend_.can_create(request.inventory_type, request.agent))
        return;
    auto folder = backend_.folder(request.agent, request.folder);
    if (!folder && defined_folder(request.inventory_type)) {
        folder = backend_.folder_type(request.agent, request.inventory_type);
        if (folder && backend_.debug)
            backend_.debug("FALLBACK");
    }
    if (!folder || folder->owner != request.agent)
        return;
    if (!request.transaction.is_zero() && request.asset_type != 56 && request.asset_type != 57) {
        auto transactions = backend_.transactions();
        if (transactions && (*transactions)(request))
            return;
    }
    auto presence = backend_.presence(request.agent);
    if (!presence.found) {
        if (backend_.error)
            backend_.error("PRESENCE");
        return;
    }
    std::uint32_t everyone = 0, group = 0, flags = 0;
    framework::Uuid asset;
    if (request.inventory_type == 3 && presence.nonnull) {
        auto landmark = backend_.landmark(request.agent);
        if (!landmark.text)
            throw std::invalid_argument("Null landmark text");
        auto text = framework::utf16(*landmark.text);
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (auto unit : text)
            bytes.push_back(static_cast<std::uint8_t>(unit));
        request.name = landmark.prefix.value_or("") + request.name.value_or("");
        request.description = request.description.value_or("") + landmark.suffix.value_or("");
        group = 0x9e000;
        everyone = 0x9a000;
        auto created = backend_.create_asset(request, std::move(bytes));
        backend_.store_asset(created);
        if (!created)
            throw std::invalid_argument("Null created landmark asset");
        asset = created->full_id();
        request.asset_type = created->type();
        backend_.create(request, asset, flags, everyone, group);
        return;
    }
    switch (request.asset_type) {
    case 56: {
        flags = request.subtype;
        auto environment = backend_.environment();
        if (!environment)
            return;
        asset = (*environment)(request.subtype);
        if (asset.is_zero()) {
            if (backend_.error)
                backend_.error("ENVIRONMENT");
            return;
        }
        break;
    }
    case 10:
        asset = framework::Uuid("2074003b-8d5f-40e5-8b20-581c1c50aedb");
        break;
    case 7:
        asset = framework::Uuid("4b6a777d-7bcd-4fc4-b06f-929b21e32925");
        break;
    case 57:
        asset = framework::Uuid("968cbad0-4dad-d64e-71b5-72bf13ad051a");
        break;
    case 5:
    case 13:
        flags = request.subtype;
        break;
    default:
        break;
    }
    backend_.create(request, asset, flags, everyone, group);
}
}
