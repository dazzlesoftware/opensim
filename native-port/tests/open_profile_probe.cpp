// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/open_profile_client.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using framework::UserProfileProperties;
using framework::Uuid;
using network::Value;
using Text = std::optional<std::u16string>;
std::string H(const Text& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string o;
    for (char16_t c : *t) {
        for (unsigned byte : {unsigned(c) & 255, unsigned(c) >> 8}) {
            o += "0123456789abcdef"[byte >> 4];
            o += "0123456789abcdef"[byte & 15];
        }
    }
    return o;
}
Text D(const std::string& h) {
    if (h == "-")
        return {};
    if (h == ".")
        return u"";
    std::u16string o;
    for (std::size_t i = 0; i < h.size(); i += 4)
        o += char16_t(std::stoi(h.substr(i, 2), nullptr, 16) |
                      (std::stoi(h.substr(i + 2, 2), nullptr, 16) << 8));
    return o;
}
Uuid Id(int n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return Uuid(s.str());
}
std::shared_ptr<UserProfileProperties> Seed(int n) {
    auto p = std::make_shared<UserProfileProperties>();
    p->UserId = Id(n);
    p->PartnerId = Id(2);
    p->ImageId = Id(3);
    p->FirstLifeImageId = Id(4);
    p->WebUrl = u"seed url";
    p->AboutText = u"seed about";
    p->FirstLifeText = u"seed life";
    p->WantToMask = 47;
    p->SkillsMask = -31;
    p->WantToText = u"seed wants";
    p->SkillsText = u"seed skills";
    p->Language = u"seed language";
    p->PublishProfile = true;
    p->PublishMature = true;
    return p;
}
std::string Dump(const std::shared_ptr<UserProfileProperties>& p) {
    if (!p)
        return "N";
    return p->UserId.str() + ":" + p->PartnerId.str() + ":" + p->ImageId.str() + ":" +
           p->FirstLifeImageId.str() + ":" + H(p->WebUrl) + ":" + H(p->AboutText) + ":" +
           H(p->FirstLifeText) + ":" + std::to_string(p->WantToMask) + ":" + H(p->WantToText) + ":" +
           std::to_string(p->SkillsMask) + ":" + H(p->SkillsText) + ":" + H(p->Language) + ":" +
           (p->PublishProfile ? "True" : "False") + ":" + (p->PublishMature ? "True" : "False");
}
Value Atom(int kind, const Text& payload) {
    Value v;
    auto s = framework::utf8(payload.value_or(u""));
    switch (kind) {
    case 0:
        return Value::null_reference();
    case 1:
        v = Value(s);
        v.sdk_string = payload ? std::make_shared<const std::u16string>(*payload) : nullptr;
        if (!payload)
            v = Value::null_reference();
        return v;
    case 2:
        return Value(std::int32_t(std::stol(s)));
    case 3:
        return Value(s == "True");
    case 4:
        v.kind = Value::Kind::Real;
        v.real = std::stod(s);
        return v;
    case 5:
        return Value::long_integer(std::stoll(s));
    case 6:
        v.kind = Value::Kind::Array;
        return v;
    case 7:
        return Value::object({});
    case 8:
        v.kind = Value::Kind::Binary;
        return v;
    case 9:
        v.kind = Value::Kind::Date;
        v.sdk_date = std::make_shared<framework::AssetDateTime>(std::stoll(s), 0);
        return v;
    case 10:
        v.kind = Value::Kind::Uri;
        v.sdk_string = payload ? std::make_shared<const std::u16string>(*payload) : nullptr;
        return v;
    default:
        throw std::runtime_error("kind");
    }
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, flags, field, kind;
        std::string h, u;
        in >> mode >> flags >> field >> kind >> h >> u;
        auto payload = D(h), uri = D(u);
        std::string trace;
        auto props = flags & 1 ? nullptr : Seed(1), initial = props;
        auto atom = Atom(kind, payload);
        const char* keys[] = {"ProfileUrl",     "AboutText",  "FirstLifeAboutText", "Image",
                              "FirstLifeImage", "Partner",    "wantmask",           "wanttext",
                              "skillsmask",     "skillstext", "languages"};
        auto row = Value::object({{"ProfileUrl", "url"},
                                  {"AboutText", "about"},
                                  {"FirstLifeAboutText", "life"},
                                  {"Image", Id(6).str()},
                                  {"FirstLifeImage", Id(7).str()},
                                  {"Partner", Id(8).str()},
                                  {"wantmask", "-2147483648"},
                                  {"wanttext", "wants"},
                                  {"skillsmask", "2147483647"},
                                  {"skillstext", "skills"},
                                  {"languages", "en"}});
        if (field >= 0)
            row.set(keys[field], atom);
        if (field == -2)
            row = Value::object({});
        std::shared_ptr<Value> response;
        Value data;
        data.kind = Value::Kind::Array;
        if (mode == 3) {
        } else if (mode == 4)
            data.array.push_back(Value::null_reference());
        else if (mode == 6)
            data.array.push_back(atom);
        else
            data.array.push_back(row);
        if (mode == 2)
            data = Value::null_reference();
        if (mode == 5)
            data = atom;
        if (mode != 0)
            response =
                std::make_shared<Value>(mode == 1 ? Value::object({}) : Value::object({{"data", data}}));
        services::OpenProfileClient::Requester request = [&](const Value& r, std::u16string_view method,
                                                             const Text& server) {
            trace += "rpc:" + r.get("avatar_id").text + ":" + H(Text(method)) + ":" + H(server) + ";";
            if (flags & 2)
                throw std::runtime_error("request");
            if (flags & 4)
                props = Seed(5);
            if (flags & 8)
                props.reset();
            return response;
        };
        services::OpenProfileClient::ObjectText text;
        if (kind == 10)
            text = [&](const Value& v) -> Text {
                if (v.kind == Value::Kind::Uri) {
                    trace += "text;";
                    if (flags & 32)
                        props = Seed(5);
                    if (flags & 64)
                        props.reset();
                    if (flags & 16)
                        throw std::runtime_error("stringify");
                    return payload;
                }
                return framework::utf16_replacing(v.as_string());
            };
        std::string result;
        try {
            bool ok = services::OpenProfileClient(uri, request, text).request_avatar_properties(props);
            result = ok ? "B:1" : "B:0";
        } catch (const framework::InvariantNumberError& e) {
            result = "E:" + H(e.message);
        } catch (const std::exception& e) {
            result = "E:" + H(framework::utf16_replacing(e.what()));
        }
        std::cout << result << '|' << trace << '|' << (initial == props ? "same" : "new") << '|'
                  << Dump(props) << '|' << Dump(initial) << '\n';
    }
}
