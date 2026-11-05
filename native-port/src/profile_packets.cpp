// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/profile_packets.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "dotnet_regex_sort.hpp"
namespace opensim::scene {
namespace {
std::u16string_view text(const ProfileText& value) {
    return value ? std::u16string_view(*value) : std::u16string_view{};
}
}
network::lludp::Packet avatar_profile_properties_packet(framework::Uuid recipient,
                                                        const AvatarProfileProperties& props) {
    auto p = network::lludp::make_packet("AvatarPropertiesReply");
    auto& a = p.block("AgentData");
    a["AgentID"] = recipient;
    a["AvatarID"] = props.avatar;
    auto& data = p.block("PropertiesData");
    data["ImageID"] = props.image;
    data["FLImageID"] = props.first_life_image;
    data["PartnerID"] = props.partner;
    data["AboutText"] = profile_string_bytes(text(props.about), 1024);
    data["FLAboutText"] = profile_string_bytes(text(props.first_life_about), 255);
    data["BornOn"] = profile_string_bytes(text(props.born), 255);
    data["ProfileURL"] = profile_string_bytes(text(props.url), 255);
    if (!props.membership)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    data["CharterMember"] = *props.membership;
    data["Flags"] = static_cast<std::uint64_t>(props.flags);
    return p;
}
network::lludp::Packet avatar_profile_interests_packet(framework::Uuid recipient,
                                                       const AvatarProfileInterests& interests) {
    auto p = network::lludp::make_packet("AvatarInterestsReply");
    auto& a = p.block("AgentData");
    a["AgentID"] = recipient;
    a["AvatarID"] = interests.avatar;
    auto& data = p.block("PropertiesData");
    data["WantToMask"] = static_cast<std::uint64_t>(interests.wants);
    data["WantToText"] = profile_string_bytes(text(interests.want_text));
    data["SkillsMask"] = static_cast<std::uint64_t>(interests.skills);
    data["SkillsText"] = profile_string_bytes(text(interests.skill_text));
    data["LanguagesText"] = profile_string_bytes(text(interests.languages));
    return p;
}
network::lludp::Packet avatar_profile_notes_packet(framework::Uuid recipient, framework::Uuid target,
                                                   const ProfileText& notes) {
    auto p = network::lludp::make_packet("AvatarNotesReply");
    p.block("AgentData")["AgentID"] = recipient;
    auto& data = p.block("Data");
    data["TargetID"] = target;
    data["Notes"] = profile_string_bytes(text(notes), 1022);
    return p;
}
network::lludp::Packet user_profile_info_packet(framework::Uuid recipient, bool im_via_email, bool visible,
                                                const ProfileText& email) {
    auto p = network::lludp::make_packet("UserInfoReply");
    p.block("AgentData")["AgentID"] = recipient;
    auto& data = p.block("UserData");
    data["IMViaEMail"] = im_via_email;
    data["DirectoryVisibility"] = profile_string_bytes(visible ? u"default" : u"hidden");
    data["EMail"] = profile_string_bytes(text(email));
    return p;
}
network::lludp::Packet avatar_classified_info_packet(framework::Uuid recipient,
                                                     const AvatarClassifiedInfo& info) {
    auto p = network::lludp::make_packet("ClassifiedInfoReply");
    p.block("AgentData")["AgentID"] = recipient;
    auto& d = p.block("Data");
    d["ClassifiedID"] = info.classified;
    d["CreatorID"] = info.creator;
    d["ParcelID"] = info.parcel;
    d["SnapshotID"] = info.snapshot;
    d["CreationDate"] = static_cast<std::uint64_t>(info.created);
    d["ExpirationDate"] = static_cast<std::uint64_t>(info.expires);
    d["Category"] = static_cast<std::uint64_t>(info.category);
    d["ParentEstate"] = static_cast<std::uint64_t>(info.parent_estate);
    d["Name"] = profile_string_bytes(text(info.name));
    d["Desc"] = profile_string_bytes(text(info.description));
    d["SimName"] = profile_string_bytes(text(info.sim_name));
    d["ParcelName"] = profile_string_bytes(text(info.parcel_name));
    d["PosGlobal"] = std::vector<double>(info.global_position.begin(), info.global_position.end());
    d["ClassifiedFlags"] = static_cast<std::uint64_t>((info.flags & 0x4e) == 0 ? info.flags | 4 : info.flags);
    d["PriceForListing"] = static_cast<std::int64_t>(info.price);
    return p;
}
network::lludp::Packet avatar_pick_info_packet(framework::Uuid recipient, const AvatarPickInfo& info) {
    auto p = network::lludp::make_packet("PickInfoReply");
    p.block("AgentData")["AgentID"] = recipient;
    auto& d = p.block("Data");
    d["PickID"] = info.pick;
    d["CreatorID"] = info.creator;
    d["ParcelID"] = info.parcel;
    d["SnapshotID"] = info.snapshot;
    d["TopPick"] = info.top_pick;
    d["Enabled"] = info.enabled;
    d["SortOrder"] = static_cast<std::int64_t>(info.sort_order);
    d["Name"] = profile_string_bytes(text(info.name), 128);
    d["Desc"] = profile_string_bytes(text(info.description), 1022);
    d["User"] = profile_string_bytes(text(info.user), 128);
    d["OriginalName"] = profile_string_bytes(text(info.original_name), 128);
    d["SimName"] = profile_string_bytes(text(info.sim_name));
    d["PosGlobal"] = std::vector<double>(info.global_position.begin(), info.global_position.end());
    return p;
}
network::lludp::Packet avatar_picks_list_packet(framework::Uuid recipient, framework::Uuid target,
                                                const ProfileList* picks, ProfileNameCompare compare) {
    auto p = network::lludp::make_packet("AvatarPicksReply");
    auto& agent = p.block("AgentData");
    agent["AgentID"] = recipient;
    agent["TargetID"] = target;
    if (!picks)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (picks->empty())
        return p;
    std::vector<std::pair<framework::Uuid, ProfileText>> ordered;
    ordered.reserve(picks->size());
    for (const auto& entry : *picks)
        ordered.emplace_back(entry.first, entry.second);
    if (!compare)
        compare = [](const ProfileText& a, const ProfileText& b) {
            if (!a)
                return b ? -1 : 0;
            if (!b)
                return 1;
            return framework::invariant_compare_utf16(*a, *b);
        };
    framework::regex_detail::sort(ordered,
                                  [&](const auto& a, const auto& b) { return compare(a.second, b.second); });
    const auto count = std::min<std::size_t>(20, ordered.size());
    const auto limit = std::min<std::size_t>(128, (1400 - 128) / count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        network::lludp::Block row;
        row["PickID"] = ordered[i].first;
        row["PickName"] = profile_string_bytes(text(ordered[i].second), static_cast<int>(limit));
        p.blocks["Data"].push_back(std::move(row));
    }
    return p;
}
network::lludp::Packet avatar_classifieds_list_packet(framework::Uuid recipient, framework::Uuid target,
                                                      const ProfileList* classifieds) {
    auto p = network::lludp::make_packet("AvatarClassifiedReply");
    auto& agent = p.block("AgentData");
    agent["AgentID"] = recipient;
    agent["TargetID"] = target;
    if (!classifieds)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    for (const auto& entry : *classifieds) {
        network::lludp::Block row;
        row["ClassifiedID"] = entry.first;
        row["Name"] = profile_string_bytes(text(entry.second));
        p.blocks["Data"].push_back(std::move(row));
    }
    return p;
}
std::optional<network::lludp::Packet> avatar_classifieds_list_packet(framework::Uuid recipient,
                                                                     framework::Uuid target,
                                                                     const std::vector<framework::Uuid>* ids,
                                                                     const std::vector<ProfileText>* names) {
    if (!ids || !names)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (ids->size() != names->size())
        return {};
    // The source allocates a reference-type DataBlock array without constructing
    // its elements, then dereferences the first null entry for a nonempty array.
    if (!ids->empty())
        throw std::runtime_error("Object reference not set to an instance of an object.");
    ProfileList empty;
    return avatar_classifieds_list_packet(recipient, target, &empty);
}
namespace {
class ProfileEventsBinding final : public ProfileEvents {
    std::shared_ptr<services::EventQueueModule> queue_;

