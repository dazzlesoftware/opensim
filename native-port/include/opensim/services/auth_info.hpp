// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/sqlite_authentication_plugin.hpp"
namespace opensim::services {
struct AuthInfo {
 framework::Uuid principal;
 std::optional<std::string> account_type, password_hash, password_salt, web_login_key;
 framework::OrderedDictionary<std::string,std::any> to_key_value_pairs()const;
};
std::shared_ptr<AuthInfo> authentication_info(const std::shared_ptr<AuthenticationRecord>& record);
std::shared_ptr<AuthenticationRecord> authentication_record(const std::shared_ptr<AuthInfo>& info);
}
