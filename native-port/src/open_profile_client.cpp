// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/open_profile_client.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/network/osd_value.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace opensim::services {
namespace {
using network::Value;
using Kind = Value::Kind;
[[noreturn]] void null_reference() {
    throw OpenProfileError("System.NullReferenceException",
                           u"Object reference not set to an instance of an object.");
}
bool is_null(const Value& value) {
    if (value.reference)
        return is_null(*value.reference);
    return value.kind == Kind::NullReference || value.kind == Kind::Undefined;
}
std::string type(const Value& value) {
    switch (value.view().kind) {
    case Kind::Map:
        return "System.Collections.Hashtable";
    case Kind::Array:
        return "System.Collections.ArrayList";
    case Kind::Integer:
        return "System.Int32";
    case Kind::Long:
        return "System.Int64";
    case Kind::Boolean:
        return "System.Boolean";
    case Kind::Real:
        return "System.Double";
    case Kind::Binary:
        return "System.Byte[]";
    case Kind::Date:
        return "System.DateTime";
    default:
        return "System.String";
    }
}
const Value& require_kind(const Value& value, Kind kind, const char* name) {
    auto& v = value.view();
    if (v.kind != kind)
        throw OpenProfileError("System.InvalidCastException", u"Unable to cast object of type '" +
                                                                  framework::utf16(type(v)) + u"' to type '" +
                                                                  framework::utf16(name) + u"'.");
    return v;
}
std::u16string object_text(const Value& value) {
    const auto& v = value.view();
    switch (v.kind) {
    case Kind::String:
        return v.sdk_string ? *v.sdk_string : framework::utf16_replacing(v.text);
    case Kind::Map:
    case Kind::Array:
    case Kind::Binary:
        return framework::utf16(type(v));
    case Kind::Boolean:
        return v.boolean ? u"True" : u"False";
    case Kind::Real: {
        if (std::isinf(v.real))
            return std::signbit(v.real) ? u"-Infinity" : u"Infinity";
        auto text = network::osd_string(v);
        if (auto e = text.find('e'); e != std::string::npos)
            text[e] = 'E';
        return framework::utf16_replacing(text);
    }
    case Kind::Date: {
        if (!v.sdk_date)
            throw std::invalid_argument("Boxed DateTime requires ticks");
        constexpr std::int64_t day_ticks = 864000000000ll;
        auto ticks = v.sdk_date->ticks();
        auto date =
            std::chrono::year_month_day(std::chrono::sys_days(std::chrono::days(ticks / day_ticks - 719162)));
        auto time = ticks % day_ticks;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setfill('0') << std::setw(2) << unsigned(date.month()) << '/' << std::setw(2)
            << unsigned(date.day()) << '/' << std::setw(4) << int(date.year()) << ' ' << std::setw(2)
            << time / 36000000000ll << ':' << std::setw(2) << time / 600000000ll % 60 << ':' << std::setw(2)
            << time / 10000000ll % 60;
        return framework::utf16(out.str());
    }
    default:
        return framework::utf16_replacing(v.as_string());
    }
}
}
std::u16string profile_boxed_object_text(const Value& value) {
    return object_text(value);
}
OpenProfileClient::OpenProfileClient(Text server, Requester requester, ObjectText formatter)
    : server_uri_(std::move(server)), request_(std::move(requester)), object_text_(std::move(formatter)) {
    if (!request_)
        throw std::invalid_argument("OpenProfile XML-RPC requester is required");
    if (!object_text_)
        object_text_ = object_text;
}
bool OpenProfileClient::request_avatar_properties(std::shared_ptr<framework::UserProfileProperties>& props) {
    if (!props) {
        null_reference();
    }
    auto request = Value::object({{"avatar_id", Value(props->UserId.str())}});
    auto response = request_(request, u"avatar_properties_request", server_uri_);
    if (!response || is_null(*response))
        return false;
    const auto& outer = require_kind(*response, Kind::Map, "System.Collections.Hashtable");
    if (!outer.contains("data"))
        return false;
    auto& data = outer.get("data");
    if (is_null(data))
        return false;
    const auto& array = require_kind(data, Kind::Array, "System.Collections.ArrayList");
    if (array.array.empty())
        throw OpenProfileError(
            "System.ArgumentOutOfRangeException",
            u"Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    if (is_null(array.array[0]))
        return false;
    const auto& profile = require_kind(array.array[0], Kind::Map, "System.Collections.Hashtable");
    // Source uses the ref parameter after the callback; it may have changed there.
    if (!props) {
        null_reference();
    }
    props->WebUrl = u"";
    props->AboutText = u"";
    props->FirstLifeText = u"";
    props->ImageId = {};
    props->FirstLifeImageId = {};
    props->PartnerId = {};
    // Retain the evaluated assignment receiver across object.ToString callbacks.
    // CLR checks a null receiver at the final field store, after evaluating RHS.
    auto store = [&](const auto& receiver, auto member, auto value) {
        if (!receiver)
            null_reference();
        receiver.get()->*member = std::move(value);
    };
    auto text = [&](const char* key, auto member) {
        const auto& value = profile.get(key);
        if (!is_null(value)) {
            auto receiver = props;
            auto result = object_text_(value);
            store(receiver, member, std::move(result));
        }
    };
    auto uuid = [&](const char* key, auto member) {
        const auto& value = profile.get(key);
        if (!is_null(value)) {
            auto receiver = props;
            auto text = object_text_(value);
            framework::Uuid result;
            try {
                result = framework::construct_sdk_uuid(text.value_or(u""));
            } catch (const std::invalid_argument& e) {
                throw OpenProfileError("System.FormatException", framework::utf16_replacing(e.what()));
            }
            store(receiver, member, result);
        }
    };
    using Props = framework::UserProfileProperties;
    text("ProfileUrl", &Props::WebUrl);
    text("AboutText", &Props::AboutText);
    text("FirstLifeAboutText", &Props::FirstLifeText);
    uuid("Image", &Props::ImageId);
    uuid("FirstLifeImage", &Props::FirstLifeImageId);
    uuid("Partner", &Props::PartnerId);
    if (!props) {
        null_reference();
    }
    props->WantToMask = 0;
    props->WantToText = u"";
    props->SkillsMask = 0;
    props->SkillsText = u"";
    props->Language = u"";
    auto number = [&](const char* key, auto member) {
        const auto& value = profile.get(key);
        if (!is_null(value)) {
            auto receiver = props;
            auto result = framework::invariant_convert_to_int32(object_text_(value));
            store(receiver, member, result);
        }
    };
    number("wantmask", &Props::WantToMask);
    text("wanttext", &Props::WantToText);
    number("skillsmask", &Props::SkillsMask);
    text("skillstext", &Props::SkillsText);
    text("languages", &Props::Language);
    return true;
}
}
