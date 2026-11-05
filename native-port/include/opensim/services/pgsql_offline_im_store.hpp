// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/network/service_protocol.hpp"
#include <memory>

namespace opensim::services {
class PgsqlOfflineImStore {
public:
    PgsqlOfflineImStore(const std::string& connection, const std::string& realm);
    ~PgsqlOfflineImStore();
    bool store(framework::Uuid recipient, const network::ServiceXmlValue& message, int limit);
    std::vector<network::ServiceXmlValue> drain(framework::Uuid recipient);
    void erase(framework::Uuid recipient);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
