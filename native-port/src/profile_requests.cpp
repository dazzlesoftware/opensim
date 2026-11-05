// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/profile_requests.hpp"
#include "opensim/framework/field_string.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/services/account_store.hpp"
#include "opensim/services/grid_service.hpp"
#include "opensim/server/grid_identity.hpp"
#include "opensim/services/presence_service.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/fake_parcel.hpp"
#include "opensim/scene/profile_packets.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
namespace opensim::scene {
namespace {
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *value;
}
bool blank(const ProfileText& value) {
    return !value || std::all_of(value->begin(), value->end(), framework::unicode_whitespace);
}
std::string type_name(const std::shared_ptr<server::JsonRpcMembers>& value) {
    if (std::dynamic_pointer_cast<framework::UserProfileNotes>(value))
        return "OpenSim.Framework.UserProfileNotes";
    if (std::dynamic_pointer_cast<framework::UserPreferences>(value))
        return "OpenSim.Framework.UserPreferences";
    if (std::dynamic_pointer_cast<framework::UserProfileProperties>(value))
        return "OpenSim.Framework.UserProfileProperties";
    if (std::dynamic_pointer_cast<framework::UserProfilePick>(value))
        return "OpenSim.Framework.UserProfilePick";
    if (std::dynamic_pointer_cast<framework::UserClassifiedAdd>(value))
        return "OpenSim.Framework.UserClassifiedAdd";
    return "System.Object";
}
template <class T>
std::shared_ptr<T> cast(const std::shared_ptr<server::JsonRpcMembers>& value, const char* name) {
    auto result = std::dynamic_pointer_cast<T>(value);
    if (value && !result)
        throw std::runtime_error("Unable to cast object of type '" + type_name(value) + "' to type '" + name +
                                 "'.");
    return result;
}
}
ProfileText ProfileRequestContext::new_rpc_id() {
    return framework::utf16(framework::Uuid::random().str());
}
bool ProfileRequestContext::request_object(std::shared_ptr<server::JsonRpcMembers>& data,
                                           const ProfileText& method, const ProfileText& uri,
                                           const ProfileText& id) {
    return rpc_.request_object(data, method, uri, id);
}
bool ProfileRequestContext::request(std::shared_ptr<network::Value>& data, const ProfileText& method,
                                    const ProfileText& uri, const ProfileText& id) {
    return rpc_.request(data, method, uri, id);
}
UserProfileRequests::UserProfileRequests(std::shared_ptr<ProfileRequestContext> context,
                                         framework::ExpiringCacheBackend backend)
    : context_(std::move(context)), profiles_(60000, std::move(backend)) {
}
bool UserProfileRequests::profile_server_uri(framework::Uuid user, ProfileText& uri) {
    if (!required(required(context_).users()).is_local(user)) {
        if (!required(required(context_).grid()).has_hg)
            uri = u"";
        else {
            bool failed = false;
            uri = required(required(context_).users()).server_url(user, u"ProfileServerURI", failed);
            if (failed)
                uri = u"";
        }
        return true;
    }
    uri = required(context_).local_profile_uri();
    return false;
}
std::shared_ptr<ProfileRequestPresence> UserProfileRequests::find_presence(framework::Uuid user) {
    auto presence = required(context_).presence(user);
    return presence && !presence->child ? presence : nullptr;
}
void UserProfileRequests::notes_request(std::shared_ptr<AdvancedProfileClient> sender,
                                        const std::vector<ProfileText>* args) {
    auto client = std::dynamic_pointer_cast<ProfileRequestClient>(sender);
    if (!client)
        return;
    auto note = std::make_shared<framework::UserProfileNotes>();
    if (!args)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (args->empty())
        throw std::out_of_range(
            "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    if (!(*args)[0] || !framework::try_parse_sdk_uuid(*(*args)[0], note->TargetId))
        return;
    note->UserId = client->agent_id();
    ProfileText uri;
    profile_server_uri(client->agent_id(), uri);
    if (blank(uri)) {
        client->send_notes(note->TargetId, note->Notes);
        return;
    }
    std::shared_ptr<server::JsonRpcMembers> data = note;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"avatarnotesrequest", uri, id)) {
        client->send_notes(note->TargetId, note->Notes);
        return;
    }
    note = cast<framework::UserProfileNotes>(data, "OpenSim.Framework.UserProfileNotes");
    auto target = required(note).TargetId;
    client->send_notes(target, required(note).Notes);
}
void UserProfileRequests::notes_update(std::shared_ptr<ProfileRequestClient> client, framework::Uuid target,
                                       const ProfileText& text) {
    if (target == framework::Uuid("11111111-1111-0000-0000-000100bba000"))
        return;
    auto presence = find_presence(target);
    if (presence && presence->npc) {
        required(client).alert(u"Notes for NPCs not available", false);
        return;
    }
    auto note = std::make_shared<framework::UserProfileNotes>();
    note->UserId = required(client).agent_id();
    note->TargetId = target;
    note->Notes = text;
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    std::shared_ptr<server::JsonRpcMembers> data = note;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"avatar_notes_update", uri, id)) {
        required(client).alert(u"Error updating note", false);
        return;
    }
}
void UserProfileRequests::preferences_request(std::shared_ptr<ProfileRequestClient> client) {
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    auto pref = std::make_shared<framework::UserPreferences>();
    pref->UserId = required(client).agent_id();
    std::shared_ptr<server::JsonRpcMembers> data = pref;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"user_preferences_request", uri, id))
        return;
    pref = cast<framework::UserPreferences>(data, "OpenSim.Framework.UserPreferences");
    auto email = required(pref).IMViaEmail;
    auto visible = required(pref).Visible;
    auto address = required(pref).EMail;
    required(client).send_preferences(email, visible, address);
}
void UserProfileRequests::preferences_update(bool email, bool visible,
                                             std::shared_ptr<ProfileRequestClient> client) {
    auto pref = std::make_shared<framework::UserPreferences>();
    pref->UserId = required(client).agent_id();
    pref->IMViaEmail = email;
    pref->Visible = visible;
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    std::shared_ptr<server::JsonRpcMembers> data = pref;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"user_preferences_update", uri, id)) {
        required(context_).info(u"[PROFILES]: UserPreferences update error");
        required(client).alert(u"Error updating preferences", false);
        return;
    }
}
namespace {
std::string osd_type(network::Value::Kind kind) {
    using K = network::Value::Kind;
    switch (kind) {
    case K::Map:
        return "OSDMap";
    case K::Array:
        return "OSDArray";
    case K::String:
        return "OSDString";
    case K::Integer:
        return "OSDInteger";
    case K::Boolean:
        return "OSDBoolean";
    case K::Real:
        return "OSDReal";
    case K::Uuid:
        return "OSDUUID";
    case K::Date:
        return "OSDDate";
    case K::Uri:
        return "OSDUri";
    case K::Binary:
        return "OSDBinary";
    case K::Long:
        return "OSDLong";
    default:
        return "OSD";
    }
}
const network::Value& profile_map(const network::Value& value) {
    const auto& v = value.view();
    if (v.kind != network::Value::Kind::Map)
        throw std::runtime_error("Unable to cast object of type 'OpenMetaverse.StructuredData." +
                                 osd_type(v.kind) + "' to type 'OpenMetaverse.StructuredData.OSDMap'.");
    return v;
}
bool profile_array(const network::Value& value) {
    if (value.kind == network::Value::Kind::NullReference)
        return false;
    return value.view().kind == network::Value::Kind::Array;
}
}
std::shared_ptr<framework::UserProfileNameList>
UserProfileRequests::fetch_picks(framework::Uuid target,
                                 std::shared_ptr<framework::UserProfileNameList> picks) {
    ProfileText uri;
    profile_server_uri(target, uri);
    if (blank(uri))
        return picks;
    auto parameters = std::make_shared<network::Value>();
    parameters->kind = network::Value::Kind::Map;
    network::Value creator;
    creator.kind = network::Value::Kind::Uuid;
    creator.text = target.str();
    parameters->set("creatorId", std::move(creator));
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request(parameters, u"avatarpicksrequest", uri, id))
        return picks;
    const auto& response = profile_map(required(parameters));
    if (!response.contains("result") || !profile_array(response.get("result")))
        return picks;
    for (const auto& item : response.get("result").view().array) {
        const auto& map = profile_map(item);
        auto key = network::osd_uuid(map.get("pickuuid"));
        auto name = network::osd_string_utf16(map.get("name"));
        (*picks)[key] = std::move(name);
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (!profiles_.try_get_value(target, entry) || !entry)
            entry = std::make_shared<framework::UserProfileCacheEntry>();
        entry->picksList = picks;
        profiles_.add_or_update(target, entry, 300.0);
    }
    return picks;
}
std::shared_ptr<framework::UserProfileNameList> UserProfileRequests::get_picks(framework::Uuid target) {
    auto picks = std::make_shared<framework::UserProfileNameList>();
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->picksList)
            return entry->picksList;
    }
    return fetch_picks(target, std::move(picks));
}
void UserProfileRequests::picks_request(std::shared_ptr<AdvancedProfileClient> sender,
                                        const std::vector<ProfileText>* args) {
    auto client = std::dynamic_pointer_cast<ProfileRequestClient>(sender);
    if (!client)
        return;
    if (!args)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (args->empty())
        throw std::out_of_range(
            "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    framework::Uuid target;
    if (!(*args)[0] || !framework::try_parse_sdk_uuid(*(*args)[0], target))
        return;
    auto picks = std::make_shared<framework::UserProfileNameList>();
    if (target == framework::Uuid("11111111-1111-0000-0000-000100bba000")) {
        client->send_picks(target, picks);
        return;
    }
    auto presence = find_presence(target);
    if (presence && presence->npc) {
        client->send_picks(target, picks);
        return;
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->picksList) {
            client->send_picks(target, entry->picksList);
            return;
        }
    }
    auto result = fetch_picks(target, std::move(picks));
    client->send_picks(target, std::move(result));
}
void UserProfileRequests::classified_interest(framework::Uuid id, framework::Uuid owner) {
    std::lock_guard lock(classified_mutex_);
    if (!classified_owners_.contains(id)) {
        classified_owners_[id] = owner;
        if (classified_interests_.contains(id))
            throw std::invalid_argument("An item with the same key has already been added. Key: " + id.str());
        classified_interests_[id] = 0;
    }
    auto found = classified_interests_.find(id);
    if (found == classified_interests_.end())
        throw std::out_of_range("The given key '" + id.str() + "' was not present in the dictionary.");
    found->second = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(found->second) + 1u);
}
void UserProfileRequests::classifieds_request(std::shared_ptr<AdvancedProfileClient> sender,
                                              const std::vector<ProfileText>* args) {
    auto client = std::dynamic_pointer_cast<ProfileRequestClient>(sender);
    if (!client)
        return;
    auto classifieds = std::make_shared<framework::UserProfileNameList>();
    if (!args)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (args->empty())
        throw std::out_of_range(
            "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    framework::Uuid target;
    if (!(*args)[0] || !framework::try_parse_sdk_uuid(*(*args)[0], target) || target.is_zero())
        return;
    if (target == framework::Uuid("11111111-1111-0000-0000-000100bba000")) {
        client->send_classifieds(target, classifieds);
        return;
    }
    auto presence = find_presence(target);
    if (presence && presence->npc) {
        client->send_classifieds(target, classifieds);
        return;
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->classifiedsLists) {
            for (const auto& item : *entry->classifiedsLists) {
                (*classifieds)[item.first] = item.second;
                classified_interest(item.first, target);
            }
            client->send_classifieds(target, entry->classifiedsLists);
            return;
        }
    }
    ProfileText uri;
    profile_server_uri(target, uri);
    if (blank(uri)) {
        client->send_classifieds(target, classifieds);
        return;
    }
    auto parameters = std::make_shared<network::Value>();
    parameters->kind = network::Value::Kind::Map;
    network::Value creator;
    creator.kind = network::Value::Kind::Uuid;
    creator.text = target.str();
    parameters->set("creatorId", std::move(creator));
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request(parameters, u"avatarclassifiedsrequest", uri, id)) {
        client->send_classifieds(target, classifieds);
        return;
    }
    const auto& response = profile_map(required(parameters));
    if (!response.contains("result") || !profile_array(response.get("result"))) {
        client->send_classifieds(target, classifieds);
        return;
    }
    for (const auto& item : response.get("result").view().array) {
        const auto& map = profile_map(item);
        auto key = network::osd_uuid(map.get("classifieduuid"));
        auto name = network::osd_string_utf16(map.get("name"));
        (*classifieds)[key] = std::move(name);
        classified_interest(key, target);
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (!profiles_.try_get_value(target, entry) || !entry)
            entry = std::make_shared<framework::UserProfileCacheEntry>();
        entry->classifiedsLists = classifieds;
        profiles_.add_or_update(target, entry, 300.0);
    }
    client->send_classifieds(target, classifieds);
}
namespace {
AvatarClassifiedInfo classified_reply(const framework::UserClassifiedAdd& ad, framework::Vector3 position) {
    AvatarClassifiedInfo reply;
    reply.classified = ad.ClassifiedId;
    reply.creator = ad.CreatorId;
    reply.created = static_cast<std::uint32_t>(ad.CreationDate);
    reply.expires = static_cast<std::uint32_t>(ad.ExpirationDate);
    reply.category = static_cast<std::uint32_t>(ad.Category);
    reply.name = ad.Name;
    reply.description = ad.Description;
    reply.parcel = ad.ParcelId;
    reply.parent_estate = static_cast<std::uint32_t>(ad.ParentEstate);
    reply.snapshot = ad.SnapshotId;
    reply.sim_name = ad.SimName;
    reply.global_position = {position.x, position.y, position.z};
    reply.parcel_name = ad.ParcelName;
    reply.flags = ad.Flags;
    reply.price = ad.Price;
    return reply;
}
}
void UserProfileRequests::cache_foreign_image(framework::Uuid agent, framework::Uuid image) {
    if (image.is_zero())
        return;
    auto uri = required(required(context_).users()).server_url(agent, u"AssetServerURI");
    if (blank(uri))
        return;
    required(context_).get_foreign_image(image, uri, false);
}
void UserProfileRequests::classified_info_request(framework::Uuid query,
                                                  std::shared_ptr<ProfileRequestClient> client) {
    auto target = required(client).agent_id();
    auto ad = std::make_shared<framework::UserClassifiedAdd>();
    ad->ClassifiedId = query;
    {
        std::lock_guard lock(classified_mutex_);
        if (classified_owners_.contains(query)) {
            target = classified_owners_.at(query);
            auto found = classified_interests_.find(query);
            if (found == classified_interests_.end())
                throw std::out_of_range("The given key '" + query.str() +
                                        "' was not present in the dictionary.");
            found->second = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(found->second) - 1u);
            if (found->second == 0) {
                classified_interests_.erase(query);
                classified_owners_.erase(query);
            }
        }
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->classifieds &&
            entry->classifieds->contains(query)) {
            ad = entry->classifieds->at(query);
            framework::Vector3 position;
            if (framework::try_parse_sdk_vector3(required(ad).GlobalPos.value_or(u""), position))
                required(client).send_classified_info(classified_reply(required(ad), position));
            return;
        }
    }
    ProfileText uri;
    auto foreign = profile_server_uri(target, uri);
    if (blank(uri))
        return;
    std::shared_ptr<server::JsonRpcMembers> data = ad;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"classifieds_info_query", uri, id)) {
        required(client).alert(u"Error getting classified info", false);
        return;
    }
    ad = cast<framework::UserClassifiedAdd>(data, "OpenSim.Framework.UserClassifiedAdd");
    if (required(ad).CreatorId.is_zero())
        return;
    if (foreign)
        cache_foreign_image(target, required(ad).SnapshotId);
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (!profiles_.try_get_value(target, entry) || !entry)
            entry = std::make_shared<framework::UserProfileCacheEntry>();
        if (!entry->classifieds)
            entry->classifieds = std::make_shared<framework::OrderedDictionary<
                framework::Uuid, std::shared_ptr<framework::UserClassifiedAdd>, framework::UuidHash>>();
        (*entry->classifieds)[required(ad).ClassifiedId] = ad;
        profiles_.add_or_update(target, entry, 300.0);
    }
    framework::Vector3 position;
    if (framework::try_parse_sdk_vector3(required(ad).GlobalPos.value_or(u""), position))
        required(client).send_classified_info(classified_reply(required(ad), position));
}
void UserProfileRequests::classified_delete(framework::Uuid classified,
                                            std::shared_ptr<ProfileRequestClient> client) {
    ProfileText uri;
    bool foreign = profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    if (foreign) {
        required(client).alert(u"Please change classifieds on your home grid", true);
        return;
    }
    // The original UUID string roundtrip always succeeds for this UUID value.
    auto parameters = std::make_shared<network::Value>();
    parameters->kind = network::Value::Kind::Map;
    network::Value id;
    id.kind = network::Value::Kind::Uuid;
    id.text = classified.str();
    parameters->set("classifiedId", std::move(id));
    auto request_id = required(context_).new_rpc_id();
    if (!required(context_).request(parameters, u"classified_delete", uri, request_id)) {
        required(client).alert(u"Error deleting classified", false);
        return;
    }
    std::lock_guard lock(profile_mutex_);
    std::shared_ptr<framework::UserProfileCacheEntry> entry;
    if (profiles_.try_get_value(required(client).agent_id(), entry) && entry) {
        entry->classifieds.reset();
        entry->classifiedsLists.reset();
    }
}

