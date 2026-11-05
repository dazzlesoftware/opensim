// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/profile_handlers.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace framework;
using namespace network;
using namespace server;
std::string H(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
std::string U(std::string_view s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); i += 2)
        r += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return r;
}
std::string snap(const std::shared_ptr<Value>& v) {
    if (!v)
        return "N";
    try {
        return H(osd_json(*v, true));
    } catch (...) {
        return "SEREX";
    }
}
struct Provider : services::UserProfilesService {
    int mode;
    std::string trace;
    explicit Provider(int m) : mode(m) {
    }
    bool called(std::string name, std::string data, Text* result = nullptr) {
        trace += name + ":" + data + ";";
        if (result)
            *result = mode == 5 ? Text{} : Text(u"service result");
        if (mode == 4)
            throw std::runtime_error("provider failure");
        return mode != 1;
    }
    template <class T> bool obj(std::string name, std::shared_ptr<T>& p, Text* result) {
        bool ok = called(name, p ? H(osd_json(p->serialize_members(), true)) : "N", result);
        if (mode == 2)
            p = std::make_shared<T>();
        if (mode == 3)
            p.reset();
        if (p)
            p->deserialize_members(Value::object({{"Name", Value("service")},
                                                  {"Flags", Value(1)},
                                                  {"Notes", Value("service")},
                                                  {"EMail", Value("service")},
                                                  {"DataVal", Value("service")}}));
        return ok;
    }
    std::shared_ptr<Value> list(std::string name, Uuid id) {
        called(name, id.str());
        if (mode == 7)
            return {};
        if (mode == 6)
            return std::make_shared<Value>("wrong");
        auto a = std::make_shared<Value>();
        a->kind = Value::Kind::Array;
        a->array = {Value("entry"), Value(id.str())};
        return a;
    }
    std::shared_ptr<Value> AvatarClassifiedsRequest(Uuid id) override {
        return list("AvatarClassifiedsRequest", id);
    }
    bool ClassifiedUpdate(std::shared_ptr<UserClassifiedAdd> p, Text& result) override {
        return obj("ClassifiedUpdate", p, &result);
    }
    bool ClassifiedInfoRequest(std::shared_ptr<UserClassifiedAdd>& p, Text& result) override {
        return obj("ClassifiedInfoRequest", p, &result);
    }
    bool ClassifiedDelete(Uuid id) override {
        return called("ClassifiedDelete", id.str());
    }
    std::shared_ptr<Value> AvatarPicksRequest(Uuid id) override {
        return list("AvatarPicksRequest", id);
    }
    bool PickInfoRequest(std::shared_ptr<UserProfilePick>& p, Text& result) override {
        return obj("PickInfoRequest", p, &result);
    }
    bool PicksUpdate(std::shared_ptr<UserProfilePick>& p, Text& result) override {
        return obj("PicksUpdate", p, &result);
    }
    bool PicksDelete(Uuid id) override {
        return called("PicksDelete", id.str());
    }
    bool AvatarNotesRequest(std::shared_ptr<UserProfileNotes>& p) override {
        return obj("AvatarNotesRequest", p, nullptr);
    }
    bool NotesUpdate(std::shared_ptr<UserProfileNotes>& p, Text& result) override {
        return obj("NotesUpdate", p, &result);
    }
    bool AvatarPropertiesRequest(std::shared_ptr<UserProfileProperties>& p, Text& result) override {
        return obj("AvatarPropertiesRequest", p, &result);
    }
    bool AvatarPropertiesUpdate(std::shared_ptr<UserProfileProperties>& p, Text& result) override {
        return obj("AvatarPropertiesUpdate", p, &result);
    }
    bool AvatarInterestsUpdate(std::shared_ptr<UserProfileProperties> p, Text& result) override {
        return obj("AvatarInterestsUpdate", p, &result);
    }
    bool UserPreferencesUpdate(std::shared_ptr<UserPreferences>& p, Text& result) override {
        return obj("UserPreferencesUpdate", p, &result);
    }
    bool UserPreferencesRequest(std::shared_ptr<UserPreferences>& p, Text& result) override {
        return obj("UserPreferencesRequest", p, &result);
    }
    std::shared_ptr<Value> AvatarImageAssetsRequest(Uuid id) override {
        return list("AvatarImageAssetsRequest", id);
    }
    bool RequestUserAppData(std::shared_ptr<UserAppData>& p, Text& result) override {
        return obj("RequestUserAppData", p, &result);
    }
    bool SetUserAppData(std::shared_ptr<UserAppData> p, Text& result) override {
        return obj("SetUserAppData", p, &result);
    }
};
int main(int argc, char** argv) {
    if (argc > 1) {
        HttpServerOptions options;
        options.port = 0;
        HttpServer listener(options, [](const HttpRequest& request) {
            int mode = std::stoi(request.path.substr(1));
            auto provider = std::make_shared<Provider>(mode);
            JsonRpcDispatcher dispatcher;
            {
                JsonRpcProfileHandlers handlers(provider);
                handlers.register_handlers(dispatcher);
            }
            return dispatcher.handle(request);
        });
        listener.start([](int port) { std::cout << "LISTENING " << port << std::endl; });
        std::string stop;
        std::getline(std::cin, stop);
        listener.stop();
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int method, mode, seed;
        std::string raw;
        in >> method >> mode >> seed >> raw;
        if (method >= 18) {
            try {
                auto text = osd_string_utf16(parse_osd_json(U(raw)).get("text"));
                Uuid id;
                bool ok = true;
                if (method == 18)
                    id = construct_sdk_uuid(text);
                else
                    ok = try_parse_sdk_uuid(std::u16string_view(text), id);
                std::cout << (ok ? "1:" : "0:") << id.str() << "\n";
            } catch (const std::exception& e) {
                std::cout << "EX:" << H(e.what()) << "\n";
            }
            continue;
        }
        std::string log, status;
        auto provider = std::make_shared<Provider>(mode);
        JsonRpcDispatcher dispatcher;
        {
            JsonRpcProfileHandlers handlers(
                seed == 4 ? nullptr : provider, [&](ProfileLogLevel level, std::u16string_view s) {
                    log += (level == ProfileLogLevel::Debug ? "D:" : "I:") + utf8(s) + "\n";
                });
            handlers.register_handlers(dispatcher);
        }
        auto response = std::make_shared<JsonRpcResponse>();
        response->id(u"wireid");
        response->version(u"2.0");
        if (seed == 1) {
            response->error->code(17);
            response->error->message(u"existing");
            response->result = std::make_shared<Value>("existing");
        }
        if (seed == 2)
            response->error.reset();
        if (seed == 3)
            response.reset();
        try {
            auto json = parse_osd_json(U(raw));
            if (json.contains("__null")) {
                json.set("params", Value::null_reference());
            }
            auto names = dispatcher.keys();
            status = dispatcher.get(names.at(method))(json, response) ? "1" : "0";
        } catch (const std::exception& e) {
            status = "EX:" + H(e.what());
        }
        std::string error =
            response ? (response->error
                            ? std::to_string(response->error->code()) + ":" +
                                  (response->error->message() ? H(utf8(*response->error->message())) : "N")
                            : "N")
                     : "N";
        std::string wire = "SEREX";
        try {
            if (response)
                wire = H(response->serialize());
        } catch (...) {
        }
        std::cout << status << "|" << error << "|" << (response ? snap(response->result) : "N") << "|"
                  << H(provider->trace) << "|" << H(log) << "|" << wire << "\n";
    }
}
