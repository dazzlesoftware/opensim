// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/local_region_router.hpp"
#include "opensim/scene/region_runtime.hpp"

#include <stdexcept>
#include <utility>

namespace opensim::server {

void LocalRegionRouter::add(const std::shared_ptr<opensim::scene::RegionRuntime> &region) {
    std::lock_guard lock(mutex_);
    const auto [entry, inserted] = regions_.try_emplace(region->descriptor().id, region);
    if (inserted)
        return;
    if (!entry->second.expired())
        throw std::invalid_argument("Duplicate local region ID");
    entry->second = region;
}

void LocalRegionRouter::remove(const std::shared_ptr<opensim::scene::RegionRuntime> &region) {
    std::lock_guard lock(mutex_);
    const auto entry = regions_.find(region->descriptor().id);
    if (entry != regions_.end() && entry->second.lock() == region)
        regions_.erase(entry);
}

std::shared_ptr<opensim::scene::RegionRuntime> LocalRegionRouter::find(const opensim::framework::Uuid &id) const {
    std::lock_guard lock(mutex_);
    const auto region = regions_.find(id);
    return region == regions_.end() ? nullptr : region->second.lock();
}

std::shared_ptr<opensim::scene::RegionRuntime> LocalRegionRouter::find_by_handle(std::uint64_t handle) const {
    for (const auto &region : snapshot()) {
        if (region->descriptor().handle() == handle)
            return region;
    }
    return nullptr;
}

void LocalRegionRouter::for_each_region(const std::function<void(opensim::scene::RegionRuntime &)> &action) const {
    for (const auto &region : snapshot())
        action(*region);
}

std::shared_ptr<opensim::scene::RegionRuntime> LocalRegionRouter::find_by_name(std::string_view name) const {
    for (const auto &region : snapshot()) {
        if (region->descriptor().name == name)
            return region;
    }
    return nullptr;
}

bool LocalRegionRouter::deliver_to_viewer(const std::function<bool(opensim::scene::RegionRuntime &)> &deliver) const {
    for (const auto &region : snapshot()) {
        if (deliver(*region))
            return true;
    }
    return false;
}

bool LocalRegionRouter::deliver_grid_message(const opensim::scene::RegionInstantMessage &message) const {
    if (message.dialog == grid_moderation_dialog) {
        // A broadcast moderation request must reach every local region.
        bool handled = false;
        for (const auto &region : snapshot()) {
            if (region->receive_grid_moderation(message))
                handled = true;
        }
        return handled;
    }

    if (deliver_to_viewer([&](auto &region) { return region.deliver_instant_message(message); }))
        return true;

    // Viewer text messages may reach a child presence after every local root has been checked.
    if (!message.sender_viewer_session)
        return false;
    return deliver_to_viewer([&](auto &region) { return region.deliver_instant_message_to_child(message); });
}

bool LocalRegionRouter::receive_grid_message(const opensim::scene::RegionInstantMessage &message) const {
    if (message.dialog == grid_moderation_dialog)
        return deliver_grid_message(message);
    return deliver_to_viewer([&](auto &region) { return region.receive_grid_instant_message(message); });
}

opensim::network::Value LocalRegionRouter::land_data(std::uint64_t handle, std::uint32_t x, std::uint32_t y) const {
    // The handle and offsets together identify the parcel's world position.
    for (const auto &region : snapshot()) {
        auto data = region->land_data(handle, x, y);
        if (!data.map.empty())
            return data;
    }
    return opensim::network::Value::object({});
}

std::optional<opensim::network::HttpResponse>
LocalRegionRouter::map_request(const opensim::network::HttpRequest &request) const {
    if (request.path != "/index.php" && !request.path.starts_with("/MAP/MapItems/"))
        return {};
    for (const auto &region : snapshot()) {
        auto response =
            request.path == "/index.php" ? region->region_map_image(request) : region->remote_map_items(request);
        if (response)
            return response;
    }
    return {};
}

bool LocalRegionRouter::query_access(opensim::services::GridRegion &destination, const opensim::framework::Uuid &agent,
                                     const std::optional<std::string> &home, bool teleport,
                                     const opensim::framework::Vector3 &position,
                                     std::vector<opensim::framework::Uuid> &features,
                                     opensim::services::LoginTransferContext &context, std::string &reason) {
    if (auto region = find(destination.id))
        return region->query_access(destination, agent, home, teleport, position, features, context, reason);
    reason = "Region unavailable";
    return false;
}

bool LocalRegionRouter::create_agent(const opensim::services::GridRegion *source,
                                     opensim::services::GridRegion &destination,
                                     const std::shared_ptr<opensim::framework::AgentCircuitData> &circuit,
                                     std::uint32_t flags, opensim::services::LoginTransferContext &context,
                                     std::string &reason) {
    if (auto region = find(destination.id))
        return region->create_agent(source, destination, circuit, flags, context, reason);
    reason = "Region unavailable";
    return false;
}

std::shared_ptr<opensim::framework::AgentTransferScene>
LocalRegionRouter::get_scene(const opensim::framework::Uuid &id) {
    if (auto region = find(id))
        return region->get_scene(id);
    return {};
}

bool LocalRegionRouter::update_agent(opensim::services::GridRegion &destination,
                                     const std::shared_ptr<opensim::framework::AgentData> &data,
                                     opensim::framework::EntityTransferContext &context) {
    auto region = find(destination.id);
    return region && region->update_agent(destination, data, context);
}

bool LocalRegionRouter::update_agent(opensim::services::GridRegion &destination,
                                     const std::shared_ptr<opensim::framework::AgentPosition> &data) {
    const auto regions = snapshot();
    bool destination_hosted = false;
    for (const auto &region : regions)
        if (region->descriptor().id == destination.id) {
            destination_hosted = true;
            break;
        }
    if (!destination_hosted || !data)
        return false;
    // A simulator receives one position update for all of its hosted child presences.
    for (const auto &region : regions) {
        auto descriptor = region->descriptor();
        region->update_agent(descriptor, data);
    }
    return true;
}

bool LocalRegionRouter::create_object(opensim::services::GridRegion *destination,
                                      const opensim::framework::Vector3 &position,
                                      std::shared_ptr<opensim::services::SimulationObject> object, bool local_call) {
    auto region = destination ? find(destination->id) : nullptr;
    return region && region->create_object(destination, position, std::move(object), local_call);
}

std::vector<std::shared_ptr<opensim::scene::RegionRuntime>> LocalRegionRouter::snapshot() const {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<opensim::scene::RegionRuntime>> result;
    result.reserve(regions_.size());
    for (const auto &[id, entry] : regions_) {
        if (auto region = entry.lock())
            result.push_back(std::move(region));
    }
    return result;
}

} // namespace opensim::server