void UserProfileRequests::classified_info_update(const ProfileClassifiedUpdate& query,
                                                 std::shared_ptr<ProfileRequestClient> client) {
    auto scene = required(client).scene();
    auto position = required(client).scene_agent_position();
    auto land = required(scene).land(position.x, position.y);
    auto creator = required(client).agent_id();
    auto presence = find_presence(creator);
    std::shared_ptr<framework::UserProfileCacheEntry> entry;
    {
        std::lock_guard lock(profile_mutex_);
        profiles_.try_get_value(required(client).agent_id(), entry);
    }
    ProfileText uri;
    bool foreign = profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    if (foreign) {
        required(client).alert(u"Please change classifieds on your home grid", true);
        if (entry && entry->classifiedsLists)
            required(client).send_classifieds(required(client).agent_id(), entry->classifiedsLists);
        return;
    }
    auto parameters = std::make_shared<network::Value>();
    parameters->kind = network::Value::Kind::Map;
    network::Value creator_value;
    creator_value.kind = network::Value::Kind::Uuid;
    creator_value.text = creator.str();
    parameters->set("creatorId", std::move(creator_value));
    auto request_id = required(context_).new_rpc_id();
    if (!required(context_).request(parameters, u"avatarclassifiedsrequest", uri, request_id)) {
        required(client).alert(u"Error fetching classifieds", false);
        return;
    }
    const auto& response = profile_map(required(parameters));
    const auto& list = response.get("result");
    if (list.kind != network::Value::Kind::NullReference && list.view().kind != network::Value::Kind::Array)
        throw std::runtime_error("Unable to cast object of type 'OpenMetaverse.StructuredData." +
                                 osd_type(list.view().kind) +
                                 "' to type 'OpenMetaverse.StructuredData.OSDArray'.");
    if (list.kind == network::Value::Kind::NullReference)
        throw std::invalid_argument("Value cannot be null. (Parameter 'source')");
    bool exists = false;
    for (const auto& row : list.view().array) {
        if (row.kind == network::Value::Kind::NullReference)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        const auto& map = profile_map(row);
        if (map.contains("classifieduuid") &&
            network::osd_uuid(map.get("classifieduuid")) == query.classified) {
            exists = true;
            break;
        }
    }
    std::shared_ptr<ProfileRequestMoney> money;
    if (!exists) {
        money = required(scene).money();
        if (money && !money->amount_covered(required(client).agent_id(), query.price)) {
            required(client).alert(u"You do not have enough money to create this classified.", false);
            if (entry && entry->classifiedsLists)
                required(client).send_classifieds(required(client).agent_id(), entry->classifiedsLists);
            return;
        }
    }
    auto ad = std::make_shared<framework::UserClassifiedAdd>();
    ad->ParcelName = land ? land->name() : ProfileText(u"");
    ad->CreatorId = required(client).agent_id();
    ad->ClassifiedId = query.classified;
    if (query.category > INT32_MAX)
        throw std::overflow_error("Value was either too large or too small for an Int32.");
    ad->Category = static_cast<std::int32_t>(query.category);
    ad->Name = query.name;
    ad->Description = query.description;
    if (query.parent_estate > INT32_MAX)
        throw std::overflow_error("Value was either too large or too small for an Int32.");
    ad->ParentEstate = static_cast<std::int32_t>(query.parent_estate);
    ad->SnapshotId = query.snapshot;
    ad->SimName = required(required(client).scene()).region_name();
    ad->GlobalPos = framework::utf16(framework::vector3_string(query.global_position));
    ad->Flags = query.flags;
    ad->Price = query.price;
    ad->ParcelId = required(presence).current_parcel;
    std::shared_ptr<server::JsonRpcMembers> data = ad;
    request_id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"classified_update", uri, request_id)) {
        required(client).alert(u"Error updating classified", false);
        if (entry && entry->classifiedsLists)
            required(client).send_classifieds(required(client).agent_id(), entry->classifiedsLists);
        return;
    }
    if (money)
        money->apply_charge(required(client).agent_id(), query.price, 1103);
    {
        std::lock_guard lock(profile_mutex_);
        if (profiles_.try_get_value(required(client).agent_id(), entry) && entry) {
            entry->classifieds.reset();
            entry->classifiedsLists.reset();
        }
    }
}

