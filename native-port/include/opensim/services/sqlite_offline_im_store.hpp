// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/sqlite_generic_connection.hpp"

namespace opensim::services {
class SqliteOfflineImStore {
public:
    explicit SqliteOfflineImStore(const std::string& connection, const std::string& realm = "native_offline_im");
    bool store(framework::Uuid recipient, const network::ServiceXmlValue& message, int limit);
    std::vector<network::ServiceXmlValue> drain(framework::Uuid recipient);
    void erase(framework::Uuid recipient);

private:
    SqliteDatabase database_;
    std::string table_;
};
}
