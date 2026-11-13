// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
namespace opensim::network {
struct Value;
}
namespace opensim::framework {
struct EntityTransferContext {
    float inbound_version = 0.8f, outbound_version = 0.8f;
    int wearables_count=-1;
    network::Value pack()const;
    void unpack(const network::Value& map);
    void unpack(const network::Value* map);
};
}
