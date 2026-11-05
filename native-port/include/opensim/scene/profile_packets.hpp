// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/basic_profile.hpp"
#include "opensim/network/lludp.hpp"
#include <array>
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/services/event_queue.hpp"
namespace opensim::scene {
network::lludp::Packet avatar_profile_properties_packet(framework::Uuid recipient,
                                                        const AvatarProfileProperties&);
network::lludp::Packet avatar_profile_interests_packet(framework::Uuid recipient,
                                                       const AvatarProfileInterests&);
network::lludp::Packet avatar_profile_notes_packet(framework::Uuid recipient, framework::Uuid target,
                                                   const ProfileText& notes);
network::lludp::Packet user_profile_info_packet(framework::Uuid recipient, bool im_via_email, bool visible,
                                                const ProfileText& email);
struct AvatarClassifiedInfo {
    framework::Uuid classified, creator, parcel, snapshot;
    std::uint32_t created = 0, expires = 0, category = 0, parent_estate = 0;
    ProfileText name, description, sim_name, parcel_name;
    std::array<float, 3> global_position{};
    std::uint8_t flags = 0;
    std::int32_t price = 0;
};
struct AvatarPickInfo {
    framework::Uuid pick, creator, parcel, snapshot;
    bool top_pick = false, enabled = false;
    ProfileText name, description, user, original_name, sim_name;
    std::array<double, 3> global_position{};
    std::int32_t sort_order = 0;
};
network::lludp::Packet avatar_classified_info_packet(framework::Uuid recipient, const AvatarClassifiedInfo&);
network::lludp::Packet avatar_pick_info_packet(framework::Uuid recipient, const AvatarPickInfo&);
using ProfileList = framework::OrderedDictionary<framework::Uuid, ProfileText, framework::UuidHash>;
using ProfileNameCompare = std::function<int(const ProfileText&, const ProfileText&)>;
network::lludp::Packet avatar_picks_list_packet(framework::Uuid recipient, framework::Uuid target,
                                                const ProfileList* picks, ProfileNameCompare compare = {});
network::lludp::Packet avatar_classifieds_list_packet(framework::Uuid recipient, framework::Uuid target,
                                                      const ProfileList* classifieds);
std::optional<network::lludp::Packet> avatar_classifieds_list_packet(framework::Uuid recipient,
                                                                     framework::Uuid target,
                                                                     const std::vector<framework::Uuid>* ids,
                                                                     const std::vector<ProfileText>* names);
struct AvatarProfileGroup {
    framework::Uuid group, picture;
    ProfileText name, title;
    std::uint64_t powers = 0;
    bool list_in_profile = false;
};
class ProfileEvents {
  public:
    virtual ~ProfileEvents() = default;
    virtual std::shared_ptr<services::EventQueueByteBuffer> start_event(const std::string& name) {
        return services::EventQueueModule::start_event(name);
    }
    virtual server::PollBytes end_event(std::shared_ptr<services::EventQueueByteBuffer> buffer) {
        return services::EventQueueModule::end_event(std::move(buffer));
    }
    virtual bool enqueue(server::PollBytes, framework::Uuid) = 0;
};
std::shared_ptr<ProfileEvents> profile_events(std::shared_ptr<services::EventQueueModule>);
void send_avatar_groups(framework::Uuid recipient, framework::Uuid avatar,
                        const std::vector<std::shared_ptr<AvatarProfileGroup>>* groups,
                        ProfileEvents* events);
}