void UserProfileRequests::pick_delete(std::shared_ptr<ProfileRequestClient> client, framework::Uuid pick) {
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    auto parameters = std::make_shared<network::Value>();
    parameters->kind = network::Value::Kind::Map;
    network::Value value;
    value.kind = network::Value::Kind::Uuid;
    value.text = pick.str();
    parameters->set("pickId", std::move(value));
    auto request_id = required(context_).new_rpc_id();
    if (!required(context_).request(parameters, u"picks_delete", uri, request_id)) {
        required(client).alert(u"Error picks delete", false);
        return;
    }
    std::shared_ptr<framework::UserProfileCacheEntry> entry;
    {
        std::lock_guard lock(profile_mutex_);
        if (profiles_.try_get_value(required(client).agent_id(), entry) && entry) {
            if (entry->picks)
                entry->picks->erase(pick);
            if (entry->picksList)
                entry->picksList->erase(pick);
            profiles_.add_or_update(required(client).agent_id(), entry, 300.0);
        }
    }
    if (entry && entry->picksList)
        required(client).send_picks(required(client).agent_id(), entry->picksList);
    else
        required(client).send_picks(required(client).agent_id(),
                                    std::make_shared<framework::UserProfileNameList>());
}

namespace {
std::uint32_t profile_coordinate(double value) {
    // Match the original .NET8 unchecked conversion without an out-of-range C++ cast.
    if (value >= -0x1p63 && value < 0x1p63)
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
    return 0;
}
AvatarPickInfo pick_reply(const framework::UserProfilePick& pick, framework::Vector3d position) {
    AvatarPickInfo reply;
    reply.pick = pick.PickId;
    reply.creator = pick.CreatorId;
    reply.top_pick = pick.TopPick;
    reply.parcel = pick.ParcelId;
    reply.name = pick.Name;
    reply.description = pick.Desc;
    reply.snapshot = pick.SnapshotId;
    reply.user = pick.ParcelName;
    reply.original_name = pick.OriginalName;
    reply.sim_name = pick.SimName;
    reply.global_position = {position.x, position.y, position.z};
    reply.sort_order = pick.SortOrder;
    reply.enabled = pick.Enabled;
    return reply;
}
}
void UserProfileRequests::map_pick_proxy(framework::Uuid target,
                                         std::shared_ptr<framework::UserProfilePick> pick,
                                         framework::Vector3d& position, bool fetch_valid_image) {
    auto grid = required(context_).grid();
    if (!required(grid).has_hg || required(grid).is_local_grid(required(pick).Gatekeeper, true) != 0)
        return;
    ProfileText name = required(pick).Gatekeeper.value_or(u"") + u" " + required(pick).SimName.value_or(u"");
    auto service = required(context_).grid_service();
    auto scope = required(context_).region_scope();
    auto region = required(service).region_by_name(scope, name);
    if (!region)
        return;
    framework::FakeParcelPosition parcel;
    bool valid = framework::parse_fake_parcel_id(required(pick).ParcelId, parcel);
    if (valid) {
        required(pick).ParcelId = framework::build_fake_parcel_id(region->handle(), parcel.x, parcel.y);
        position.x = static_cast<double>(static_cast<std::int64_t>(region->x) + parcel.x);
        position.y = static_cast<double>(static_cast<std::int64_t>(region->y) + parcel.y);
    } else {
        auto local_x = profile_coordinate(position.x) >> 8;
        position.x -= static_cast<std::uint32_t>(local_x << 8);
        auto local_y = profile_coordinate(position.y) >> 8;
        position.y -= static_cast<std::uint32_t>(local_y << 8);
        required(pick).ParcelId = framework::build_fake_parcel_id(
            region->handle(), profile_coordinate(position.x), profile_coordinate(position.y));
        position.x += region->x;
        position.y += region->y;
    }
    required(pick).GlobalPos = framework::utf16(framework::vector3d_string(position));
    if (!valid || fetch_valid_image)
        cache_foreign_image(target, required(pick).SnapshotId);
}
void UserProfileRequests::pick_info_request(std::shared_ptr<AdvancedProfileClient> sender,
                                            const std::vector<ProfileText>* args) {
    auto client = std::dynamic_pointer_cast<ProfileRequestClient>(sender);
    if (!client)
        return;
    if (!args)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    auto argument = [&](std::size_t index) -> const ProfileText& {
        if (index >= args->size())
            throw std::out_of_range(
                "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
        return args->at(index);
    };
    framework::Uuid target, query;
    auto& target_text = argument(0);
    if (!target_text || !framework::try_parse_sdk_uuid(*target_text, target))
        return;
    auto& pick_text = argument(1);
    if (!pick_text || !framework::try_parse_sdk_uuid(*pick_text, query))
        return;
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->picks &&
            entry->picks->contains(query)) {
            auto pick = entry->picks->at(query);
            framework::Vector3d position;
            if (framework::try_parse_sdk_vector3d(required(pick).GlobalPos.value_or(u""), position))
                required(client).send_pick_info(pick_reply(required(pick), position));
            return;
        }
    }
    ProfileText uri;
    profile_server_uri(target, uri);
    if (blank(uri))
        return;
    auto pick = std::make_shared<framework::UserProfilePick>();
    pick->PickId = query;
    pick->CreatorId = target;
    std::shared_ptr<server::JsonRpcMembers> data = pick;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"pickinforequest", uri, id)) {
        required(client).alert(u"Error selecting pick", false);
        return;
    }
    pick = cast<framework::UserProfilePick>(data, "OpenSim.Framework.UserProfilePick");
    framework::Vector3d position;
    if (!framework::try_parse_sdk_vector3d(required(pick).GlobalPos.value_or(u""), position))
        return;
    map_pick_proxy(target, pick, position, false);
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (!profiles_.try_get_value(target, entry) || !entry)
            entry = std::make_shared<framework::UserProfileCacheEntry>();
        if (!entry->picks)
            entry->picks = std::make_shared<framework::OrderedDictionary<
                framework::Uuid, std::shared_ptr<framework::UserProfilePick>, framework::UuidHash>>();
        (*entry->picks)[required(pick).PickId] = pick;
        profiles_.add_or_update(target, entry, 300.0);
    }
    required(client).send_pick_info(pick_reply(required(pick), position));
}
std::shared_ptr<framework::UserProfilePick> UserProfileRequests::get_pick(framework::Uuid target,
                                                                          framework::Uuid query) {
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(target, entry) && entry && entry->picks && entry->picks->contains(query))
            return entry->picks->at(query);
    }
    auto pick = std::make_shared<framework::UserProfilePick>();
    pick->PickId = query;
    pick->CreatorId = target;
    ProfileText uri;
    profile_server_uri(target, uri);
    if (blank(uri))
        return {};
    std::shared_ptr<server::JsonRpcMembers> data = pick;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"pickinforequest", uri, id))
        return {};
    pick = cast<framework::UserProfilePick>(data, "OpenSim.Framework.UserProfilePick");
    framework::Vector3d position;
    if (!framework::try_parse_sdk_vector3d(required(pick).GlobalPos.value_or(u""), position))
        return {};
    map_pick_proxy(target, pick, position, true);
    return pick;
}

