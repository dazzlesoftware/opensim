// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <libpq-fe.h>
#include <memory>
#include <string>

namespace opensim::services {
using PostgresConnection = std::unique_ptr<PGconn, decltype(&PQfinish)>;

PostgresConnection connect_postgres(const std::string& connection);
}
