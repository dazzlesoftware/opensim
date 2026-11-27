// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_accounts.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/text.hpp"

namespace opensim::services {
namespace {
using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;

std::optional<UserAccount> account(const network::ServiceXmlValue &value) {
    if (!value.list)
        return {};
    AccountValues fields;
    for (const auto &[name, field] : value.fields)
        fields[name] = field.list ? std::string("System.Collections.Generic.Dictionary`2[System.String,System.Object]")
                                  : field.text;
    return UserAccount(fields);
}

struct Remote {
    std::string uri;
    network::RestFormsRequest request;

    network::ServiceXmlValue send(std::string_view method, Fields fields) const {
        fields.insert(fields.begin(), {{"VERSIONMIN", "1"}, {"VERSIONMAX", "1"}, {"METHOD", std::string(method)}});
        try {
            const auto reply = request(uri + "/accounts", network::build_service_form(fields));
            return reply.empty() ? network::ServiceXmlValue{} : network::parse_service_xml(reply);
        } catch (...) {
            // The original connector returns a missing account on transport failure.
            return {};
        }
    }

    std::optional<UserAccount> single(std::string_view method, Fields fields) const {
        const auto reply = send(method, std::move(fields));
        for (const auto &[name, value] : reply.fields)
            if (name == "result")
                return account(value);
        return {};
    }
};
} // namespace

AccountBackend remote_account_backend(const server::IniConfig &config, network::RestFormsRequest request,
                                      std::shared_ptr<network::WebHttpClients> clients) {
    auto uri = config.get("UserAccountService", "UserAccountServerURI");
    if (framework::trim_utf8(uri).empty())
        throw std::invalid_argument("UserAccountServerURI is missing or invalid");
    const framework::HttpHost host{uri, true};
    if (!host.resolved())
        throw std::invalid_argument(host.valid() ? "Could not resolve UserAccountServerURI"
                                                 : "UserAccountServerURI is an invalid host");
    uri = host.uri;
    if (!request) {
        std::optional<std::pair<std::string, std::string>> auth;
        if (config.get("UserAccountService", "AuthType", config.get("Network", "AuthType")) ==
            "BasicHttpAuthentication")
            auth = {{config.get("UserAccountService", "HttpAuthUsername", config.get("Network", "HttpAuthUsername")),
                     config.get("UserAccountService", "HttpAuthPassword", config.get("Network", "HttpAuthPassword"))}};
        request =
            clients ? network::rest_forms_transport(std::move(clients), auth) : network::rest_forms_transport(auth);
    }
    auto remote = std::make_shared<Remote>(Remote{std::move(uri), std::move(request)});
    AccountBackend backend;
    backend.find = [remote](std::string_view scope, const AccountBackend::Fields &fields) {
        Fields query{{"ScopeID", std::string(scope)}};
        for (const auto &[name, value] : fields)
            query.emplace_back(name == "PrincipalID" ? "UserID" : name, value);
        return remote->single("getaccount", std::move(query));
    };
    backend.search = [remote](std::string_view scope, std::string_view text) {
        const auto reply = remote->send("getaccounts", {{"ScopeID", std::string(scope)}, {"query", std::string(text)}});
        std::vector<UserAccount> result;
        for (const auto &[name, value] : reply.fields)
            if (auto row = account(value))
                result.push_back(std::move(*row));
        return result;
    };
    backend.store = [remote](const UserAccount &value) {
        Fields fields;
        const auto values = value.to_key_value_pairs();
        for (const auto &[name, field] : *values)
            if (field.has_value())
                fields.emplace_back(name, framework::service_url_object_text(field));
        return remote->single("setaccount", std::move(fields)).has_value();
    };
    return backend;
}
} // namespace opensim::services
