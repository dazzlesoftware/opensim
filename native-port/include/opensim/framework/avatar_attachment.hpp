// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
namespace opensim::network {
struct Value;
}
namespace opensim::framework {
struct AvatarAttachment {
    std::int32_t point = 0;
    Uuid item, asset;
    AvatarAttachment(std::int32_t attach_point, const Uuid& item_id, const Uuid& asset_id)
        : point(attach_point), item(item_id), asset(asset_id) {}
    explicit AvatarAttachment(const network::Value& map);
    network::Value pack() const;
    void unpack(const network::Value& map);
    void unpack(const network::Value* map);
};
}