void UserProfileRequests::pick_info_update(std::shared_ptr<ProfileRequestClient> client,
                                           const ProfilePickUpdate& query) {
    ProfileText uri;
    bool alien = profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    if (alien && !required(required(context_).grid()).has_hg) {
        required(client).alert(u"Not possible to update that pick in this region", false);
        return;
    }
    auto presence = find_presence(required(client).agent_id());
    if (!presence)
        return;
    if (required(client).agent_id() != query.creator && !presence->viewer_ui_god())
        return;
    std::shared_ptr<framework::UserProfilePick> pick;
    auto current = get_picks(query.creator);
    if (current && !current->contains(query.pick)) {
        if (current->size() >= 20) {
            required(client).send_picks(required(client).agent_id(), current);
            return;
        }
    } else
        pick = get_pick(query.creator, query.pick);
    framework::Vector3d global;
    if (!pick) {
        auto position = presence->absolute_position();
        auto x = required(required(client).scene()).world_x();
        const float global_x = static_cast<float>(x) + position.x;
        auto y = required(required(client).scene()).world_y();
        const float global_y = static_cast<float>(y) + position.y;
        global = {global_x, global_y, position.z};
        ProfileText parcel_name = u"My Parcel";
        auto handle = required(required(client).scene()).region_handle();
        auto parcel = framework::build_fake_parcel_id(handle, profile_coordinate(position.x),
                                                      profile_coordinate(position.y));
        auto land = required(presence->scene()).land(position.x, position.y);
        if (land)
            parcel_name = land->name();
        else {
            auto name = required(presence->scene()).scene_name();
            required(context_).warn_pick_parcel(position.x, position.y, name);
        }
        pick = std::make_shared<framework::UserProfilePick>();
        pick->PickId = query.pick;
        pick->CreatorId = query.creator;
        pick->TopPick = query.top_pick;
        pick->Name = query.name;
        pick->Desc = query.description;
        pick->ParcelId = parcel;
        pick->SnapshotId = query.snapshot;
        pick->ParcelName = parcel_name;
        pick->SimName = required(required(client).scene()).region_name();
        pick->Gatekeeper = required(required(context_).grid()).gatekeeper_url();
        pick->GlobalPos = framework::utf16(framework::vector3d_string(global));
        pick->SortOrder = query.sort_order;
        pick->Enabled = query.enabled;
    } else {
        pick->TopPick = query.top_pick;
        pick->Name = query.name;
        pick->Desc = query.description;
        pick->SnapshotId = query.snapshot;
        pick->SortOrder = query.sort_order;
        pick->Enabled = query.enabled;
    }
    std::shared_ptr<server::JsonRpcMembers> data = pick;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"picks_update", uri, id)) {
        required(client).alert(u"Error updating pick", false);
        return;
    }
    std::shared_ptr<framework::UserProfileCacheEntry> entry;
    {
        std::lock_guard lock(profile_mutex_);
        if (!profiles_.try_get_value(query.creator, entry) || !entry)
            entry = std::make_shared<framework::UserProfileCacheEntry>();
        if (!entry->picks)
            entry->picks = std::make_shared<framework::OrderedDictionary<
                framework::Uuid, std::shared_ptr<framework::UserProfilePick>, framework::UuidHash>>();
        if (!entry->picksList)
            entry->picksList = std::make_shared<framework::UserProfileNameList>();
        (*entry->picks)[pick->PickId] = pick;
        (*entry->picksList)[pick->PickId] = pick->Name;
        profiles_.add_or_update(query.creator, entry, 300.0);
    }
    framework::try_parse_sdk_vector3d(pick->GlobalPos.value_or(u""), global);
    required(client).send_picks(query.creator, entry->picksList);
    required(client).send_pick_info(pick_reply(*pick, global));
}

