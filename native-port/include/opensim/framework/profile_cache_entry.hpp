// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/user_profile_models.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/framework/reference_hash_set.hpp"
namespace opensim::scene {
struct AdvancedProfileClient;
class ProfileRequestClient;
}
namespace opensim::framework {
struct GroupMembershipData {
    Uuid GroupID;
    std::optional<std::u16string> GroupName;
    bool AllowPublish = true, MaturePublish = true;
    std::optional<std::u16string> Charter;
    Uuid FounderID, GroupPicture;
    std::int32_t MembershipFee = 0;
    bool OpenEnrollment = true, ShowInList = true, AcceptNotices = true;
    std::int32_t Contribution = 0;
    std::uint64_t GroupPowers = 0;
    bool Active = false;
    Uuid ActiveRole;
    bool ListInProfile = false;
    std::optional<std::u16string> GroupTitle;
};
using ProfileClientSet = ReferenceHashSet<scene::ProfileRequestClient>;
using UserProfileNameList = OrderedDictionary<Uuid, std::optional<std::u16string>, UuidHash>;
struct UserProfileCacheEntry {
    std::shared_ptr<UserProfileNameList> picksList;
    std::shared_ptr<OrderedDictionary<Uuid, std::shared_ptr<UserProfilePick>, UuidHash>> picks;
    std::shared_ptr<UserProfileNameList> classifiedsLists;
    std::shared_ptr<OrderedDictionary<Uuid, std::shared_ptr<UserClassifiedAdd>, UuidHash>> classifieds;
    std::shared_ptr<UserProfileProperties> props;
    std::shared_ptr<std::vector<std::shared_ptr<GroupMembershipData>>> avatarGroups;
    std::optional<std::u16string> born;
    std::shared_ptr<std::vector<std::uint8_t>> membershipType;
    std::uint32_t flags = 0;
    std::shared_ptr<ProfileClientSet> ClientsWaitingProps;
};
}
