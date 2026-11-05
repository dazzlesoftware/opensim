// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/profile_handlers.hpp"
#include "opensim/framework/sdk_uuid.hpp"
namespace opensim::server {
namespace {
using network::Value;
using K = Value::Kind;
using namespace framework;
[[noreturn]] void null_reference() {
    throw std::runtime_error("Object reference not set to an instance of an object.");
}
template <class T> T& required(const std::shared_ptr<T>& p) {
    if (!p)
        null_reference();
    return *p;
}
std::string cast_message(K kind, const char* target) {
    const char* type = "OSD";
    switch (kind) {
    case K::Map:
        type = "OSDMap";
        break;
    case K::Array:
        type = "OSDArray";
        break;
    case K::String:
        type = "OSDString";
        break;
    case K::Boolean:
        type = "OSDBoolean";
        break;
    case K::Integer:
        type = "OSDInteger";
        break;
    case K::Long:
        type = "OSDLong";
        break;
    case K::Real:
        type = "OSDReal";
        break;
    case K::Uuid:
        type = "OSDUUID";
        break;
    case K::Date:
        type = "OSDDate";
        break;
    case K::Uri:
        type = "OSDUri";
        break;
    case K::Binary:
        type = "OSDBinary";
        break;
    default:
        break;
    }
    return std::string("Unable to cast object of type 'OpenMetaverse.StructuredData.") + type +
           "' to type 'OpenMetaverse.StructuredData." + target + "'.";
}
const Value& as_map(const Value& value) {
    const auto& v = value.view();
    if (v.kind != K::Map)
        throw std::runtime_error(cast_message(v.kind, "OSDMap"));
    return v;
}
bool map_value(const Value& value) {
    return value.kind != K::NullReference && value.view().kind == K::Map;
}
template <class T> std::shared_ptr<T> model(const Value& params) {
    auto result = std::make_shared<T>();
    result->deserialize_members(params);
    return result;
}
template <class T> void serialized(JsonRpcResponse& response, const std::shared_ptr<T>& value) {
    response.result = std::make_shared<Value>(required(value).serialize_members());
}
void array_result(const std::shared_ptr<JsonRpcResponse>& response, std::shared_ptr<Value> value) {
    if (value && value->view().kind != K::Array)
        throw std::runtime_error(cast_message(value->view().kind, "OSDArray"));
    required(response).result = std::move(value);
}
}
void JsonRpcProfileHandlers::register_handlers(JsonRpcDispatcher& dispatcher) const {
    for (auto method :
         {u"avatarclassifiedsrequest", u"classified_update", u"classifieds_info_query", u"classified_delete",
          u"avatarpicksrequest", u"pickinforequest", u"picks_update", u"picks_delete", u"avatarnotesrequest",
          u"avatar_notes_update", u"avatar_properties_request", u"avatar_properties_update",
          u"avatar_interests_update", u"user_preferences_update", u"user_preferences_request",
          u"image_assets_request", u"user_data_request", u"user_data_update"})
        dispatcher.add(method, [copy = *this, name = std::u16string(method)](
                                   const Value& json, std::shared_ptr<JsonRpcResponse>& response) {
            return copy.handle(name, json, response);
        });
}
bool JsonRpcProfileHandlers::handle(std::u16string_view method, const Value& json,
                                    std::shared_ptr<JsonRpcResponse>& response) const {
    auto fail = [&](int code, std::optional<std::u16string> message = {}, std::u16string_view log = {}) {
        auto& error = required(required(response).error);
        error.code(code);
        if (message)
            error.message(message);
        if (logger_ && !log.empty())
            logger_(ProfileLogLevel::Debug, log);
        return false;
    };
    auto params = [&](bool typed, std::optional<std::u16string> message,
                      std::u16string_view log) -> const Value* {
        if (!json.contains("params") || (typed && !map_value(json.get("params")))) {
            fail(JsonRpcErrorCode::ParseError, message, log);
            return nullptr;
        }
        return &as_map(json.get("params"));
    };
    auto error = [&](const std::optional<std::u16string>& text) {
        return fail(JsonRpcErrorCode::InternalError, text.value_or(u""));
    };
    auto service = [&]() -> services::UserProfilesService& { return required(service_); };
    std::optional<std::u16string> result = u"";
    if (method == u"avatarclassifiedsrequest" || method == u"image_assets_request" ||
        method == u"classified_delete") {
        auto p = params(false, {},
                        method == u"avatarclassifiedsrequest" ? u"Classified Request"
                        : method == u"classified_delete"      ? u"Classified Delete Request"
                                                              : u"Avatar Image Assets Request");
        if (!p)
            return false;
        auto id = construct_sdk_uuid(
            network::osd_string_utf16(p->get(method == u"avatarclassifiedsrequest" ? "creatorId"
                                             : method == u"classified_delete"      ? "classifiedId"
                                                                                   : "avatarId")));
        if (method == u"classified_delete")
            return service().ClassifiedDelete(id) || error(u"data error removing record");
        array_result(response, method == u"avatarclassifiedsrequest"
                                   ? service().AvatarClassifiedsRequest(id)
                                   : service().AvatarImageAssetsRequest(id));
        return true;
    }
    if (method == u"avatarpicksrequest" || method == u"picks_delete") {
        bool list = method == u"avatarpicksrequest";
        auto p = params(true, {}, list ? u"Avatar Picks Request" : u"Avatar Picks Delete Request");
        if (!p)
            return false;
        const char* key = list ? "creatorId" : "pickId";
        if (!p->contains(key))
            return fail(JsonRpcErrorCode::ParseError, u"missing parameters",
                        list ? u"Avatar Picks Request missing Parameters"
                             : u"Avatar Pick Delete missing Parameter");
        auto id = network::osd_uuid(p->get(key));
        if (list) {
            array_result(response, service().AvatarPicksRequest(id));
            return true;
        }
        return service().PicksDelete(id) || error(u"data error removing record");
    }
    if (method == u"classified_update" || method == u"classifieds_info_query") {
        bool update = method == u"classified_update";
        auto p = params(true, update ? u"Error parsing classified update request" : u"no parameters supplied",
                        update ? u"Classified Update Request" : u"Classified Info Request");
        if (!p)
            return false;
        auto ad = model<UserClassifiedAdd>(*p);
        if (update && (ad->Flags & 0x4e) == 0)
            ad->Flags |= 0x04;
        bool ok =
            update ? service().ClassifiedUpdate(ad, result) : service().ClassifiedInfoRequest(ad, result);
        if (!ok)
            return error(result);
        if (!update && (required(ad).Flags & 0x4e) == 0)
            ad->Flags |= 0x04;
        serialized(required(response), ad);
        return true;
    }
    if (method == u"pickinforequest" || method == u"picks_update") {
        bool info = method == u"pickinforequest";
        auto p =
            params(true, u"no parameters supplied",
                   info ? u"Avatar Picks Info Request Invalid Parameters" : u"Avatar Picks Update Request");
        if (!p)
            return false;
        std::shared_ptr<UserProfilePick> pick;
        if (info) {
            if (!p->contains("PickId"))
                return fail(JsonRpcErrorCode::ParseError, u"missing parameters",
                            u"Avatar Picks Info Request missing Parameters");
            auto id = network::osd_uuid(p->get("PickId"));
            if (!p->contains("CreatorId"))
                return fail(JsonRpcErrorCode::ParseError, u"missing parameters",
                            u"Avatar Picks Info Request missing Parameters");
            auto creator = network::osd_uuid(p->get("CreatorId"));
            pick = std::make_shared<UserProfilePick>();
            pick->PickId = id;
            pick->CreatorId = creator;
        } else
            pick = model<UserProfilePick>(*p);
        bool ok = info ? service().PickInfoRequest(pick, result) : service().PicksUpdate(pick, result);
        if (!ok)
            return error(info ? result : std::optional<std::u16string>(u"unable to update pick"));
        serialized(required(response), pick);
        return true;
    }
    if (method == u"avatarnotesrequest" || method == u"avatar_notes_update") {
        bool update = method == u"avatar_notes_update";
        auto p = params(true, update ? u"No parameters" : u"Params missing",
                        update ? u"Avatar Notes Update Request" : u"Avatar Notes Request");
        if (!p)
            return false;
        auto note = model<UserProfileNotes>(*p);
        bool ok = update ? service().NotesUpdate(note, result) : service().AvatarNotesRequest(note);
        if (ok) {
            serialized(required(response), note);
            return true;
        }
        return update || error(u"Error reading notes");
    }
    if (method == u"avatar_properties_request" || method == u"avatar_properties_update" ||
        method == u"avatar_interests_update") {
        auto p = params(false, u"no parameters supplied",
                        method == u"avatar_properties_request"  ? u"Avatar Properties Request"
                        : method == u"avatar_properties_update" ? u"Avatar Properties Update Request"
                                                                : u"Avatar Interests Update Request");
        if (!p)
            return false;
        auto props = model<UserProfileProperties>(*p);
        bool ok = method == u"avatar_properties_request"  ? service().AvatarPropertiesRequest(props, result)
                  : method == u"avatar_properties_update" ? service().AvatarPropertiesUpdate(props, result)
                                                          : service().AvatarInterestsUpdate(props, result);
        if (!ok)
            return error(result);
        serialized(required(response), props);
        return true;
    }
    if (method == u"user_preferences_request" || method == u"user_preferences_update") {
        bool update = method == u"user_preferences_update";
        auto p =
            params(false, update ? std::optional<std::u16string>(u"no parameters supplied") : std::nullopt,
                   update ? u"User Preferences Update Request" : u"User Preferences Request");
        if (!p)
            return false;
        auto prefs = model<UserPreferences>(*p);
        bool ok = update ? service().UserPreferencesUpdate(prefs, result)
                         : service().UserPreferencesRequest(prefs, result);
        if (ok) {
            serialized(required(response), prefs);
            return true;
        }
        error(result);
        if (update && logger_)
            logger_(ProfileLogLevel::Info, u"[PROFILES]: User preferences update error - " +
                                               required(required(response).error).message().value_or(u""));
        return false;
    }
    if (method == u"user_data_request" || method == u"user_data_update") {
        bool update = method == u"user_data_update";
        auto p = params(false, u"no parameters supplied",
                        update ? u"User App Data Update Request"
                               : u"User Application Service URL Request: No Parameters!");
        if (!p)
            return false;
        auto props = model<UserAppData>(*p);
        bool ok =
            update ? service().SetUserAppData(props, result) : service().RequestUserAppData(props, result);
        if (!ok)
            return error(result);
        if (update)
            serialized(required(response), props);
        else {
            auto token = Value(utf8(result.value_or(u"")));
            token.sdk_string = std::make_shared<const std::u16string>(result.value_or(u""));
            required(response).result = std::make_shared<Value>(
                Value::object({{"result", Value("success")}, {"token", std::move(token)}}));
        }
        return true;
    }
    throw std::invalid_argument("Unknown profile handler");
}
}
