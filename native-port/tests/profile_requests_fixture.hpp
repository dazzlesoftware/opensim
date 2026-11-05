#pragma once
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/profile_requests.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/scene/profile_packets.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
using namespace opensim;
using namespace framework;
using namespace scene;
namespace {
int mode, flags;
ProfileText uri, text, asset_uri;
std::string trace;
void Fail(int mask, const char* message) {
    if (flags & mask)
        throw std::runtime_error(message);
}
std::string H(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string T(const ProfileText& value) {
    if (!value)
        return "-";
    std::string bytes;
    for (auto c : *value) {
        bytes += static_cast<char>(c & 255);
        bytes += static_cast<char>(c >> 8);
    }
    return H(bytes);
}
ProfileText Decode(const std::string& value) {
    if (value == "-")
        return {};
    if (value == ".")
        return u"";
    std::u16string out;
    for (std::size_t i = 0; i < value.size(); i += 4)
        out += static_cast<char16_t>(std::stoi(value.substr(i, 2), nullptr, 16) |
                                     (std::stoi(value.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
Uuid Id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return Uuid(out.str());
}
std::string Dump(const std::shared_ptr<server::JsonRpcMembers>& value) {
    if (auto n = std::dynamic_pointer_cast<UserProfileNotes>(value))
        return "N:" + n->UserId.str() + ":" + n->TargetId.str() + ":" + T(n->Notes);
    if (auto p = std::dynamic_pointer_cast<UserPreferences>(value))
        return "P:" + p->UserId.str() + ":" + (p->IMViaEmail ? "1" : "0") + ":" + (p->Visible ? "1" : "0") +
               ":" + T(p->EMail);
    return "?";
}
struct Directory : ProfileUserDirectory {
    void web_failed(Uuid) override {
        throw std::logic_error("unexpected web failed");
    }
    ProfileText server_url(Uuid user, std::u16string_view kind) override {
        trace += "asseturl:" + user.str() + ":" + utf8(kind) + ";";
        Fail(524288, "asseturl");
        return asset_uri;
    }
    bool is_local(Uuid user) override {
        trace += "local:" + user.str() + ";";
        Fail(262144, "local");
        return !(flags & 16);
    }
    ProfileText server_url(Uuid user, std::u16string_view kind, bool& failed) override {
        trace += "url:" + user.str() + ":" + utf8(kind) + ";";
        Fail(524288, "url");
        failed = flags & 64;
        return uri;
    }
};
struct FixturePresence : ProfileRequestPresence {
    std::shared_ptr<ProfileNpcClient> npc_client() override {
        throw std::logic_error("unexpected NPC");
    }
    bool viewer_ui_god() override {
        throw std::logic_error("unexpected god");
    }
    Vector3 absolute_position() override {
        throw std::logic_error("unexpected position");
    }
    std::shared_ptr<ProfileRequestScene> scene() override {
        throw std::logic_error("unexpected presence scene");
    }
};
struct NetPresence : FixturePresence {
    std::shared_ptr<ProfileRequestScene> world;
    bool viewer_ui_god() override {
        return false;
    }
    Vector3 absolute_position() override {
        return {10, 20, 30};
    }
    std::shared_ptr<ProfileRequestScene> scene() override {
        return world;
    }
};
struct Client : ProfileRequestClient {
    bool is_active() override {
        throw std::logic_error("unexpected active");
    }
    std::shared_ptr<ProfileRequestPresence> scene_presence() override {
        throw std::logic_error("unexpected client presence");
    }
    void send_properties(const AvatarProfileProperties&) override {
        throw std::logic_error("unexpected properties");
    }
    void send_interests(const AvatarProfileInterests&) override {
        throw std::logic_error("unexpected interests");
    }
    void send_groups(Uuid, std::shared_ptr<ProfileMemberships>) override {
        throw std::logic_error("unexpected groups");
    }

    void send_pick_info(const AvatarPickInfo&) override {
        throw std::logic_error("unexpected pick detail");
    }
    std::shared_ptr<ProfileRequestScene> scene() override {
        throw std::logic_error("unexpected scene");
    }
    Vector3 scene_agent_position() override {
        throw std::logic_error("unexpected scene agent");
    }
    void send_classified_info(const AvatarClassifiedInfo&) override {
        throw std::logic_error("unexpected classified detail callback in notes fixture");
    }
    void send_classifieds(Uuid, std::shared_ptr<UserProfileNameList>) override {
        throw std::logic_error("unexpected classifieds callback in notes fixture");
    }
    void send_picks(Uuid, std::shared_ptr<UserProfileNameList>) override {
        throw std::logic_error("unexpected picks callback in notes fixture");
    }
    int count = 0;
    Uuid agent_id() override {
        auto id = Id((flags & 1048576) && count++ > 0 ? 2 : 1);
        trace += "agent:" + id.str() + ";";
        Fail(8192, "agent");
        return id;
    }
    void send_notes(Uuid target, const ProfileText& notes) override {
        trace += "notes:" + target.str() + ":" + T(notes) + ";";
        Fail(16384, "notes");
    }
    void send_preferences(bool email, bool visible, const ProfileText& address) override {
        trace +=
            "prefs:" + std::string(email ? "1" : "0") + ":" + (visible ? "1" : "0") + ":" + T(address) + ";";
        Fail(32768, "prefs");
    }
    void alert(std::u16string_view message, bool modal) override {
        trace += "alert:" + T(std::u16string(message)) + ":" + (modal ? "1" : "0") + ";";
        Fail(65536, "alert");
    }
};
struct FixtureHomeGrid : ProfileHomeGrid {
    ProfileText gatekeeper_url() override {
        return u"http://native.example";
    }
    int is_local_grid(const ProfileText&, bool) override {
        return 1;
    }
};
struct Context : ProfileRequestContext {
    bool allow_web_urls() override {
        throw std::logic_error("unexpected web policy");
    }
    ProfileText system_agent_born() override {
        throw std::logic_error("unexpected system birth");
    }
    std::shared_ptr<ProfileAccountService> account_service() override {
        throw std::logic_error("unexpected accounts");
    }
    std::shared_ptr<ProfileUserAgent> user_agent(const ProfileText&) override {
        throw std::logic_error("unexpected user agent");
    }
    std::shared_ptr<ProfileFriends> friends() override {
        throw std::logic_error("unexpected friends");
    }
    std::shared_ptr<ProfilePresenceService> presence_service() override {
        throw std::logic_error("unexpected presence service");
    }
    std::shared_ptr<ProfileRequestGroups> groups() override {
        throw std::logic_error("unexpected groups");
    }
    bool legacy_profile(std::shared_ptr<UserProfileProperties>&, const ProfileText&) override {
        throw std::logic_error("unexpected legacy profile");
    }
    void debug(const ProfileText&, const std::optional<std::u16string>&) override {
        throw std::logic_error("unexpected debug");
    }
    void process_error(const std::exception&) override {
        throw std::logic_error("unexpected process error");
    }
    void schedule(std::function<void()>) override {
        throw std::logic_error("unexpected schedule");
    }

    void warn_pick_parcel(float, float, const ProfileText&) override {
        throw std::logic_error("unexpected warning");
    }
    std::shared_ptr<ProfileRequestGridService> grid_service() override {
        throw std::logic_error("unexpected grid service");
    }
    Uuid region_scope() override {
        throw std::logic_error("unexpected scope");
    }
    void get_foreign_image(Uuid, const ProfileText&, bool) override {
        throw std::logic_error("unexpected asset callback in notes fixture");
    }
    std::shared_ptr<Directory> directory = std::make_shared<Directory>();
    std::shared_ptr<ProfileHomeGrid> home = std::make_shared<FixtureHomeGrid>();
    std::shared_ptr<ProfileUserDirectory> users() override {
        return flags & 128 ? nullptr : directory;
    }
    std::shared_ptr<ProfileHomeGrid> grid() override {
        home->has_hg = !(flags & 32);
        return flags & 256 ? nullptr : home;
    }
    ProfileText local_profile_uri() override {
        return uri;
    }
    std::shared_ptr<ProfileRequestPresence> presence(Uuid id) override {
        if (flags & 4194304)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "presence:" + id.str() + ";";
        Fail(4096, "presence");
        if (flags & 2048)
            return {};
        auto p = std::make_shared<FixturePresence>();
        p->child = flags & 1024;
        p->npc = flags & 512;
        return p;
    }
    void info(std::u16string_view value) override {
        trace += "info:" + utf8(value) + ";";
        Fail(131072, "info");
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        Uuid parsed;
        bool valid = id && try_parse_sdk_uuid(*id, parsed) && !parsed.is_zero();
        trace += "rpc:" + utf8(method.value_or(u"")) + ":" + T(url) + ":" + Dump(data) + ":" +
                 (valid ? "1" : "0") + ";";
        auto notes = std::dynamic_pointer_cast<UserProfileNotes>(data);
        if (mode == 7)
            throw std::runtime_error("rpc");
        if (mode == 1 || mode == 2 || mode == 10) {
            if (notes) {
                notes->TargetId = Id(3);
                notes->Notes = mode == 10 ? ProfileText{} : text;
            } else {
                auto p = std::dynamic_pointer_cast<UserPreferences>(data);
                p->IMViaEmail = true;
                p->Visible = false;
                p->EMail = mode == 10 ? ProfileText{} : text;
            }
        }
        if (mode == 3 || mode == 4) {
            if (notes) {
                auto n = std::make_shared<UserProfileNotes>();
                n->UserId = Id(2);
                n->TargetId = Id(4);
                n->Notes = text;
                data = n;
            } else {
                auto p = std::make_shared<UserPreferences>();
                p->UserId = Id(2);
                p->IMViaEmail = false;
                p->Visible = true;
                p->EMail = text;
                data = p;
            }
        }
        if (mode == 5 || mode == 9)
            data.reset();
        if (mode == 6) {
            if (notes)
                data = std::make_shared<UserPreferences>();
            else
                data = std::make_shared<UserProfileNotes>();
        }
        return mode != 0 && mode != 2 && mode != 4 && mode != 9;
    }
};
}

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct NetDirectory : ProfileUserDirectory {
    void web_failed(Uuid) override {
        throw std::logic_error("unexpected web failed");
    }
    ProfileText server_url(Uuid user, std::u16string_view) override {
        last = user;
        return url;
    }
    bool foreign = false;
    ProfileText url;
    Uuid last;
    bool is_local(Uuid user) override {
        last = user;
        return !foreign;
    }
    ProfileText server_url(Uuid user, std::u16string_view kind, bool& failed) override {
        last = user;
        Check(kind == u"ProfileServerURI", "source directory key");
        failed = false;
        return url;
    }
};
struct NetContext : ProfileRequestContext {
    bool allow_web_urls() override {
        throw std::logic_error("unexpected web policy");
    }
    ProfileText system_agent_born() override {
        throw std::logic_error("unexpected system birth");
    }
    std::shared_ptr<ProfileAccountService> account_service() override {
        throw std::logic_error("unexpected accounts");
    }
    std::shared_ptr<ProfileUserAgent> user_agent(const ProfileText&) override {
        throw std::logic_error("unexpected user agent");
    }
    std::shared_ptr<ProfileFriends> friends() override {
        throw std::logic_error("unexpected friends");
    }
    std::shared_ptr<ProfilePresenceService> presence_service() override {
        throw std::logic_error("unexpected presence service");
    }
    std::shared_ptr<ProfileRequestGroups> groups() override {
        throw std::logic_error("unexpected groups");
    }
    bool legacy_profile(std::shared_ptr<UserProfileProperties>&, const ProfileText&) override {
        throw std::logic_error("unexpected legacy profile");
    }
    void debug(const ProfileText&, const std::optional<std::u16string>&) override {
        throw std::logic_error("unexpected debug");
    }
    void process_error(const std::exception&) override {
        throw std::logic_error("unexpected process error");
    }
    void schedule(std::function<void()>) override {
        throw std::logic_error("unexpected schedule");
    }

    void warn_pick_parcel(float, float, const ProfileText&) override {
        throw std::logic_error("unexpected warning");
    }
    std::shared_ptr<ProfileRequestScene> pick_presence_scene;
    std::shared_ptr<ProfileRequestGridService> grid_service() override {
        throw std::logic_error("unexpected grid service");
    }
    Uuid region_scope() override {
        throw std::logic_error("unexpected scope");
    }
    void get_foreign_image(Uuid, const ProfileText&, bool) override {
        throw std::logic_error("unexpected foreign image in network fixture");
    }
    std::shared_ptr<NetDirectory> directory = std::make_shared<NetDirectory>();
    std::shared_ptr<ProfileHomeGrid> home = std::make_shared<FixtureHomeGrid>();
    bool npc = false, last_rpc = false, update_presence = false;
    int calls = 0;
    std::shared_ptr<ProfileUserDirectory> users() override {
        return directory;
    }
    std::shared_ptr<ProfileHomeGrid> grid() override {
        return home;
    }
    ProfileText local_profile_uri() override {
        return directory->url;
    }
    std::shared_ptr<ProfileRequestPresence> presence(Uuid) override {
        if (update_presence) {
            auto p = std::make_shared<NetPresence>();
            p->world = pick_presence_scene;
            p->current_parcel = Id(6);
            return p;
        }
        if (!npc)
            return {};
        auto p = std::make_shared<FixturePresence>();
        p->npc = true;
        return p;
    }
    void info(std::u16string_view) override {
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        ++calls;
        return last_rpc = ProfileRequestContext::request_object(data, method, url, id);
    }
    bool request(std::shared_ptr<network::Value>& data, const ProfileText& method, const ProfileText& url,
                 const ProfileText& id) override {
        ++calls;
        return last_rpc = ProfileRequestContext::request(data, method, url, id);
    }
};
// Account/scene services are controlled boundaries; profile RPC and storage are real.
struct NetAccounts : ProfileAccountService {
    std::shared_ptr<ProfileAccountData> get(Uuid, Uuid) override {
        auto a = std::make_shared<ProfileAccountData>();
        a->flags = 39;
        a->created = 1;
        a->title = u"native account";
        return a;
    }
};
struct NetAsyncContext : NetContext {
    bool web = true;
    std::vector<std::function<void()>> jobs;
    std::vector<std::string> errors;
    bool allow_web_urls() override {
        return web;
    }
    ProfileText system_agent_born() override {
        return u"01/01/2007 00:00:00";
    }
    std::shared_ptr<ProfileAccountService> account_service() override {
        return std::make_shared<NetAccounts>();
    }
    Uuid region_scope() override {
        return Id(8);
    }
    std::shared_ptr<ProfileRequestGroups> groups() override {
        return {};
    }
    std::shared_ptr<ProfileFriends> friends() override {
        return {};
    }
    std::shared_ptr<ProfilePresenceService> presence_service() override {
        return {};
    }
    void debug(const ProfileText&, const std::optional<std::u16string>&) override {
    }
    void process_error(const std::exception& e) override {
        errors.emplace_back(e.what());
    }
    void schedule(std::function<void()> work) override {
        jobs.push_back(std::move(work));
    }
    void drain() {
        while (!jobs.empty()) {
            auto work = std::move(jobs.front());
            jobs.erase(jobs.begin());
            std::thread worker(std::move(work));
            worker.join();
        }
        Check(errors.empty(), "async worker completes without logged errors");
    }
};
struct NetLand : ProfileRequestLand {
    ProfileText name() override {
        return u"native parcel";
    }
};
struct NetScene : ProfileRequestScene {
    std::uint32_t world_x() override {
        return 512;
    }
    std::uint32_t world_y() override {
        return 768;
    }
    std::uint64_t region_handle() override {
        return (std::uint64_t{512} << 32) | 768;
    }
    ProfileText scene_name() override {
        return u"native region";
    }
    int money_lookups = 0;
    std::shared_ptr<ProfileRequestLand> land(float x, float y) override {
        Check(x == 10 && y == 20, "classified uses caller position for land");
        return std::make_shared<NetLand>();
    }
    std::shared_ptr<ProfileRequestMoney> money() override {
        ++money_lookups;
        return {};
    }
    ProfileText region_name() override {
        return u"native region";
    }
};
struct NetClient : ProfileRequestClient {
    bool is_active() override {
        return true;
    }
    std::shared_ptr<ProfileRequestPresence> scene_presence() override {
        return {};
    }
    int properties_replies = 0, interests_replies = 0;
    AvatarProfileProperties last_properties;
    AvatarProfileInterests last_interests;
    void send_properties(const AvatarProfileProperties& p) override {
        ++properties_replies;
        last_properties = p;
        Check(!network::lludp::encode_datagram(avatar_profile_properties_packet(owner, p)).empty(),
              "properties UDP framing");
    }
    void send_interests(const AvatarProfileInterests& p) override {
        ++interests_replies;
        last_interests = p;
        Check(!network::lludp::encode_datagram(avatar_profile_interests_packet(owner, p)).empty(),
              "interests UDP framing");
    }
    void send_groups(Uuid, std::shared_ptr<ProfileMemberships>) override {
        throw std::logic_error("unexpected groups");
    }

    int pick_details = 0;
    AvatarPickInfo last_pick_data;
    void send_pick_info(const AvatarPickInfo& pick) override {
        ++pick_details;
        last_pick_data = pick;
        auto wire = network::lludp::encode_datagram(avatar_pick_info_packet(owner, pick));
        Check(!wire.empty(), "pick detail UDP framing");
    }
    std::shared_ptr<NetScene> world = std::make_shared<NetScene>();
    std::shared_ptr<ProfileRequestScene> scene() override {
        return world;
    }
    Vector3 scene_agent_position() override {
        return {10, 20, 30};
    }
    int detail_replies = 0;
    AvatarClassifiedInfo last_ad;
    void send_classified_info(const AvatarClassifiedInfo& ad) override {
        ++detail_replies;
        last_ad = ad;
        auto wire = network::lludp::encode_datagram(avatar_classified_info_packet(owner, ad));
        Check(!wire.empty(), "classified detail UDP framing");
    }
    int classified_replies = 0;
    std::shared_ptr<UserProfileNameList> classified_list;
    void send_classifieds(Uuid id, std::shared_ptr<UserProfileNameList> value) override {
        ++classified_replies;
        classified_list = value;
        auto wire = network::lludp::encode_datagram(avatar_classifieds_list_packet(owner, id, value.get()));
        Check(!wire.empty(), "classifieds UDP framing");
    }

    int pick_replies = 0;
    std::shared_ptr<UserProfileNameList> pick_list;
    void send_picks(Uuid id, std::shared_ptr<UserProfileNameList> picks) override {
        auto wire = network::lludp::encode_datagram(avatar_picks_list_packet(owner, id, picks.get()));
        Check(!wire.empty(), "picks UDP framing");
        ++pick_replies;
        pick_list = std::move(picks);
    }
    Uuid owner, target;
    ProfileText notes;
    int note_replies = 0, pref_replies = 0, alerts = 0;
    std::u16string last_alert;
    Uuid agent_id() override {
        return owner;
    }
    void send_notes(Uuid id, const ProfileText& value) override {
        target = id;
        notes = value;
        ++note_replies;
        auto packet = avatar_profile_notes_packet(owner, id, value);
        auto wire = network::lludp::encode_datagram(packet);
        Check(!wire.empty(), "notes UDP framing");
    }
    void send_preferences(bool email, bool visible, const ProfileText& address) override {
        ++pref_replies;
        auto wire = network::lludp::encode_datagram(user_profile_info_packet(owner, email, visible, address));
        Check(!wire.empty(), "preferences UDP framing");
    }
    void alert(std::u16string_view value, bool modal) override {
        Check(!modal, "source nonmodal alert");
        ++alerts;
        last_alert = value;
    }
};
[[maybe_unused]] int RunNetwork(const char* endpoint, const char* user, const char* target_text) {
    try {
        auto context = std::make_shared<NetAsyncContext>();
        context->directory->url = utf16_replacing(endpoint);
        context->home->has_hg = true;
        auto client = std::make_shared<NetClient>();
        client->owner = Uuid(user);
        Uuid target(target_text);
        auto owned = std::make_shared<UserProfileRequests>(context);
        auto& module = *owned;
        std::vector<ProfileText> args{utf16(target.str())};
        module.notes_request(client, &args);
        Check(context->last_rpc && client->target == target && !client->notes,
              "missing notes reply uses nullable original field");
        module.notes_update(client, target, u"controller note \U0001F600 <&");
        Check(context->last_rpc && client->alerts == 0, "notes update goes through actual RPC");
        module.notes_request(client, &args);
        Check(context->last_rpc && client->target == target &&
                  client->notes == ProfileText(u"controller note \U0001F600 <&") && client->note_replies == 2,
              "controller notes round trip");
        std::cout << "PASS: controller notes HTTP SQLite and UDP framing\n";
        module.preferences_update(true, false, client);
        Check(context->last_rpc && client->alerts == 0, "preferences update goes through actual RPC");
        module.preferences_request(client);
        Check(!context->last_rpc && client->pref_replies == 0 && client->alerts == 0,
              "original provider read error silently suppresses reply");
        std::cout << "PASS: controller preferences update and source read failure\n";
        context->directory->foreign = true;
        module.notes_request(client, &args);
        Check(context->last_rpc && context->directory->last == client->owner &&
                  client->notes == ProfileText(u"controller note \U0001F600 <&"),
              "foreign routing uses owner home service");
        std::cout << "PASS: controller foreign owner profile routing\n";
        auto calls = context->calls;
        context->home->has_hg = false;
        module.notes_request(client, &args);
        module.preferences_request(client);
        Check(context->calls == calls && !client->notes && client->pref_replies == 0,
              "HG disabled avoids network and returns empty note");
        std::cout << "PASS: controller HG-disabled suppression\n";
        context->npc = true;
        module.notes_update(client, Uuid("11111111-1111-0000-0000-000100bba000"), u"ignored");
        Check(client->alerts == 0 && context->calls == calls, "system agent notes ignored");
        module.notes_update(client, target, u"ignored");
        Check(client->alerts == 1 && client->last_alert == u"Notes for NPCs not available" &&
                  context->calls == calls,
              "NPC alert before RPC");
        std::cout << "PASS: controller NPC and system-agent guards\n";
        context->npc = false;
        context->directory->foreign = false;
        context->directory->url = u" \t";
        module.notes_request(client, &args);
        module.preferences_request(client);
        Check(context->calls == calls && !client->notes && client->pref_replies == 0, "blank URI guards");
        std::cout << "PASS: controller blank URI avoids HTTP\n";
        context->directory->url = utf16_replacing(endpoint);
        auto picks = module.get_picks(client->owner);
        Check(context->last_rpc && picks && picks->size() == 1 &&
                  picks->begin()->second == ProfileText(u"pick"),
              "actual picks list response decoded and cached");
        auto pick_key = picks->begin()->first;
        calls = context->calls;
        Check(module.get_picks(client->owner) == picks && context->calls == calls,
              "GetPicks cache identity suppresses HTTP");
        std::vector<ProfileText> pick_args{utf16(client->owner.str())};
        module.picks_request(client, &pick_args);
        Check(client->pick_replies == 1 && client->pick_list == picks && context->calls == calls,
              "viewer picks callback uses cached reference and UDP frame");
        (*picks)[pick_key] = u"controller cached title";
        Check(module.get_picks(client->owner)->at(pick_key) == ProfileText(u"controller cached title") &&
                  context->calls == calls,
              "caller mutation remains cached by reference");
        module.profile_cache().clear();
        auto fresh = module.get_picks(client->owner);
        Check(context->last_rpc && context->calls == calls + 1 && fresh != picks &&
                  fresh->at(pick_key) == ProfileText(u"pick"),
              "cache clear re-fetches original persistent title");
        std::cout << "PASS: controller picks RPC cache identity mutation clear and UDP framing\n";
        calls = context->calls;
        module.classifieds_request(client, &pick_args);
        Check(context->last_rpc && client->classified_replies == 1 && client->classified_list &&
                  client->classified_list->size() == 1 &&
                  client->classified_list->begin()->second == ProfileText(u"ad \u00e9"),
              "classified list RPC and reply");
        auto ads = client->classified_list;
        auto ad = ads->begin()->first;
        Check(module.classified_owners().at(ad) == client->owner && module.classified_interests().at(ad) == 1,
              "classified creator and first interest");
        module.classifieds_request(client, &pick_args);
        Check(client->classified_list == ads && context->calls == calls + 1 &&
                  module.classified_interests().at(ad) == 2,
              "classified cache hit increments interest without HTTP");
        (*ads)[ad] = u"cached ad";
        module.classifieds_request(client, &pick_args);
        Check(client->classified_list == ads &&
                  client->classified_list->at(ad) == ProfileText(u"cached ad") &&
                  module.classified_interests().at(ad) == 3 && context->calls == calls + 1,
              "shared classified cache mutation");
        module.profile_cache().clear();
        module.classifieds_request(client, &pick_args);
        Check(client->classified_list != ads &&
                  client->classified_list->at(ad) == ProfileText(u"ad \u00e9") &&
                  module.classified_interests().at(ad) == 4 &&
                  module.classified_owners().at(ad) == client->owner && context->calls == calls + 2,
              "classified re-fetch retains bookkeeping and original persisted title");
        std::cout << "PASS: controller classified RPC cache interest ownership and UDP framing\n";
        calls = context->calls;
        module.classified_info_request(ad, client);
        Check(context->last_rpc && context->calls == calls + 1 && client->detail_replies == 1 &&
                  client->last_ad.classified == ad && client->last_ad.creator == client->owner &&
                  client->last_ad.name == ProfileText(u"ad \u00e9"),
              "classified detail real RPC SQLite reply");
        module.classified_info_request(ad, client);
        Check(context->calls == calls + 1 && client->detail_replies == 2 &&
                  module.classified_interests().at(ad) == 2,
              "classified detail cache suppresses HTTP and consumes interest");
        std::shared_ptr<UserProfileCacheEntry> detail_entry;
        Check(module.profile_cache().try_get_value(client->owner, detail_entry) && detail_entry &&
                  detail_entry->classifieds,
              "detail cache populated");
        detail_entry->classifieds->at(ad)->Name = u"cached detail";
        module.classified_info_request(ad, client);
        Check(context->calls == calls + 1 && client->last_ad.name == ProfileText(u"cached detail") &&
                  module.classified_interests().at(ad) == 1,
              "detail shared mutation and interest");
        module.classified_info_request(ad, client);
        Check(!module.classified_owners().contains(ad) && !module.classified_interests().contains(ad) &&
                  client->detail_replies == 4 && context->calls == calls + 1,
              "last detail consumes bookkeeping");
        std::cout << "PASS: controller classified detail RPC cache mutation interest and UDP framing\n";
        auto cached_ad = detail_entry->classifieds->at(ad);
        auto alert_count = client->alerts;
        module.classified_delete(ad, client);
        Check(!context->last_rpc && client->alerts == alert_count + 1 &&
                  client->last_alert == u"Error deleting classified" &&
                  detail_entry->classifieds->at(ad) == cached_ad && detail_entry->classifiedsLists,
              "original SQLite delete error leaves cached details and lists intact");
        std::cout << "PASS: controller classified delete source provider failure preserves cache\n";
        calls = context->calls;
        detail_entry->picksList = fresh;
        context->update_presence = true;
        ProfileClassifiedUpdate update;
        update.classified = ad;
        update.parcel = Id(99);
        update.snapshot = client->last_ad.snapshot;
        update.name = u"ad \u00e9";
        update.description = u"native controller description";
        update.category = 8;
        update.parent_estate = 4;
        update.global_position = {12.5f, -4, 256};
        update.flags = 128;
        update.price = 100;
        module.classified_info_update(update, client);
        Check(context->last_rpc && context->calls == calls + 2 && client->world->money_lookups == 0,
              "existing classified update composes actual list and typed RPC without money lookup");
        Check(!detail_entry->classifieds && !detail_entry->classifiedsLists && detail_entry->picksList &&
                  client->alerts == alert_count + 1,
              "successful update clears classified fields and preserves picks");
        std::cout << "PASS: controller classified update RPC SQLite persistence and cache invalidation\n";
        auto temporary = std::make_shared<UserProfilePick>();
        temporary->PickId = target;
        temporary->CreatorId = client->owner;
        temporary->Name = u"temporary controller pick";
        temporary->SnapshotId = client->last_ad.snapshot;
        std::shared_ptr<server::JsonRpcMembers> temporary_data = temporary;
        Check(context->request_object(temporary_data, u"picks_update", context->directory->url,
                                      context->new_rpc_id()),
              "create temporary pick through production service");
        calls = context->calls;
        auto first_pick = module.get_pick(client->owner, target);
        auto second_pick = module.get_pick(client->owner, target);
        Check(first_pick && second_pick && first_pick != second_pick && context->calls == calls + 2 &&
                  first_pick->Name == temporary->Name,
              "GetPick fresh replies are uncached distinct HTTP records");
        std::vector<ProfileText> info_args{utf16(client->owner.str()), utf16(target.str())};
        module.pick_info_request(client, &info_args);
        Check(context->calls == calls + 3 && client->pick_details == 1 &&
                  client->last_pick_data.pick == target && client->last_pick_data.creator == client->owner &&
                  client->last_pick_data.name == temporary->Name && !client->last_pick_data.top_pick &&
                  !client->last_pick_data.enabled,
              "pick info real RPC SQLite and source boolean fields");
        auto shared_pick = module.get_pick(client->owner, target);
        Check(shared_pick && shared_pick != first_pick && context->calls == calls + 3 &&
                  detail_entry->picks->at(target) == shared_pick,
              "pick info cache returns original shared record");
        shared_pick->Name = u"cached temporary pick";
        module.pick_info_request(client, &info_args);
        Check(context->calls == calls + 3 && client->pick_details == 2 &&
                  client->last_pick_data.name == shared_pick->Name,
              "cached pick detail mutation and reply suppress HTTP");
        std::cout << "PASS: controller pick detail HTTP SQLite uncached GetPick and cached viewer reply\n";
        (*fresh)[target] = temporary->Name;
        context->pick_presence_scene = client->world;
        ProfilePickUpdate edit;
        edit.pick = target;
        edit.creator = client->owner;
        edit.snapshot = client->last_ad.snapshot;
        edit.top_pick = true;
        edit.enabled = true;
        edit.name = u"edited temporary pick";
        edit.description = u"edited temporary description";
        edit.sort_order = 9;
        calls = context->calls;
        auto detail_count = client->pick_details;
        module.pick_info_update(client, edit);
        Check(context->last_rpc && context->calls == calls + 1 &&
                  detail_entry->picks->at(target) == shared_pick && shared_pick->Name == edit.name &&
                  fresh->at(target) == edit.name && client->pick_list == fresh &&
                  client->pick_details == detail_count + 1 && client->last_pick_data.top_pick &&
                  client->last_pick_data.enabled,
              "existing pick update preserves shared record and list with source reply fields");
        auto stored = std::make_shared<UserProfilePick>();
        stored->PickId = target;
        stored->CreatorId = client->owner;
        std::shared_ptr<server::JsonRpcMembers> stored_data = stored;
        Check(context->request_object(stored_data, u"pickinforequest", context->directory->url,
                                      context->new_rpc_id()) &&
                  stored->Name == edit.name && stored->Desc == edit.description && !stored->TopPick &&
                  !stored->Enabled,
              "pick update persisted with original SQLite boolean quirks");
        std::cout << "PASS: controller existing pick update HTTP SQLite shared cache and replies\n";
        edit.pick = Uuid("00000000-0000-0000-0000-00000000aa55");
        edit.name = u"new controller pick";
        edit.description = u"new controller description";
        edit.sort_order = 7;
        calls = context->calls;
        detail_count = client->pick_details;
        module.pick_info_update(client, edit);
        Check(context->last_rpc && context->calls == calls + 1 && client->pick_details == detail_count + 1 &&
                  client->last_pick_data.pick == edit.pick &&
                  client->last_pick_data.global_position == std::array<double, 3>{522, 788, 30} &&
                  client->last_pick_data.user == ProfileText(u"native parcel") &&
                  fresh->at(edit.pick) == edit.name,
              "new pick update uses caller world position and presence parcel");
        Check(detail_entry->picks->at(edit.pick)->Gatekeeper == ProfileText(u"http://native.example") &&
                  detail_entry->picks->at(edit.pick)->GlobalPos == ProfileText(u"<522, 788, 30>"),
              "new pick cached original gatekeeper and SDK coordinates");
        std::cout << "PASS: controller new pick update scene parcel HTTP SQLite and UDP framing\n";
        auto kept = std::make_shared<UserProfilePick>();
        kept->PickId = pick_key;
        (*detail_entry->picks)[pick_key] = kept;
        auto replies = client->pick_replies;
        calls = context->calls;
        module.pick_delete(client, target);
        Check(context->last_rpc && context->calls == calls + 1 && client->pick_replies == replies + 1 &&
                  client->pick_list == fresh && !fresh->contains(target) &&
                  fresh->at(pick_key) == ProfileText(u"pick"),
              "pick delete real HTTP and shared reply list");
        Check(!detail_entry->picks->contains(target) && detail_entry->picks->at(pick_key) == kept &&
                  module.get_picks(client->owner) == fresh && context->calls == calls + 1,
              "pick delete preserves other details and cached reference");
        std::cout << "PASS: controller pick delete HTTP SQLite shared cache and UDP framing\n";
        auto peer = std::make_shared<NetClient>();
        peer->owner = client->owner;
        calls = context->calls;
        module.request_avatar_properties(client, client->owner);
        module.request_avatar_properties(client, client->owner);
        module.request_avatar_properties(peer, client->owner);
        Check(context->jobs.size() == 1 && module.queue_count(false) == 2 && context->calls == calls,
              "pending requests deduplicate client and schedule one worker");
        context->drain();
        Check(context->calls == calls + 2 && client->properties_replies == 2 &&
                  peer->properties_replies == 1 && client->interests_replies == 2 &&
                  peer->interests_replies == 1,
              "LIFO worker broadcasts waiters then completes remaining queued request");
        Check(client->last_properties.flags == 55 &&
                  client->last_properties.born == ProfileText(u"1/1/1970") && detail_entry->flags == 39 &&
                  !detail_entry->ClientsWaitingProps && detail_entry->picksList == fresh,
              "fresh reply combines online flag while cache retains account flags and picks");
        module.request_avatar_properties(client, client->owner);
        Check(context->calls == calls + 2 && client->last_properties.flags == 16 &&
                  client->properties_replies == 3,
              "cached property reply replaces flags with source online flag");
        std::cout << "PASS: controller async worker HTTP SQLite waiter broadcast cache and UDP framing\n";
        calls = context->calls;
        module.avatar_interests_update(client, 0xffffffffu, u"controller wants \U0001F600", 0x80000000u,
                                       u"controller skills", u"en fr");
        Check(context->calls == calls + 1 && !detail_entry->props && detail_entry->picksList == fresh &&
                  context->jobs.size() == 1,
              "interest update invalidates properties and schedules refresh");
        context->drain();
        Check(context->calls == calls + 2 && client->last_interests.wants == 0xffffffffu &&
                  client->last_interests.skills == 0x80000000u &&
                  client->last_interests.want_text == ProfileText(u"controller wants \U0001F600") &&
                  client->last_interests.languages == ProfileText(u"en fr"),
              "interest update and worker refresh round trip unsigned masks and Unicode");
        std::cout << "PASS: controller interest update HTTP SQLite invalidation and worker refresh\n";
        auto properties = std::make_shared<UserProfileProperties>();
        properties->UserId = client->owner;
        properties->AboutText = u"controller about \U0001F600";
        properties->FirstLifeText = u"controller first life";
        properties->WebUrl = u"https://example.test/blocked";
        properties->ImageId = Id(71);
        properties->FirstLifeImageId = Id(72);
        context->web = false;
        calls = context->calls;
        module.avatar_properties_update(client, properties);
        Check(properties->WebUrl == ProfileText(u"") && context->calls == calls + 1 && !detail_entry->props &&
                  detail_entry->picksList == fresh,
              "web policy mutates original input and invalidates properties only");
        context->drain();
        Check(context->calls == calls + 2 && client->last_properties.about == properties->AboutText &&
                  client->last_properties.url == ProfileText(u"") &&
                  client->last_properties.image == Id(71) &&
                  client->last_properties.first_life_image == Id(72) &&
                  client->last_interests.wants == 0xffffffffu,
              "properties refresh returns stored fields and preserves interests");
        std::cout << "PASS: controller properties update HTTP SQLite web policy and worker refresh\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
}
