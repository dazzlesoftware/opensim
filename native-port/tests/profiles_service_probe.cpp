// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/profiles_service.hpp"
#include "opensim/services/profiles_configuration.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace framework;
using namespace network;
using namespace services;
std::string H(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
std::string snap(const std::shared_ptr<Value>& v) {
    return v ? H(osd_json(*v, true)) : "N";
}
struct Store : ProfilesData {
    int mode;
    std::string trace;
    explicit Store(int m) : mode(m) {
    }
    bool called(std::string name, std::string value) {
        trace += name + ":" + value + ";";
        if (mode == 4 || (mode == 9 && name == "UpdateUserPreferences"))
            throw std::runtime_error("provider failure");
        return mode != 1 && !(mode == 8 && name == "UpdateUserPreferences");
    }
    template <class T> bool obj(std::string name, std::shared_ptr<T>& value, Text* result = nullptr) {
        bool ok = called(name, value ? H(osd_json(value->serialize_members(), true)) : "N");
        if (result)
            *result = mode == 5 ? Text{} : Text(u"provider result");
        if (mode == 2)
            value = std::make_shared<T>();
        if (mode == 3)
            value.reset();
        if (value) {
            auto fields = Value::object(
                {{"Name", Value("stored")}, {"Notes", Value("stored")}, {"DataVal", Value("stored")}});
            if (mode == 7)
                fields.set("EMail", Value{});
            value->deserialize_members(fields);
        }
        return ok;
    }
    std::shared_ptr<Value> list(std::string name, Uuid id) {
        called(name, id.str());
        if (mode == 6)
            return {};
        auto v = std::make_shared<Value>();
        v->kind = Value::Kind::Array;
        v->array = {Value(id.str())};
        return v;
    }
    std::shared_ptr<network::Value> GetClassifiedRecords(Uuid id) override {
        return list("GetClassifiedRecords", id);
    }
    bool UpdateClassifiedRecord(std::shared_ptr<framework::UserClassifiedAdd> value, Text& result) override {
        return obj("UpdateClassifiedRecord", value, &result);
    }
    bool GetClassifiedInfo(std::shared_ptr<framework::UserClassifiedAdd>& value, Text& result) override {
        return obj("GetClassifiedInfo", value, &result);
    }
    bool DeleteClassifiedRecord(Uuid id) override {
        return called("DeleteClassifiedRecord", id.str());
    }
    std::shared_ptr<network::Value> GetAvatarPicks(Uuid id) override {
        return list("GetAvatarPicks", id);
    }
    std::shared_ptr<UserProfilePick> GetPickInfo(Uuid avatar, Uuid pick) override {
        called("GetPickInfo", avatar.str() + ":" + pick.str());
        if (mode == 3)
            return {};
        auto value = std::make_shared<UserProfilePick>();
        value->Name = u"stored";
        return value;
    }
    bool UpdatePicksRecord(std::shared_ptr<framework::UserProfilePick> value) override {
        return obj("UpdatePicksRecord", value);
    }
    bool DeletePicksRecord(Uuid id) override {
        return called("DeletePicksRecord", id.str());
    }
    bool GetAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value) override {
        return obj("GetAvatarNotes", value);
    }
    bool UpdateAvatarNotes(std::shared_ptr<framework::UserProfileNotes>& value, Text& result) override {
        return obj("UpdateAvatarNotes", value, &result);
    }
    bool GetAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value,
                             Text& result) override {
        return obj("GetAvatarProperties", value, &result);
    }
    bool UpdateAvatarProperties(std::shared_ptr<framework::UserProfileProperties>& value,
                                Text& result) override {
        return obj("UpdateAvatarProperties", value, &result);
    }
    bool UpdateAvatarInterests(std::shared_ptr<framework::UserProfileProperties> value,
                               Text& result) override {
        return obj("UpdateAvatarInterests", value, &result);
    }
    bool UpdateUserPreferences(std::shared_ptr<framework::UserPreferences>& value, Text& result) override {
        return obj("UpdateUserPreferences", value, &result);
    }
    bool GetUserPreferences(std::shared_ptr<framework::UserPreferences>& value, Text& result) override {
        return obj("GetUserPreferences", value, &result);
    }
    std::shared_ptr<network::Value> GetUserImageAssets(Uuid id) override {
        return list("GetUserImageAssets", id);
    }
    bool GetUserAppData(std::shared_ptr<framework::UserAppData>& value, Text& result) override {
        return obj("GetUserAppData", value, &result);
    }
    bool SetUserAppData(std::shared_ptr<framework::UserAppData> value, Text& result) override {
        return obj("SetUserAppData", value, &result);
    }
};
struct Accounts : LocalProfileAccounts {
    int mode;
    std::string& trace;
    Accounts(int m, std::string& t) : mode(m), trace(t) {
    }
    std::shared_ptr<ProfileAccount> get(Uuid scope, Uuid user) override {
        trace += scope.str() + ":" + user.str() + ";";
        if (mode == 2)
            return {};
        if (mode == 3)
            throw std::runtime_error("account failure");
        auto account = std::make_shared<ProfileAccount>();
        account->email = mode == 4   ? std::optional<std::u16string>(u"")
                         : mode == 5 ? std::nullopt
                                     : std::optional<std::u16string>(u"account@example.test");
        return account;
    }
};
struct Remote : ProfileAccounts {
    std::shared_ptr<ProfileAccount> get(Uuid, Uuid) override {
        throw std::runtime_error("must not call remote");
    }
};
#ifndef PROFILES_PROVIDER_ONLY
int main(int argc, char**) {
    if (argc > 1) {
        HttpServerOptions options;
        options.port = 0;
        HttpServer listener(options, [](const HttpRequest& request) {
            std::istringstream path(request.path.substr(1));
            int mode = 0, account = 0;
            path >> mode;
            char slash;
            path >> slash >> account;
            auto store = std::make_shared<Store>(mode);
            std::string trace;
            std::shared_ptr<ProfileAccounts> accounts;
            if (account == 1)
                accounts = std::make_shared<Remote>();
            else if (account)
                accounts = std::make_shared<Accounts>(account, trace);
            auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
            auto config = arena->create_ini_source();
            auto section = config->add_config(nini_writer_text(u"Profiles"));
            for (auto pair :
                 {std::pair(u"Enabled", u"true"), std::pair(u"StorageProvider", u"store"),
                  std::pair(u"UserAccountService", u"account"), std::pair(u"LocalServiceModule", u"profile")})
                section->set_text(nini_writer_text(pair.first), nini_writer_text(pair.second));
            ProfilesPluginFactories factories;
            factories.storage = [&](auto, auto) { return store; };
            factories.accounts = [&](auto, auto) { return accounts; };
            auto service = configured_profiles_service(config, u"Profiles", factories);
            auto dispatcher = std::make_shared<server::JsonRpcDispatcher>();
            {
                ProfileServiceConnector connector(config, dispatcher, u"Profiles",
                                                  [&](auto, auto, auto) { return service; });
            }
            return dispatcher->handle(request);
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
        int method, mode, email, account, flags, seed;
        in >> method >> mode >> email >> account >> flags >> seed;
        auto store = std::make_shared<Store>(mode);
        std::string log, atrace, status, snapshot = "-";
        std::shared_ptr<ProfileAccounts> accounts;
        if (account == 1)
            accounts = std::make_shared<Remote>();
        else if (account)
            accounts = std::make_shared<Accounts>(account, atrace);
        ProfilesService service(flags & 2 ? nullptr : store, accounts,
                                [&](auto text) { log += utf8(text) + "\n"; });
        ProfilesService::Text result = seed ? ProfilesService::Text{} : ProfilesService::Text(u"initial");
        std::shared_ptr<server::JsonRpcMembers> original;
        std::function<void()> call;
        switch (method) {
        case 0:
            call = [&] {
                snapshot =
                    snap(service.AvatarClassifiedsRequest(Uuid("00000000-0000-0000-0000-000000000001")));
                status = "O";
            };
            break;
        case 1: {
            auto value =
                flags & 1 ? std::shared_ptr<UserClassifiedAdd>{} : std::make_shared<UserClassifiedAdd>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.ClassifiedUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 2: {
            auto value =
                flags & 1 ? std::shared_ptr<UserClassifiedAdd>{} : std::make_shared<UserClassifiedAdd>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.ClassifiedInfoRequest(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 3:
            call = [&] {
                status = service.ClassifiedDelete(Uuid("00000000-0000-0000-0000-000000000001")) ? "1" : "0";
            };
            break;
        case 4:
            call = [&] {
                snapshot = snap(service.AvatarPicksRequest(Uuid("00000000-0000-0000-0000-000000000001")));
                status = "O";
            };
            break;
        case 5: {
            auto value = flags & 1 ? std::shared_ptr<UserProfilePick>{} : std::make_shared<UserProfilePick>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.PickInfoRequest(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 6: {
            auto value = flags & 1 ? std::shared_ptr<UserProfilePick>{} : std::make_shared<UserProfilePick>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.PicksUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 7:
            call = [&] {
                status = service.PicksDelete(Uuid("00000000-0000-0000-0000-000000000001")) ? "1" : "0";
            };
            break;
        case 8: {
            auto value =
                flags & 1 ? std::shared_ptr<UserProfileNotes>{} : std::make_shared<UserProfileNotes>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.AvatarNotesRequest(value) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 9: {
            auto value =
                flags & 1 ? std::shared_ptr<UserProfileNotes>{} : std::make_shared<UserProfileNotes>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.NotesUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 10: {
            auto value = flags & 1 ? std::shared_ptr<UserProfileProperties>{}
                                   : std::make_shared<UserProfileProperties>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.AvatarPropertiesRequest(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 11: {
            auto value = flags & 1 ? std::shared_ptr<UserProfileProperties>{}
                                   : std::make_shared<UserProfileProperties>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.AvatarPropertiesUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 12: {
            auto value = flags & 1 ? std::shared_ptr<UserProfileProperties>{}
                                   : std::make_shared<UserProfileProperties>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.AvatarInterestsUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 13: {
            auto value = flags & 1 ? std::shared_ptr<UserPreferences>{} : std::make_shared<UserPreferences>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.UserPreferencesUpdate(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 14: {
            auto value = flags & 1 ? std::shared_ptr<UserPreferences>{} : std::make_shared<UserPreferences>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.UserPreferencesRequest(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 15:
            call = [&] {
                snapshot =
                    snap(service.AvatarImageAssetsRequest(Uuid("00000000-0000-0000-0000-000000000001")));
                status = "O";
            };
            break;
        case 16: {
            auto value = flags & 1 ? std::shared_ptr<UserAppData>{} : std::make_shared<UserAppData>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.RequestUserAppData(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        case 17: {
            auto value = flags & 1 ? std::shared_ptr<UserAppData>{} : std::make_shared<UserAppData>();
            if (value) {
                auto fields = Value::object({{"UserId", Value("00000000-0000-0000-0000-000000000001")},
                                             {"CreatorId", Value("00000000-0000-0000-0000-000000000002")},
                                             {"PickId", Value("00000000-0000-0000-0000-000000000003")}});
                fields.set("EMail", email == 0 ? Value{}
                                               : Value(email == 1   ? ""
                                                       : email == 2 ? " "
                                                                    : "incoming@example.test"));
                value->deserialize_members(fields);
            }
            call = [&, value]() mutable {
                try {
                    status = service.SetUserAppData(value, result) ? "1" : "0";
                } catch (const std::exception& e) {
                    status = "EX:" + H(e.what());
                }
                snapshot = value ? H(osd_json(value->serialize_members(), true)) : "N";
            };
            break;
        }
        }
        try {
            call();
        } catch (const std::exception& e) {
            status = "EX:" + H(e.what());
        }
        std::cout << status << "|" << snapshot << "|" << (result ? H(utf8(*result)) : "N") << "|"
                  << H(store->trace) << "|" << H(atrace) << "|" << H(log) << "\n";
    }
}

#endif