namespace {
const framework::Uuid system_profile_agent("11111111-1111-0000-0000-000100bba000");
std::u16string profile_date(std::int32_t created) {
    auto date = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds(std::chrono::seconds(created))));
    return framework::utf16(std::to_string(static_cast<unsigned>(date.month())) + "/" +
                            std::to_string(static_cast<unsigned>(date.day())) + "/" +
                            std::to_string(static_cast<int>(date.year())));
}
std::int32_t profile_boxed_int(const std::any& value) {
    if (!value.has_value() || value.type() == typeid(std::nullptr_t))
        throw std::runtime_error("Object reference not set to an instance of an object.");
    if (auto number = std::any_cast<std::int32_t>(&value))
        return *number;
    if (auto boxed = std::any_cast<network::Value>(&value)) {
        const auto& object = boxed->view();
        using Kind = network::Value::Kind;
        if (object.kind == Kind::Integer)
            return object.integer;
        if (object.kind == Kind::NullReference || object.kind == Kind::Undefined)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        std::string type;
        switch (object.kind) {
        case Kind::String:
            type = "System.String";
            break;
        case Kind::Long:
            type = "System.Int64";
            break;
        case Kind::Boolean:
            type = "System.Boolean";
            break;
        case Kind::Real:
            type = "System.Double";
            break;
        case Kind::Map:
            type = "System.Collections.Hashtable";
            break;
        case Kind::Array:
            type = "System.Collections.ArrayList";
            break;
        case Kind::Binary:
            type = "System.Byte[]";
            break;
        case Kind::Date:
            type = "System.DateTime";
            break;
        case Kind::Uuid:
            type = "OpenMetaverse.UUID";
            break;
        case Kind::Uri:
            type = "System.Uri";
            break;
        default:
            type = "System.Object";
            break;
        }
        throw std::runtime_error("Unable to cast object of type '" + type + "' to type 'System.Int32'.");
    }
    std::string type = "System.Object";
    if (value.type() == typeid(std::string) || value.type() == typeid(std::u16string))
        type = "System.String";
    else if (value.type() == typeid(std::int64_t))
        type = "System.Int64";
    else if (value.type() == typeid(bool))
        type = "System.Boolean";
    else if (value.type() == typeid(double))
        type = "System.Double";
    throw std::runtime_error("Unable to cast object of type '" + type + "' to type 'System.Int32'.");
}
AvatarProfileProperties property_reply(const framework::UserProfileProperties& p, const ProfileText& born,
                                       std::shared_ptr<std::vector<std::uint8_t>> membership,
                                       std::uint32_t flags) {
    AvatarProfileProperties out;
    out.avatar = p.UserId;
    out.about = p.AboutText;
    out.born = born;
    out.membership = std::move(membership);
    out.first_life_about = p.FirstLifeText;
    out.flags = flags;
    out.first_life_image = p.FirstLifeImageId;
    out.image = p.ImageId;
    out.url = p.WebUrl;
    out.partner = p.PartnerId;
    return out;
}
AvatarProfileInterests interest_reply(const framework::UserProfileProperties& p) {
    AvatarProfileInterests out;
    out.avatar = p.UserId;
    out.wants = static_cast<std::uint32_t>(p.WantToMask);
    out.want_text = p.WantToText;
    out.skills = static_cast<std::uint32_t>(p.SkillsMask);
    out.skill_text = p.SkillsText;
    out.languages = p.Language;
    return out;
}
}
bool UserProfileRequests::get_user_account_data(framework::Uuid user,
                                                std::shared_ptr<ProfileAccountData>& account) {
    account.reset();
    if (required(required(context_).users()).is_local(user)) {
        auto accounts = required(context_).account_service();
        auto scope = required(context_).region_scope();
        account = required(accounts).get(scope, user);
        return account != nullptr;
    }
    bool failed = false;
    auto home = required(required(context_).users()).server_url(user, u"HomeURI", failed);
    if (failed || !home || home->empty())
        return false;
    auto connector = required(context_).user_agent(home);
    std::shared_ptr<ProfileUserInfo> info;
    try {
        info = required(connector).get_info(user);
    } catch (const std::exception& error) {
        required(context_).debug(u"[PROFILES]: GetUserInfo call failed ",
                                 framework::utf16_replacing(error.what()));
        required(required(context_).users()).web_failed(user);
        return false;
    }
    if (required(info).empty())
        return false;
    account = std::make_shared<ProfileAccountData>();
    if (info->contains("user_flags"))
        account->flags = profile_boxed_int(info->at("user_flags"));
    if (info->contains("user_created"))
        account->created = profile_boxed_int(info->at("user_created"));
    account->title = u"HG Visitor";
    return true;
}
bool UserProfileRequests::get_profile_data(std::shared_ptr<framework::UserProfileProperties>& properties,
                                           bool foreign, const ProfileText& uri, ProfileText& message) {
    if (!uri || uri->empty()) {
        message = u"User profile service unknown at this time";
        return false;
    }
    std::shared_ptr<server::JsonRpcMembers> data = properties;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"avatar_properties_request", uri, id)) {
        bool second = false;
        if (foreign) {
            try {
                if (required(context_).legacy_profile(properties, uri))
                    second = true;
            } catch (const std::exception& error) {
                required(context_).debug(u"[PROFILES]: Request using the OpenProfile API for user " +
                                         framework::utf16(required(properties).UserId.str()) + u" to " +
                                         *uri + u" failed: " + framework::utf16_replacing(error.what()));
            }
        }
        if (!second) {
            message = u"JsonRpcRequest for user " + framework::utf16(required(properties).UserId.str()) +
                      u" to " + *uri + u" failed";
            required(context_).debug(u"[PROFILES]: " + *message);
            return false;
        }
    }
    properties = cast<framework::UserProfileProperties>(data, "OpenSim.Framework.UserProfileProperties");
    if (foreign) {
        cache_foreign_image(required(properties).UserId, required(properties).ImageId);
        cache_foreign_image(required(properties).UserId, required(properties).FirstLifeImageId);
    }
    message = u"Success";
    return true;
}
bool UserProfileRequests::is_friend_online(std::shared_ptr<ProfileRequestClient> client,
                                           framework::Uuid avatar) {
    auto p = required(context_).presence(avatar);
    if (p && !p->child && !p->deleted)
        return true;
    auto friends = required(context_).friends();
    if (friends && friends->online(required(client).agent_id(), avatar))
        return true;
    auto own = required(client).scene_presence();
    if (own && own->viewer_ui_god()) {
        auto service = required(context_).presence_service();
        auto agents = service ? service->agents({avatar.str()}) : nullptr;
        return agents && agents->count > 0;
    }
    return false;
}
void UserProfileRequests::request_avatar_properties(std::shared_ptr<ProfileRequestClient> client,
                                                    framework::Uuid avatar) {
    if (avatar.is_zero()) {
        required(context_).debug(u"[PROFILES]: got request of null ID");
        return;
    }
    if (avatar == system_profile_agent) {
        AvatarProfileProperties p;
        p.avatar = avatar;
        p.about = u"Creator of OpenSimulator shared assets library";
        p.born = required(context_).system_agent_born();
        p.membership = std::make_shared<std::vector<std::uint8_t>>(profile_string_bytes(u"System agent"));
        p.first_life_about = u"MrOpenSim has no life";
        p.flags = 0x10;
        p.url = u"";
        required(client).send_properties(p);
        AvatarProfileInterests interests;
        interests.avatar = avatar;
        interests.want_text = u"";
        interests.skill_text = u"Getting into trouble";
        interests.languages = u"Droidspeak";
        required(client).send_interests(interests);
        return;
    }
    auto presence = find_presence(avatar);
    if (presence && presence->npc) {
        AvatarProfileProperties p;
        p.avatar = avatar;
        p.about = required(presence->npc_client()).about();
        p.born = required(presence->npc_client()).born();
        p.membership =
            std::make_shared<std::vector<std::uint8_t>>(profile_string_bytes(u"Non Player Character (NPC)"));
        p.first_life_about = u"NPCs have no life";
        p.flags = 0x10;
        p.image = required(presence->npc_client()).image();
        p.url = u"";
        required(client).send_properties(p);
        AvatarProfileInterests interests;
        interests.avatar = avatar;
        interests.want_text = u"";
        interests.skill_text = u"Getting into trouble";
        interests.languages = u"Droidspeak";
        required(client).send_interests(interests);
        return;
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> entry;
        if (profiles_.try_get_value(avatar, entry) && entry) {
            if (entry->props) {
                auto props = entry->props;
                auto flags = entry->flags;
                if (is_friend_online(client, avatar))
                    flags = 0x10;
                else
                    flags &= ~std::uint32_t{0x10};
                required(client).send_properties(
                    property_reply(*props, entry->born, entry->membershipType, flags));
                required(client).send_interests(interest_reply(*props));
                if (entry->avatarGroups)
                    required(client).send_groups(avatar, entry->avatarGroups);
                return;
            }
            if (!entry->ClientsWaitingProps)
                entry->ClientsWaitingProps = std::make_shared<framework::ProfileClientSet>();
            else if (entry->ClientsWaitingProps->contains(client))
                return;
            entry->ClientsWaitingProps->insert(client);
        } else {
            entry = std::make_shared<framework::UserProfileCacheEntry>();
            entry->ClientsWaitingProps = std::make_shared<framework::ProfileClientSet>();
            entry->ClientsWaitingProps->insert(client);
            profiles_.add_or_update(avatar, entry, 300.0);
        }
    }
    AsyncRequest request{client, presence, avatar, 0};
    bool local = required(required(context_).users()).is_local(avatar);
    auto& queue = local ? local_ : hg_;
    {
        std::lock_guard lock(queue.stack);
        queue.items.push_back(std::move(request));
        if (queue.running)
            return;
        queue.running = true;
    }
    auto self = shared_from_this();
    try {
        required(context_).schedule([self, local] {
            if (local)
                self->process_local_requests();
            else
                self->process_hg_requests();
        });
    } catch (...) {
        std::lock_guard lock(queue.stack);
        queue.running = false;
        throw;
    }
}
void UserProfileRequests::process_request(AsyncRequest request) {
    try {
        auto client = request.client;
        if (request.type != 0)
            return;
        ProfileText uri;
        bool foreign = profile_server_uri(request.agent, uri);
        if (!uri)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        bool ok = !uri->empty();
        auto membership = std::make_shared<std::vector<std::uint8_t>>(1);
        ProfileText born = u"";
        std::uint32_t flags = 0;
        std::shared_ptr<ProfileAccountData> account;
        if (ok && get_user_account_data(request.agent, account)) {
            flags = static_cast<std::uint32_t>(account->flags & 0xff);
            if (!account->title)
                throw std::runtime_error("Object reference not set to an instance of an object.");
            if (account->title->empty())
                (*membership)[0] = static_cast<std::uint8_t>((account->flags & 0x0f00) >> 8);
            else
                membership =
                    std::make_shared<std::vector<std::uint8_t>>(profile_string_bytes(*account->title));
            if (account->created != 0)
                born = profile_date(account->created);
        } else
            ok = false;
        auto props = std::make_shared<framework::UserProfileProperties>();
        props->UserId = request.agent;
        if (ok) {
            ProfileText result;
            ok = get_profile_data(props, foreign, uri, result);
        }
        if (!ok)
            required(props).AboutText =
                u"Profile not available at this time. User may still be unknown to this grid";
        if (!required(context_).allow_web_urls())
            required(props).WebUrl = u"";
        std::shared_ptr<ProfileMemberships> groups;
        if (ok) {
            auto module = required(context_).groups();
            if (module)
                groups = module->memberships(request.agent);
        }
        std::shared_ptr<framework::ProfileClientSet> clients;
        {
            std::lock_guard lock(profile_mutex_);
            std::shared_ptr<framework::UserProfileCacheEntry> entry;
            auto key = required(props).UserId;
            if (!profiles_.try_get_value(key, entry) || !entry)
                entry = std::make_shared<framework::UserProfileCacheEntry>();
            entry->props = props;
            entry->born = born;
            entry->membershipType = membership;
            entry->flags = flags;
            clients = entry->ClientsWaitingProps;
            entry->ClientsWaitingProps.reset();
            entry->avatarGroups = groups;
            profiles_.add_or_update(key, entry, 300.0);
        }
        if (is_friend_online(request.client, request.agent))
            flags |= 0x10;
        else
            flags &= ~std::uint32_t{0x10};
        auto send = [&](const std::shared_ptr<ProfileRequestClient>& c) {
            required(c).send_properties(property_reply(required(props), born, membership, flags));
            required(c).send_interests(interest_reply(required(props)));
            if (groups)
                required(c).send_groups(request.agent, groups);
        };
        if (!clients)
            send(client);
        else {
            if (!clients->contains(client) && required(client).is_active())
                send(client);
            for (const auto& c : *clients) {
                if (!required(c).is_active())
                    continue;
                send(c);
            }
        }
    } catch (const std::exception& error) {
        required(context_).process_error(error);
    }
}
void UserProfileRequests::process_queue(WorkQueue& queue) {
    std::lock_guard lock(queue.gate);
    for (;;) {
        AsyncRequest request;
        {
            std::lock_guard items(queue.stack);
            if (queue.items.empty()) {
                queue.running = false;
                return;
            }
            request = std::move(queue.items.back());
            queue.items.pop_back();
        }
        try {
            if (required(request.client).is_active())
                process_request(std::move(request));
        } catch (...) {
            std::lock_guard items(queue.stack);
            queue.running = false;
            throw;
        }
    }
}
void UserProfileRequests::process_local_requests() {
    process_queue(local_);
}
void UserProfileRequests::process_hg_requests() {
    process_queue(hg_);
}
std::size_t UserProfileRequests::queue_count(bool hg) const {
    const auto& queue = hg ? hg_ : local_;
    std::lock_guard lock(queue.stack);
    return queue.items.size();
}
void UserProfileRequests::avatar_interests_update(std::shared_ptr<ProfileRequestClient> client,
                                                  std::uint32_t wants, const ProfileText& want_text,
                                                  std::uint32_t skills, const ProfileText& skill_text,
                                                  const ProfileText& languages) {
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    auto props = std::make_shared<framework::UserProfileProperties>();
    props->UserId = required(client).agent_id();
    props->WantToMask = std::bit_cast<std::int32_t>(wants);
    props->WantToText = want_text;
    props->SkillsMask = std::bit_cast<std::int32_t>(skills);
    props->SkillsText = skill_text;
    props->Language = languages;
    std::shared_ptr<server::JsonRpcMembers> data = props;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"avatar_interests_update", uri, id)) {
        required(client).alert(u"Error updating interests", false);
        return;
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> e;
        if (profiles_.try_get_value(required(client).agent_id(), e) && e) {
            e->props.reset();
            e->ClientsWaitingProps.reset();
        }
    }
    request_avatar_properties(client, required(client).agent_id());
}
void UserProfileRequests::avatar_properties_update(std::shared_ptr<ProfileRequestClient> client,
                                                   std::shared_ptr<framework::UserProfileProperties> props) {
    ProfileText uri;
    profile_server_uri(required(client).agent_id(), uri);
    if (blank(uri))
        return;
    if (!required(context_).allow_web_urls())
        required(props).WebUrl = u"";
    std::shared_ptr<server::JsonRpcMembers> data = props;
    auto id = required(context_).new_rpc_id();
    if (!required(context_).request_object(data, u"avatar_properties_update", uri, id)) {
        required(client).alert(u"Error updating properties", false);
        return;
    }
    {
        std::lock_guard lock(profile_mutex_);
        std::shared_ptr<framework::UserProfileCacheEntry> e;
        if (profiles_.try_get_value(required(client).agent_id(), e) && e) {
            e->props.reset();
            e->ClientsWaitingProps.reset();
        }
    }
    request_avatar_properties(client, required(client).agent_id());
}

}

