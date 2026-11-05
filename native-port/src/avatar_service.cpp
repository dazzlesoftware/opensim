// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/avatar_service.hpp"
namespace opensim::services {
AvatarData AvatarService::get_avatar(framework::Uuid principal) {
    auto rows = store_.get(principal);
    AvatarData result;
    result.data = std::make_shared<AvatarData::Fields>();
    if (rows.empty()) {
        result.avatar_type = 1;
        return result;
    }
    for (const auto& row : rows) {
        if (!row || !row->data)
            throw std::invalid_argument("Null avatar database row");
        auto name = row->data->at("Name"), value = row->data->at("Value");
        if (name == "AvatarType")
            result.avatar_type = value ? account_integer(*value) : 0;
        else {
            if (!name)
                throw std::invalid_argument("Null avatar field name");
            (*result.data)[*name] = value;
        }
    }
    return result;
}
bool AvatarService::set_avatar(framework::Uuid principal, AvatarData& avatar) {
    if (!avatar.data)
        throw std::invalid_argument("Null avatar fields");
    // Retain the source's pre-delete dictionary enumeration boundary.
    std::size_t count = 0;
    for (auto [key, value] : *avatar.data)
        if (key.starts_with('_'))
            ++count;
    (void)count;
    store_.erase_all(principal);
    auto row = std::make_shared<AvatarBaseData>();
    row->principal = principal;
    row->data = std::make_shared<AvatarData::Fields>();
    (*row->data)["Name"] = "AvatarType";
    (*row->data)["Value"] = std::to_string(avatar.avatar_type);
    if (!store_.store(row))
        return false;
    auto fields = avatar.data;
    if (!fields)
        throw std::invalid_argument("Null avatar fields");
    for (auto [name, value] : *fields) {
        if (!row->data)
            throw std::invalid_argument("Null avatar row fields");
        (*row->data)["Name"] = name;
        if (name == "AvatarHeight") {
            float height = 0;
            if (!parse_avatar_float(value, height) || height < 0 || height > 10) {
                if (!value)
                    throw std::invalid_argument("Null avatar height");
                auto raw = *value;
                for (auto& c : raw)
                    if (c == ',')
                        c = '.';
                if (!parse_avatar_float(raw, height) || height < 0 || height > 10)
                    height = 1.771488f;
            }
            (*row->data)["Value"] = avatar_float_string(height);
        } else
            (*row->data)["Value"] = value;
        if (!store_.store(row)) {
            store_.erase_all(principal);
            return false;
        }
    }
    return true;
}
std::shared_ptr<framework::AvatarAppearance> AvatarService::get_appearance(framework::Uuid principal) {
    return get_avatar(principal).to_appearance();
}
bool AvatarService::set_appearance(framework::Uuid principal, const framework::AvatarAppearance* appearance) {
    AvatarData data(appearance);
    return store_.write_appearance([&] { return set_avatar(principal, data); });
}
bool AvatarService::set_items(framework::Uuid principal, const Strings* names, const Strings* values) {
    auto row = std::make_shared<AvatarBaseData>();
    row->principal = principal;
    row->data = std::make_shared<AvatarData::Fields>();
    if (!names || !values)
        throw std::invalid_argument("Null avatar item array");
    if (names->size() != values->size())
        return false;
    for (std::size_t i = 0; i < names->size(); ++i) {
        if (!row->data)
            throw std::invalid_argument("Null avatar row fields");
        (*row->data)["Name"] = names->at(i);
        (*row->data)["Value"] = values->at(i);
        if (!store_.store(row))
            return false;
    }
    return true;
}
bool AvatarService::remove_items(framework::Uuid principal, const Strings* names) {
    if (!names)
        throw std::invalid_argument("Null avatar item names");
    for (auto& name : *names)
        store_.erase(principal, name);
    return true;
}
}