  public:
    explicit ProfileEventsBinding(std::shared_ptr<services::EventQueueModule> queue)
        : queue_(std::move(queue)) {
    }
    bool enqueue(server::PollBytes bytes, framework::Uuid agent) override {
        return queue_->enqueue(std::move(bytes), agent);
    }
};
void group_uuid(std::string& xml, std::string_view key, framework::Uuid id) {
    xml += "<key>";
    xml += key;
    xml += "</key>";
    xml += id.is_zero() ? "<uuid />" : "<uuid>" + id.str() + "</uuid>";
}
void group_text(std::string& xml, std::string_view key, const ProfileText& input) {
    xml += "<key>";
    xml += key;
    xml += "</key>";
    if (!input || input->empty()) {
        xml += "<string />";
        return;
    }
    xml += "<string>";
    // LLSDxmlEncode2 -> OSDParser.EscapeToXML -> osUTF8.AppendCharBytes:
    // lone surrogates are dropped; an invalid high surrogate consumes its next unit.
    for (std::size_t i = 0; i < input->size(); ++i) {
        std::uint32_t c = (*input)[i];
        switch (c) {
        case '<':
            xml += "&lt;";
            continue;
        case '>':
            xml += "&gt;";
            continue;
        case '&':
            xml += "&amp;";
            continue;
        case '"':
            xml += "&quot;";
            continue;
        case '\'':
            xml += "&apos;";
            continue;
        default:
            break;
        }
        if (c >= 0xd800 && c < 0xe000) {
            if (c >= 0xdc00 || i + 1 >= input->size())
                continue;
            auto next = (*input)[++i];
            if (next < 0xdc00 || next > 0xdfff)
                continue;
            c = 0x10000 + ((c - 0xd800) << 10) + (next - 0xdc00);
        }
        if (c < 128)
            xml += static_cast<char>(c);
        else if (c < 2048) {
            xml += static_cast<char>(0xc0 | (c >> 6));
            xml += static_cast<char>(0x80 | (c & 63));
        } else if (c < 65536) {
            xml += static_cast<char>(0xe0 | (c >> 12));
            xml += static_cast<char>(0x80 | ((c >> 6) & 63));
            xml += static_cast<char>(0x80 | (c & 63));
        } else {
            xml += static_cast<char>(0xf0 | (c >> 18));
            xml += static_cast<char>(0x80 | ((c >> 12) & 63));
            xml += static_cast<char>(0x80 | ((c >> 6) & 63));
            xml += static_cast<char>(0x80 | (c & 63));
        }
    }
    xml += "</string>";
}
}
std::shared_ptr<ProfileEvents> profile_events(std::shared_ptr<services::EventQueueModule> queue) {
    if (!queue)
        return {};
    return std::make_shared<ProfileEventsBinding>(std::move(queue));
}
void send_avatar_groups(framework::Uuid recipient, framework::Uuid avatar,
                        const std::vector<std::shared_ptr<AvatarProfileGroup>>* groups,
                        ProfileEvents* events) {
    if (!events)
        return;
    auto buffer = events->start_event("AvatarGroupsReply");
    if (!buffer)
        throw services::EventQueueNullReference();
    auto& xml = buffer->text;
    xml += "<key>AgentData</key><array><map>";
    group_uuid(xml, "AgentID", recipient);
    group_uuid(xml, "AvatarID", avatar);
    xml += "</map></array>";
    if (!groups)
        throw services::EventQueueNullReference();
    if (groups->empty())
        xml += "<key>GroupData</key><array />";
    else {
        xml += "<key>GroupData</key><array>";
        for (const auto& group : *groups) {
            if (avatar != recipient) {
                if (!group)
                    throw services::EventQueueNullReference();
                if (!group->list_in_profile)
                    continue;
            }
            xml += "<map>";
            if (!group)
                throw services::EventQueueNullReference();
            xml += "<key>GroupPowers</key><binary>";
            auto bytes = std::make_shared<std::vector<std::uint8_t>>(8);
            for (int i = 0; i < 8; ++i)
                (*bytes)[i] = static_cast<std::uint8_t>(group->powers >> (56 - i * 8));
            xml += network::binary_base64(network::osd_from_binary(bytes));
            xml += "</binary>";
            group_text(xml, "GroupTitle", group->title);
            group_uuid(xml, "GroupID", group->group);
            group_text(xml, "GroupName", group->name);
            group_uuid(xml, "GroupInsigniaID", group->picture);
            xml += "</map>";
        }
        xml += "</array>";
    }
    events->enqueue(events->end_event(buffer), recipient);
}
}
