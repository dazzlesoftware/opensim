// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/profile_packets.hpp"
#include <iostream>
#include <sstream>
#include <bit>
#include <limits>
#include <iomanip>
using namespace opensim;
using namespace framework;
using namespace scene;
using namespace network::lludp;
std::string H(std::span<const std::uint8_t> data) {
    std::string out;
    for (auto c : data) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
ProfileText Text(const std::string& value) {
    if (value == "-")
        return {};
    if (value == ".")
        return u"";
    std::u16string s;
    for (std::size_t i = 0; i < value.size(); i += 4)
        s += static_cast<char16_t>(std::stoi(value.substr(i, 2), nullptr, 16) |
                                   (std::stoi(value.substr(i + 2, 2), nullptr, 16) << 8));
    return s;
}
Uuid Key(int i) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << i;
    return Uuid(out.str());
}

class GroupEventsProbe : public ProfileEvents {
  public:
    std::uint32_t flags;
    std::string trace;
    std::shared_ptr<services::EventQueueByteBuffer> buffer;
    explicit GroupEventsProbe(std::uint32_t value) : flags(value) {
    }
    std::shared_ptr<services::EventQueueByteBuffer> start_event(const std::string& name) override {
        trace += "S|";
        if (flags & 32)
            throw std::runtime_error("start");
        if (flags & 64)
            return {};
        return buffer = ProfileEvents::start_event(name);
    }
    server::PollBytes end_event(std::shared_ptr<services::EventQueueByteBuffer> value) override {
        trace += "F|";
        if (flags & 128)
            throw std::runtime_error("finish");
        if (flags & 1024)
            return {};
        return ProfileEvents::end_event(std::move(value));
    }
    bool enqueue(server::PollBytes bytes, Uuid agent) override {
        trace += "E:" + agent.str() + ":" + (bytes ? H(*bytes) : "-") + "|";
        if (flags & 512)
            throw std::runtime_error("enqueue");
        return !(flags & 256);
    }
};
std::string GroupProbe(const ProfileText& first, const ProfileText& last, int count, std::uint32_t flags) {
    GroupEventsProbe events(flags);
    std::string result = "OK";
    try {
        Uuid recipient("00000000-0000-0000-0000-000000000001"),
            avatar("00000000-0000-0000-0000-000000000002");
        if (flags & 4)
            throw services::EventQueueNullReference();
        std::vector<std::shared_ptr<AvatarProfileGroup>> groups;
        for (int i = 0; i < count; ++i) {
            if (((flags & 16) && i == 0) || ((flags & 2048) && i == 1)) {
                groups.push_back({});
                continue;
            }
            auto group = std::make_shared<AvatarProfileGroup>();
            group->group = Key(i + 100);
            group->picture = i % 2 == 0 ? Uuid{} : Key(i + 200);
            group->name = first;
            group->title = last;
            group->powers = std::array<std::uint64_t, 4>{0, 0xffffffffffffffffULL, 0x8000000000000000ULL,
                                                         0x0123456789abcdefULL}[i % 4];
            group->list_in_profile = i % 2 == 0;
            groups.push_back(group);
        }
        events.trace += "Q|";
        send_avatar_groups(recipient, (flags & 1) ? recipient : avatar, count < 0 ? nullptr : &groups,
                           (flags & 2) ? nullptr : &events);
    } catch (const std::exception& e) {
        std::string error = e.what();
        result = "EX:" + H(std::span(reinterpret_cast<const std::uint8_t*>(error.data()), error.size()));
    }
    return result + "|" + events.trace + "|" + (events.buffer ? H(*events.buffer->to_array()) : "-");
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, length;
        std::string first, last;
        std::uint32_t flags;
        in >> mode >> first >> last >> length >> flags;
        try {
            Uuid recipient("00000000-0000-0000-0000-000000000001"),
                target("00000000-0000-0000-0000-000000000002");
            Packet packet;
            if (mode == 9) {
                std::cout << GroupProbe(Text(first), Text(last), length, flags) << "\n";
                continue;
            }
            if (mode == 0) {
                AvatarProfileProperties p;
                p.avatar = target;
                p.about = Text(first);
                p.born = Text(last);
                p.first_life_about = Text(last);
                p.url = Text(first);
                p.flags = flags;
                if (length >= 0) {
                    p.membership = std::make_shared<std::vector<std::uint8_t>>(length);
                    for (int i = 0; i < length; ++i)
                        (*p.membership)[i] = static_cast<std::uint8_t>(i);
                }
                packet = avatar_profile_properties_packet(recipient, p);
            } else if (mode == 1) {
                AvatarProfileInterests p;
                p.avatar = target;
                p.wants = flags;
                p.skills = ~flags;
                p.want_text = Text(first);
                p.skill_text = Text(last);
                p.languages = Text(first);
                packet = avatar_profile_interests_packet(recipient, p);
            } else if (mode == 2) {
                packet = avatar_profile_notes_packet(recipient, target, Text(first));
            } else if (mode == 3) {
                packet = user_profile_info_packet(recipient, (flags & 1) != 0, (flags & 2) != 0, Text(first));
            } else if (mode <= 5) {
                const std::array<std::array<double, 3>, 4> positions{
                    {{0.1, -0.0, 256.25},
                     {-123456.789, 1e30, -1e-30},
                     {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0},
                     {0, 0, 0}}};
                const auto& v = positions.at(length);
                if (mode == 4) {
                    AvatarClassifiedInfo p;
                    p.classified = target;
                    p.creator = recipient;
                    p.snapshot = target;
                    p.created = flags;
                    p.expires = ~flags;
                    p.category = 0x80000000;
                    p.parent_estate = 0xffffffff;
                    p.name = Text(first);
                    p.description = Text(last);
                    p.sim_name = Text(first);
                    p.parcel_name = Text(last);
                    p.global_position = {static_cast<float>(v[0]), static_cast<float>(v[1]),
                                         static_cast<float>(v[2])};
                    p.flags = static_cast<std::uint8_t>(flags);
                    p.price = std::bit_cast<std::int32_t>(flags);
                    packet = avatar_classified_info_packet(recipient, p);
                } else {
                    AvatarPickInfo p;
                    p.pick = target;
                    p.creator = recipient;
                    p.snapshot = target;
                    p.top_pick = (flags & 1) != 0;
                    p.enabled = (flags & 2) != 0;
                    p.name = Text(first);
                    p.description = Text(last);
                    p.user = Text(last);
                    p.original_name = Text(first);
                    p.sim_name = Text(first);
                    p.global_position = v;
                    p.sort_order = std::bit_cast<std::int32_t>(flags);
                    packet = avatar_pick_info_packet(recipient, p);
                }
            } else if (mode == 8) {
                std::vector<Uuid> ids;
                if (length >= 0)
                    ids.resize(length);
                auto n = std::bit_cast<std::int32_t>(flags);
                std::vector<ProfileText> names;
                if (n >= 0)
                    names.resize(n);
                auto result = avatar_classifieds_list_packet(recipient, target, length < 0 ? nullptr : &ids,
                                                             n < 0 ? nullptr : &names);
                if (!result) {
                    std::cout << "NONE\n";
                    continue;
                }
                packet = std::move(*result);
            } else {
                ProfileList list;
                for (int i = 0; i < length; ++i)
                    list[Key(i)] = i % 4 == 0   ? Text(first)
                                   : i % 4 == 1 ? Text(last)
                                   : i % 4 == 2 ? Text(first)
                                                : ProfileText{};
                if (length > 3 && (flags & 1) != 0) {
                    list.erase(Key(0));
                    list.erase(Key(3));
                    list[Key(10000)] = Text(last);
                    list[Key(10001)] = Text(first);
                }
                if (length > 1 && (flags & 2) != 0)
                    list[Key(1)] = Text(first);
                packet = mode == 6 ? avatar_picks_list_packet(recipient, target, length < 0 ? nullptr : &list)
                                   : avatar_classifieds_list_packet(recipient, target,
                                                                    length < 0 ? nullptr : &list);
            }
            auto raw = encode_packet(packet), wire = encode_datagram(packet);
            std::cout << H(raw) << "|" << H(wire) << "\n";
        } catch (const std::exception& e) {
            std::string text = e.what();
            std::cout << "EX:"
                      << H(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))
                      << "\n";
        }
    }
}
