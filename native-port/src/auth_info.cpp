// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/auth_info.hpp"
namespace opensim::services {
framework::OrderedDictionary<std::string, std::any> AuthInfo::to_key_value_pairs() const {
    framework::OrderedDictionary<std::string, std::any> result;
    result["PrincipalID"] = principal;
    auto put = [&](const char* name, const auto& value) {
        result[name] = value ? std::any(*value) : std::any{};
    };
    put("AccountType", account_type);
    put("PasswordHash", password_hash);
    put("PasswordSalt", password_salt);
    put("WebLoginKey", web_login_key);
    return result;
}
std::shared_ptr<AuthInfo> authentication_info(const std::shared_ptr<AuthenticationRecord>& record) {
    if (!record)
        return {};
    if (!record->data)
        throw std::invalid_argument("Null authentication Data");
    auto info = std::make_shared<AuthInfo>();
    info->principal = record->principal;
    auto text = [&](const char* key) -> std::optional<std::string> {
        auto& value = record->data->at(key);
        if (auto p = std::any_cast<std::string>(&value))
            return *p;
        return {};
    };
    info->account_type = text("accountType");
    info->password_hash = text("passwordHash");
    info->password_salt = text("passwordSalt");
    info->web_login_key = text("webLoginKey");
    return info;
}
std::shared_ptr<AuthenticationRecord> authentication_record(const std::shared_ptr<AuthInfo>& info) {
    if (!info)
        throw std::invalid_argument("Null authentication info");
    auto record = std::make_shared<AuthenticationRecord>();
    record->principal = info->principal;
    record->data = std::make_shared<framework::OrderedDictionary<std::string, std::any>>();
    auto put = [&](const char* key, const auto& value) {
        (*record->data)[key] = value ? std::any(*value) : std::any{};
    };
    put("accountType", info->account_type);
    put("webLoginKey", info->web_login_key);
    put("passwordHash", info->password_hash);
    put("passwordSalt", info->password_salt);
    return record;
}
}