namespace opensim::scene {
NativeProfileUserDirectory::NativeProfileUserDirectory(services::AccountStore& accounts,
                                                       framework::Uuid scope, UrlLookup urls)
    : accounts_(accounts), scope_(scope), urls_(std::move(urls)) {
}
bool NativeProfileUserDirectory::is_local(framework::Uuid user) {
    return accounts_.find_by_id(scope_.str(), user.str()).has_value();
}
void NativeProfileUserDirectory::web_failed(framework::Uuid user) {
    std::lock_guard lock(mutex_);
    failed_[user] = std::chrono::steady_clock::now();
}
ProfileText NativeProfileUserDirectory::lookup(framework::Uuid user, std::u16string_view key, bool& failed,
                                               bool include_failed) {
    failed = false;
    if (is_local(user))
        return u"";
    {
        std::lock_guard lock(mutex_);
        if (auto it = failed_.find(user); it != failed_.end()) {
            auto elapsed = std::chrono::steady_clock::now() - it->second;
            failed = include_failed ? elapsed <= std::chrono::minutes(2) : elapsed < std::chrono::minutes(2);
            if (!failed)
                failed_.erase(it);
        }
    }
    if ((!include_failed && failed) || !urls_)
        return u"";
    auto urls = urls_(user);
    if (!urls)
        return u"";
    auto it = urls->find(framework::utf8(key));
    if (it == urls->end())
        return u"";
    auto text = std::any_cast<std::string>(&it->second);
    if (!text)
        return u"";
    framework::HttpHost host(*text);
    return host.valid() ? framework::utf16_replacing(host.uri) : std::u16string{};
}
ProfileText NativeProfileUserDirectory::server_url(framework::Uuid user, std::u16string_view key,
                                                   bool& failed) {
    return lookup(user, key, failed, true);
}
ProfileText NativeProfileUserDirectory::server_url(framework::Uuid user, std::u16string_view key) {
    bool failed;
    return lookup(user, key, failed, false);
}

NativeProfileHomeGrid::NativeProfileHomeGrid(const server::GridIdentity& grid) : grid_(grid) {
    has_hg = grid.has_hypergrid;
}
ProfileText NativeProfileHomeGrid::gatekeeper_url() {
    auto uri = grid_.gate_uri();
    if (!uri)
        return {};
    return framework::utf16_replacing(*uri);
}
int NativeProfileHomeGrid::is_local_grid(const ProfileText& gatekeeper, bool resolve) {
    std::optional<std::string> uri;
    if (gatekeeper)
        uri = framework::utf8(*gatekeeper);
    return grid_.is_local_grid(uri, resolve);
}

std::shared_ptr<ProfileAgents> NativeProfilePresenceService::agents(const std::vector<std::string>& users) {
    return std::make_shared<ProfileAgents>(ProfileAgents{presence_.agents(users).size()});
}

std::shared_ptr<ProfileRequestGridRegion> NativeProfileGridService::region_by_name(framework::Uuid scope,
                                                                                   const ProfileText& name) {
    std::optional<std::string> query;
    if (name)
        query = framework::utf8(*name);
    const auto region = grid_.by_name(scope, query);
    if (!region)
        return {};
    return std::make_shared<ProfileRequestGridRegion>(ProfileRequestGridRegion{region->x, region->y});
}

std::shared_ptr<ProfileAccountData> NativeProfileAccounts::get(framework::Uuid scope, framework::Uuid user) {
    const auto account = accounts_.find_by_id(scope.str(), user.str());
    if (!account)
        return {};
    auto data = std::make_shared<ProfileAccountData>();
    data->flags = account->user_flags;
    data->created = account->created;
    if (account->user_title)
        data->title = framework::utf16_replacing(*account->user_title);
    return data;
}

bool UserProfileRequests::dispatch_viewer_packet(const network::lludp::Packet& packet,
                                                 const std::shared_ptr<ProfileRequestClient>& client) {
    const auto& name = packet.spec->name;
    const auto field_text = [](const auto& block, const char* field) {
        return framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(block.at(field)));
    };
    if (name == "GenericMessage") {
        const auto generic_text = [](const auto& block, const char* field) {
            return framework::field_to_string(std::get<std::vector<std::uint8_t>>(block.at(field)));
        };
        const auto method = framework::invariant_lower_utf16(
            framework::utf16(framework::trim_utf8(generic_text(packet.block("MethodData"), "Method"))));
        using Handler = void (UserProfileRequests::*)(std::shared_ptr<AdvancedProfileClient>,
                                                      const std::vector<ProfileText>*);
        Handler handler = nullptr;
        if (method == u"avatarclassifiedsrequest")
            handler = &UserProfileRequests::classifieds_request;
        else if (method == u"avatarpicksrequest")
            handler = &UserProfileRequests::picks_request;
        else if (method == u"pickinforequest")
            handler = &UserProfileRequests::pick_info_request;
        else if (method == u"avatarnotesrequest")
            handler = &UserProfileRequests::notes_request;
        if (!handler)
            return false;
        std::vector<ProfileText> args;
        if (const auto blocks = packet.blocks.find("ParamList"); blocks != packet.blocks.end()) {
            args.reserve(blocks->second.size());
            for (const auto& block : blocks->second)
                args.emplace_back(framework::utf16(generic_text(block, "Parameter")));
        }
        try {
            (this->*handler)(client, &args);
        } catch (const std::exception& error) {
            required(context_).process_error(error);
        }
        return true;
    }
    if (name == "AvatarPropertiesRequest") {
        request_avatar_properties(client,
                                  std::get<framework::Uuid>(packet.block("AgentData").at("AvatarID")));
    } else if (name == "UserInfoRequest") {
        preferences_request(client);
    } else if (name == "UpdateUserInfo") {
        const auto& data = packet.block("UserData");
        preferences_update(std::get<bool>(data.at("IMViaEMail")),
                           field_text(data, "DirectoryVisibility") != u"hidden", client);
    } else if (name == "AvatarNotesUpdate") {
        const auto& data = packet.block("Data");
        notes_update(client, std::get<framework::Uuid>(data.at("TargetID")), field_text(data, "Notes"));
    } else if (name == "AvatarInterestsUpdate") {
        const auto& data = packet.block("PropertiesData");
        avatar_interests_update(client,
                                static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("WantToMask"))),
                                field_text(data, "WantToText"),
                                static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("SkillsMask"))),
                                field_text(data, "SkillsText"), field_text(data, "LanguagesText"));
    } else if (name == "AvatarPropertiesUpdate") {
        const auto& data = packet.block("PropertiesData");
        auto profile = std::make_shared<framework::UserProfileProperties>();
        profile->UserId = client->agent_id();
        profile->WebUrl = field_text(data, "ProfileURL");
        profile->ImageId = std::get<framework::Uuid>(data.at("ImageID"));
        profile->FirstLifeImageId = std::get<framework::Uuid>(data.at("FLImageID"));
        profile->AboutText = field_text(data, "AboutText");
        profile->FirstLifeText = field_text(data, "FLAboutText");
        profile->PublishProfile = std::get<bool>(data.at("AllowPublish"));
        profile->PublishMature = std::get<bool>(data.at("MaturePublish"));
        avatar_properties_update(client, std::move(profile));
    } else if (name == "PickInfoUpdate") {
        const auto& data = packet.block("Data");
        ProfilePickUpdate update;
        update.pick = std::get<framework::Uuid>(data.at("PickID"));
        update.creator = std::get<framework::Uuid>(data.at("CreatorID"));
        update.snapshot = std::get<framework::Uuid>(data.at("SnapshotID"));
        update.top_pick = std::get<bool>(data.at("TopPick"));
        update.enabled = std::get<bool>(data.at("Enabled"));
        update.name = field_text(data, "Name").substr(0, 128);
        update.description = field_text(data, "Desc").substr(0, 1022);
        update.sort_order = static_cast<std::int32_t>(std::get<std::int64_t>(data.at("SortOrder")));
        pick_info_update(client, update);
    } else if (name == "ClassifiedInfoUpdate") {
        const auto& data = packet.block("Data");
        const auto& pos = std::get<std::vector<double>>(data.at("PosGlobal"));
        ProfileClassifiedUpdate update;
        update.classified = std::get<framework::Uuid>(data.at("ClassifiedID"));
        update.parcel = std::get<framework::Uuid>(data.at("ParcelID"));
        update.snapshot = std::get<framework::Uuid>(data.at("SnapshotID"));
        update.category = static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("Category")));
        update.parent_estate = static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("ParentEstate")));
        update.name = field_text(data, "Name");
        update.description = field_text(data, "Desc");
        update.global_position = {static_cast<float>(pos.at(0)), static_cast<float>(pos.at(1)),
                                  static_cast<float>(pos.at(2))};
        update.flags = static_cast<std::uint8_t>(std::get<std::uint64_t>(data.at("ClassifiedFlags")));
        if ((update.flags & 0x4e) == 0)
            update.flags |= 0x4;
        update.price = static_cast<std::int32_t>(std::get<std::int64_t>(data.at("PriceForListing")));
        classified_info_update(update, client);
    } else if (name == "PickDelete") {
        pick_delete(client, std::get<framework::Uuid>(packet.block("Data").at("PickID")));
    } else if (name == "ClassifiedInfoRequest") {
        classified_info_request(std::get<framework::Uuid>(packet.block("Data").at("ClassifiedID")), client);
    } else if (name == "ClassifiedDelete") {
        classified_delete(std::get<framework::Uuid>(packet.block("Data").at("ClassifiedID")), client);
    } else {
        return false;
    }
    return true;
}

