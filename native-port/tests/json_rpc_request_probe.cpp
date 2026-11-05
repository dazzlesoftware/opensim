#include "opensim/server/json_rpc_request.hpp"
#include "opensim/framework/user_profile_models.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
using namespace opensim;
using namespace server;
using namespace network;
using namespace framework;
std::string hex(std::string_view s) {
    std::string r;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        r += h[c >> 4];
        r += h[c & 15];
    }
    return r;
}
Uuid uid(int i) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::setw(12) << std::setfill('0') << std::hex << i;
    return Uuid(s.str());
}
Value uuid_value(int i) {
    Value v;
    v.kind = Value::Kind::Uuid;
    v.text = uid(i).str();
    return v;
}
std::shared_ptr<BoundJsonRpcMembers> model(int type, bool fill) {
    switch (type) {
    case 0: {
        auto m = std::make_shared<UserClassifiedAdd>();
        if (fill) {
            m->ClassifiedId = uid(1);
            m->CreatorId = uid(2);
            m->CreationDate = -5;
            m->ExpirationDate = -4;
            m->Category = -3;
            m->Name = std::optional<std::u16string>(u"initial <&> Name \u00e9");
            m->Description = std::nullopt;
            m->ParcelId = uid(8);
            m->ParentEstate = 1;
            m->SnapshotId = uid(10);
            m->SimName = std::optional<std::u16string>(u"initial <&> SimName \u00e9");
            m->GlobalPos = std::optional<std::u16string>(u"initial <&> GlobalPos \u00e9");
            m->ParcelName = std::nullopt;
            m->Flags = static_cast<std::uint8_t>(242);
            m->Price = 7;
        }
        return m;
    }
    case 1: {
        auto m = std::make_shared<UserProfileProperties>();
        if (fill) {
            m->UserId = uid(1);
            m->PartnerId = uid(2);
            m->PublishProfile = true;
            m->PublishMature = false;
            m->WebUrl = std::optional<std::u16string>(u"initial <&> WebUrl \u00e9");
            m->WantToMask = -2;
            m->WantToText = std::nullopt;
            m->SkillsMask = 0;
            m->SkillsText = std::optional<std::u16string>(u"initial <&> SkillsText \u00e9");
            m->Language = std::nullopt;
            m->ImageId = uid(11);
            m->AboutText = std::optional<std::u16string>(u"initial <&> AboutText \u00e9");
            m->FirstLifeImageId = uid(13);
            m->FirstLifeText = std::optional<std::u16string>(u"initial <&> FirstLifeText \u00e9");
        }
        return m;
    }
    case 2: {
        auto m = std::make_shared<UserProfilePick>();
        if (fill) {
            m->PickId = uid(1);
            m->CreatorId = uid(2);
            m->TopPick = true;
            m->Name = std::nullopt;
            m->OriginalName = std::optional<std::u16string>(u"initial <&> OriginalName \u00e9");
            m->Desc = std::optional<std::u16string>(u"initial <&> Desc \u00e9");
            m->ParcelId = uid(7);
            m->SnapshotId = uid(8);
            m->ParcelName = std::optional<std::u16string>(u"initial <&> ParcelName \u00e9");
            m->SimName = std::nullopt;
            m->GlobalPos = std::optional<std::u16string>(u"initial <&> GlobalPos \u00e9");
            m->Gatekeeper = std::optional<std::u16string>(u"initial <&> Gatekeeper \u00e9");
            m->SortOrder = 5;
            m->Enabled = false;
        }
        return m;
    }
    case 3: {
        auto m = std::make_shared<UserProfileNotes>();
        if (fill) {
            m->UserId = uid(1);
            m->TargetId = uid(2);
            m->Notes = std::optional<std::u16string>(u"initial <&> Notes \u00e9");
        }
        return m;
    }
    case 4: {
        auto m = std::make_shared<UserPreferences>();
        if (fill) {
            m->UserId = uid(1);
            m->IMViaEmail = false;
            m->Visible = true;
            m->EMail = std::nullopt;
        }
        return m;
    }
    case 5: {
        auto m = std::make_shared<UserAccountProperties>();
        if (fill) {
            m->EmailAddress = std::nullopt;
            m->Firstname = std::optional<std::u16string>(u"initial <&> Firstname \u00e9");
            m->LastName = std::optional<std::u16string>(u"initial <&> LastName \u00e9");
            m->Password = std::nullopt;
            m->UserId = std::optional<std::u16string>(u"initial <&> UserId \u00e9");
        }
        return m;
    }
    case 6: {
        auto m = std::make_shared<UserAccountAuth>();
        if (fill) {
            m->UserId = std::nullopt;
            m->Password = std::optional<std::u16string>(u"initial <&> Password \u00e9");
        }
        return m;
    }
    case 7: {
        auto m = std::make_shared<UserAppData>();
        if (fill) {
            m->TagId = std::nullopt;
            m->DataKey = std::optional<std::u16string>(u"initial <&> DataKey \u00e9");
            m->UserId = std::optional<std::u16string>(u"initial <&> UserId \u00e9");
            m->DataVal = std::nullopt;
        }
        return m;
    }
    default:
        return {};
    }
}
std::vector<JsonRpcFieldSchema> schema() {
    std::vector<JsonRpcFieldSchema> result;
    for (int i = 0; i < 8; ++i)
        for (auto& field : model(i, false)->schema())
            if (std::none_of(result.begin(), result.end(),
                             [&](const auto& x) { return x.name == field.name; }))
                result.push_back(field);
    return result;
}
Value member_result(int mode) {
    Value map;
    map.kind = Value::Kind::Map;
    int index = 0;
    for (auto& field : schema()) {
        Value value;
        switch (field.kind) {
        case JsonRpcFieldKind::Uuid:
            value = uuid_value(1000 + index);
            break;
        case JsonRpcFieldKind::Integer:
            value = Value("17.9");
            break;
        case JsonRpcFieldKind::Byte:
            value = Value(-1);
            break;
        case JsonRpcFieldKind::Boolean:
            value = Value("0");
            break;
        default:
            value = Value("remote <&> " + utf8(field.name) + " \u00e9");
            break;
        }
        if (mode == 13)
            value = {};
        if (mode == 14 && field.name == u"Name")
            value = Value::null_reference();
        map.set(sdk_map_key_bytes(field.name), value);
        ++index;
    }
    return map;
}
std::shared_ptr<Value> initial(int kind) {
    switch (kind) {
    case 0:
        return {};
    case 1:
        return std::make_shared<Value>();
    case 2:
        return std::make_shared<Value>(Value::object({{"x", Value(1)}, {"zero", Value(0)}}));
    case 3: {
        auto v = std::make_shared<Value>();
        v->kind = Value::Kind::Array;
        v->array = {Value(1), Value{}};
        return v;
    }
    case 4:
        return std::make_shared<Value>("parameter <&> \u00e9");
    case 5:
        return std::make_shared<Value>(0);
    case 6:
        return std::make_shared<Value>(osd_from_binary(
            std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{0, 1, 2, 3})));
    default:
        return std::make_shared<Value>(uuid_value(0));
    }
}
std::shared_ptr<Value> boxed_ptr(const Value& value) {
    if (value.kind == Value::Kind::NullReference)
        return {};
    if (value.reference)
        return value.reference;
    return std::make_shared<Value>(value.view());
}
Value boxed_value(const std::shared_ptr<Value>& value) {
    return value ? Value::alias(value) : Value::null_reference();
}
std::shared_ptr<Value> reply(int mode, const Value& request) {
    if (mode == 9)
        throw std::runtime_error("post failed");
    if (mode == 10)
        return {};
    auto outer = std::make_shared<Value>(Value::object({}));
    if (mode == 1)
        return outer;
    if (mode == 2) {
        outer->set("_Result", Value("bad"));
        return outer;
    }
    if (mode == 11) {
        outer->set("_Result", Value::null_reference());
        return outer;
    }
    if (mode == 17) {
        outer->set("_Result", boxed_value(boxed_ptr(request.get("params"))));
        return outer;
    }
    auto response = std::make_shared<Value>(Value::object({}));
    if (mode != 3) {
        response->set("jsonrpc", Value("2.0"));
        response->set("id", Value("reply"));
        if (mode == 4)
            response->set("error", Value::object({{"code", Value(-3)}, {"message", Value("denied")}}));
        else if (mode == 5)
            response->set("error", Value{});
        else if (mode == 6)
            response->set("error", Value::null_reference());
        else if (mode == 7)
            response->set("result", Value("bad"));
        else if (mode == 8)
            response->set("result", Value::null_reference());
        else if (mode == 12)
            response->set("result", Value::object({}));
        else
            response->set("result", member_result(mode));
    }
    if (mode == 18) {
        auto decoded = deserialize_sdk_osd_text(utf16_replacing(osd_json(*response)));
        outer->set("_Result", std::move(decoded));
    } else
        outer->set("_Result", Value::alias(response));
    if (mode == 16)
        outer->set("Success", Value(false));
    return outer;
}
std::string snapshot(const std::shared_ptr<Value>& value) {
    if (!value)
        return "N";
    try {
        return hex(osd_json(*value, true));
    } catch (...) {
        return "SEREX";
    }
}
std::string snapshot(const std::shared_ptr<JsonRpcMembers>& object) {
    if (!object)
        return "N";
    try {
        return hex(osd_json(object->serialize_members(), true));
    } catch (...) {
        return "SEREX";
    }
}
int main(int argc, char** argv) {
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream input(row);
        std::string kind;
        int type, fill, mode, args;
        input >> kind >> type >> fill >> mode >> args;
        JsonRpcRequestManager::Text method = u"profile.query", uri = u"http://service.test/rpc",
                                    id = u"id <&>";
        if (args == 1)
            id = u"";
        if (args == 2)
            id.reset();
        if (args == 3)
            uri = u"";
        if (args == 4)
            uri = u" \u2003\t";
        if (args == 5)
            uri.reset();
        if (args == 6)
            method = u"";
        if (args == 7)
            method.reset();
        if (args == 8) {
            id.reset();
            uri.reset();
            method.reset();
        }
        if (args == 9)
            id = u" ";
        std::string captured = "-", wire = "-", log, postinfo = "-", status;
        int calls = 0;
        auto data = initial(type);
        auto original_data = data;
        std::shared_ptr<JsonRpcMembers> object = model(type, fill);
        auto original_object = object;
        JsonRpcRequestManager::Post post;
        if (argc <= 1)
            post = [&](const auto& url, const Value& incoming, int timeout, bool rpc) {
                ++calls;
                postinfo =
                    std::to_string(timeout) + ":" + (rpc ? "1" : "0") + ":" + (url ? hex(utf8(*url)) : "N");
                Value copy = incoming;
                if (kind == "O" && (!id || id->empty())) {
                    auto generated = osd_string(incoming.get("id"));
                    if (generated.size() != 36 || generated[14] != '4' ||
                        std::string("89ab").find(generated[19]) == std::string::npos)
                        throw std::runtime_error("bad generated id");
                    copy.set("id", Value("<generated>"));
                }
                captured = hex(osd_json(copy, true));
                wire = hex(osd_json(copy));
                return reply(mode, incoming);
            };
        else
            uri = utf16_replacing(argv[1]);
        JsonRpcRequestManager manager(post, {}, [&](const JsonRpcRequestLog& event) {
            log += utf8(event.message) + (event.exception ? " EX:" + utf8(*event.exception) : "") + "\n";
        });
        try {
            status = (kind == "O" ? manager.request(data, method, uri, id)
                                  : manager.request_object(object, method, uri, id))
                         ? "1"
                         : "0";
        } catch (const JsonRpcRequestArgumentError& e) {
            status = "ARG:" + e.parameter;
        } catch (const std::exception& e) {
            status = "EX:" + hex(e.what());
        }
        auto state = kind == "O" ? snapshot(data) : snapshot(object);
        if (argc > 1) {
            std::cout << status << "|" << state << "\n";
            continue;
        }
        bool same = kind == "O" ? data == original_data : object == original_object;
        std::cout << status << "|" << state << "|" << (same ? "S" : "D") << "|" << calls << ":" << postinfo
                  << "|" << captured << "|" << wire << "|" << hex(log) << "\n";
    }
}
