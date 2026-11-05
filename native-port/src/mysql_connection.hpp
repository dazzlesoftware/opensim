// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <mysql.h>
#include <memory>
#include <string>

namespace opensim::services {
using MysqlConnection = std::unique_ptr<MYSQL, decltype(&mysql_close)>;

MysqlConnection connect_mysql(const std::string& connection, unsigned long client_flags = 0);
}
