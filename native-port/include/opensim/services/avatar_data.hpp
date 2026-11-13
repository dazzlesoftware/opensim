// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/user_account.hpp"
#include "opensim/framework/avatar_appearance.hpp"
namespace opensim::services {
bool parse_avatar_float(const std::optional<std::string>& text,float& value);
std::string avatar_float_string(float value);
struct AvatarData {
    using Fields = framework::OrderedDictionary<std::string, std::optional<std::string>>;
    int avatar_type=0;
    std::shared_ptr<Fields> data;
    AvatarData()=default;
    explicit AvatarData(const AccountValues& values);
    explicit AvatarData(const framework::AvatarAppearance* appearance);
    std::shared_ptr<AccountValues> to_key_value_pairs()const;
    std::shared_ptr<framework::AvatarAppearance> to_appearance()const;
};
}
