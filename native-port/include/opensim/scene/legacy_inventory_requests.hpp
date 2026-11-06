// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/scene/inventory_packets.hpp"

namespace opensim::scene {

struct LegacyItemRequest {
    framework::Uuid owner;
    framework::Uuid item;
};

struct LegacyFolderRequest {
    framework::Uuid folder;
    bool fetch_folders = false;
    bool fetch_items = false;
};

struct LegacyInventoryBackend {
    std::function<std::optional<services::InventoryItem>(framework::Uuid, framework::Uuid)> item;
    std::function<services::InventoryCollection(framework::Uuid, framework::Uuid)> content;
    std::function<std::optional<services::InventoryFolder>(framework::Uuid, framework::Uuid)> folder;
    std::function<std::optional<services::InventoryCollection>(framework::Uuid)> library_folder;
};

enum class LegacyFolderDisposition {
    Ignored,
    Answered,
    Queued
};

class LegacyInventoryRequests {
public:
    explicit LegacyInventoryRequests(LegacyInventoryBackend backend);

    bool process_items(framework::Uuid agent,
                       std::span<const LegacyItemRequest> requests,
                       const std::function<bool()>& is_active,
                       const InventoryPacketSink& sink) const;
    LegacyFolderDisposition receive_folder(framework::Uuid agent,
                                           const LegacyFolderRequest& request,
                                           const InventoryPacketSink& sink) const;

    // A true result means the original ordinary-folder worker reaches Sleep(50).
    // Exceptions propagate before that point and are handled by the host queue.
    bool process_folder(framework::Uuid agent,
                        const LegacyFolderRequest& request,
                        const std::function<bool()>& is_active,
                        const InventoryPacketSink& sink) const;

private:
    void send_folder(framework::Uuid agent,
                     const LegacyFolderRequest& request,
                     const services::InventoryCollection& content,
                     int version,
                     int descendants,
                     const InventoryPacketSink& sink) const;

    LegacyInventoryBackend backend_;
};

} // namespace opensim::scene
