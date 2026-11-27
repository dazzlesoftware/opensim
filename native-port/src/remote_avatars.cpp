// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_avatars.hpp"
#include "opensim/framework/culture_prefix.hpp"

namespace opensim::services {
namespace {
using framework::Uuid;
using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;

class RemoteAvatars final : public AvatarService {
  public:
    RemoteAvatars(AvatarStore &store, std::string uri, network::RestFormsRequest request)
        : AvatarService(store), uri_(std::move(uri)), request_(std::move(request)) {}

    AvatarData get_avatar(Uuid principal) override {
        const auto reply = send("getavatar", principal, {});
        for (const auto &[name, value] : reply.fields) {
            if (name != "result" || !value.list)
                continue;
            AccountValues fields;
            for (const auto &[key, entry] : value.fields)
                fields[key] =
                    entry.list ? "System.Collections.Generic.Dictionary`2[System.String,System.Object]" : entry.text;
            return AvatarData(fields);
        }
        // A missing remote avatar remains missing. Never consult the local store.
        return {};
    }

    bool set_avatar(Uuid principal, AvatarData &avatar) override {
        Fields fields;
        const auto values = avatar.to_key_value_pairs();
        for (const auto &[name, value] : *values) {
            if (!value.has_value())
                throw std::invalid_argument("Null avatar field");
            fields.emplace_back(name, framework::service_url_object_text(value));
        }
        return accepted(send("setavatar", principal, std::move(fields)));
    }

    std::shared_ptr<framework::AvatarAppearance> get_appearance(Uuid principal) override {
        return get_avatar(principal).to_appearance();
    }

    bool set_appearance(Uuid principal, const framework::AvatarAppearance *appearance) override {
        AvatarData avatar(appearance);
        return set_avatar(principal, avatar);
    }

    bool reset(Uuid principal) override {
        return accepted(send("resetavatar", principal, {}));
    }

    bool set_items(Uuid principal, const Strings *names, const Strings *values) override {
        if (!names || !values)
            throw std::invalid_argument("Null avatar item array");
        Fields fields;
        for (const auto &name : *names)
            fields.emplace_back("Names[]", name.value_or(""));
        for (const auto &value : *values)
            fields.emplace_back("Values[]", value.value_or(""));
        return accepted(send("setitems", principal, std::move(fields)));
    }

    bool remove_items(Uuid principal, const Strings *names) override {
        if (!names)
            throw std::invalid_argument("Null avatar item names");
        Fields fields;
        for (const auto &name : *names)
            fields.emplace_back("Names[]", name.value_or(""));
        return accepted(send("removeitems", principal, std::move(fields)));
    }

  private:
    network::ServiceXmlValue send(std::string_view method, Uuid principal, Fields fields) const {
        fields.insert(
            fields.begin(),
            {{"VERSIONMIN", "1"}, {"VERSIONMAX", "1"}, {"METHOD", std::string(method)}, {"UserID", principal.str()}});
        try {
            const auto reply = request_(uri_ + "/avatar", network::build_service_form(fields));
            return reply.empty() ? network::ServiceXmlValue{} : network::parse_service_xml(reply);
        } catch (...) {
            return {};
        }
    }

    static bool accepted(const network::ServiceXmlValue &reply) {
        for (const auto &[name, value] : reply.fields)
            if (name == "result")
                return !value.list && framework::ordinal_equals_ignore_case(value.text, "success");
        return false;
    }

    std::string uri_;
    network::RestFormsRequest request_;
};
} // namespace

std::unique_ptr<AvatarService> remote_avatar_service(AvatarStore &store, const server::IniConfig &config,
                                                     network::RestFormsRequest request,
                                                     std::shared_ptr<network::WebHttpClients> clients) {
    auto uri = config.get("AvatarService", "AvatarServerURI");
    if (uri.empty())
        throw std::invalid_argument("AvatarServerURI is missing");
    if (!request) {
        std::optional<std::pair<std::string, std::string>> auth;
        if (config.get("AvatarService", "AuthType", config.get("Network", "AuthType")) == "BasicHttpAuthentication")
            auth = {{config.get("AvatarService", "HttpAuthUsername", config.get("Network", "HttpAuthUsername")),
                     config.get("AvatarService", "HttpAuthPassword", config.get("Network", "HttpAuthPassword"))}};
        request =
            clients ? network::rest_forms_transport(std::move(clients), auth) : network::rest_forms_transport(auth);
    }
    return std::make_unique<RemoteAvatars>(store, std::move(uri), std::move(request));
}
} // namespace opensim::services