void ProfileViewerReplies::send_properties(const AvatarProfileProperties& data) {
    send_profile_packet(avatar_profile_properties_packet(agent_id(), data));
}
void ProfileViewerReplies::send_interests(const AvatarProfileInterests& data) {
    send_profile_packet(avatar_profile_interests_packet(agent_id(), data));
}
void ProfileViewerReplies::send_notes(framework::Uuid target, const ProfileText& notes) {
    send_profile_packet(avatar_profile_notes_packet(agent_id(), target, notes));
}
void ProfileViewerReplies::send_preferences(bool email, bool visible, const ProfileText& address) {
    send_profile_packet(user_profile_info_packet(agent_id(), email, visible, address));
}
void ProfileViewerReplies::send_picks(framework::Uuid target,
                                      std::shared_ptr<framework::UserProfileNameList> picks) {
    send_profile_packet(avatar_picks_list_packet(agent_id(), target, picks.get()));
}
void ProfileViewerReplies::send_classifieds(framework::Uuid target,
                                            std::shared_ptr<framework::UserProfileNameList> classifieds) {
    send_profile_packet(avatar_classifieds_list_packet(agent_id(), target, classifieds.get()));
}
void ProfileViewerReplies::send_pick_info(const AvatarPickInfo& data) {
    send_profile_packet(avatar_pick_info_packet(agent_id(), data));
}
void ProfileViewerReplies::send_classified_info(const AvatarClassifiedInfo& data) {
    send_profile_packet(avatar_classified_info_packet(agent_id(), data));
}
void ProfileViewerReplies::send_groups(framework::Uuid target,
                                       std::shared_ptr<ProfileMemberships> memberships) {
    auto events = profile_event_queue();
    if (!events)
        return;
    std::vector<std::shared_ptr<AvatarProfileGroup>> groups;
    if (memberships) {
        groups.reserve(memberships->size());
        for (const auto& entry : *memberships) {
            groups.push_back(entry ? std::make_shared<AvatarProfileGroup>(AvatarProfileGroup{
                                         entry->GroupID, entry->GroupPicture, entry->GroupName,
                                         entry->GroupTitle, entry->GroupPowers, entry->ListInProfile})
                                   : nullptr);
        }
    }
    send_avatar_groups(agent_id(), target, memberships ? &groups : nullptr, events.get());
}
}
