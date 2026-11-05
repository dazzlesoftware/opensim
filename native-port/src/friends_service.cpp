// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/friends_service.hpp"
#include <stdexcept>
namespace opensim::services {
bool friend_can_modify_objects(IFriendsService& service, framework::Uuid actor, framework::Uuid owner,
                               bool hypergrid) {
    if (actor.is_zero())
        return false;
    constexpr std::int32_t modify_objects = 4;
    const auto owner_id = owner.str();
    for (const auto& entry : service.get(actor)) {
        if (entry.friend_id &&
            (hypergrid ? entry.friend_id->starts_with(owner_id) : *entry.friend_id == owner_id))
            return entry.their_flags != -1 && (entry.their_flags & modify_objects) != 0;
    }
    return false;
}
FriendInfo::FriendInfo(const AccountValues& values) : friend_id("") {
    auto text = [&](const char* key) -> std::optional<std::string> {
        auto i = values.find(key);
        if (i == values.end() || !i->second.has_value() || i->second.type() == typeid(std::nullptr_t))
            return {};
        return framework::service_url_object_text(i->second);
    };
    if (auto value = text("PrincipalID"))
        framework::try_parse_sdk_uuid(*value, principal);
    if (auto value = text("Friend"))
        friend_id = *value;
    if (auto value = text("MyFlags"))
        parse_account_integer(*value, my_flags);
    if (auto value = text("TheirFlags"))
        parse_account_integer(*value, their_flags);
}
AccountValues FriendInfo::to_key_value_pairs() const {
    AccountValues values;
    values["PrincipalID"] = principal.str();
    values["Friend"] = friend_id ? std::any(*friend_id) : std::any{};
    values["MyFlags"] = std::to_string(my_flags);
    values["TheirFlags"] = std::to_string(their_flags);
    return values;
}
std::vector<FriendInfo> FriendsService::get(const framework::Uuid& principal) {
    auto rows = store_.get(principal);
    std::vector<FriendInfo> result;
    for (const auto& row : rows) {
        if (!row || !row->principal)
            throw std::invalid_argument("Null friends row or principal");
        FriendInfo info;
        if (!framework::try_parse_sdk_uuid(*row->principal, info.principal))
            throw std::invalid_argument("Invalid friend principal UUID");
        info.friend_id = row->friend_id;
        if (!row->data)
            throw std::invalid_argument("Null friend data");
        auto my = row->data->at("Flags");
        info.my_flags = my ? account_integer(*my) : 0;
        auto their = row->data->at("TheirFlags");
        info.their_flags = their ? account_integer(*their) : 0;
        result.push_back(std::move(info));
    }
    return result;
}
std::vector<FriendInfo> FriendsService::get(const std::optional<std::string>& principal) {
    auto rows = store_.get(principal);
    // The original string overload validates the newly constructed info.Friend
    // before assigning d.Friend. Its null default fails, skipping every row.
    (void)rows;
    return {};
}
bool FriendsService::store(const std::optional<std::string>& principal,
                           const std::optional<std::string>& friend_id, std::int32_t flags) {
    auto row = std::make_shared<FriendsData>();
    row->principal = principal;
    row->friend_id = friend_id;
    row->data = std::make_shared<FriendsData::Fields>();
    (*row->data)["Flags"] = std::to_string(flags);
    return store_.store(row);
}
}
