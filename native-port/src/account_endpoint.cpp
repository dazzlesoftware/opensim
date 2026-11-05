// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/account_endpoint.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::server {
using namespace network;
namespace {
constexpr const char* failure =
    "<?xml version =\"1.0\"?><ServerResponse><result>Failure</result></ServerResponse>";
std::string text(const FormValue& value) {
    if (auto p = std::get_if<std::string>(&value))
        return *p;
    return "System.Collections.Generic.List`1[System.String]";
}
bool scope(const Form& fields, framework::Uuid& id) {
    auto i = fields.find("ScopeID");
    return i == fields.end() || framework::try_parse_sdk_uuid(text(i->second), id);
}
ServiceXmlValue account_value(const services::UserAccount& account) {
    auto pairs = account.to_key_value_pairs();
    auto result = ServiceXmlValue::object({});
    for (const auto& [key, value] : *pairs)
        if (value.has_value() && value.type() != typeid(std::nullptr_t))
            result.fields.push_back({key, framework::service_url_object_text(value)});
    return result;
}
std::string single(const AccountOperations::Account& account) {
    return service_xml(
        ServiceXmlValue::object({{"result", account ? account_value(*account) : ServiceXmlValue("null")}}));
}
std::string many(const AccountOperations::Accounts& accounts, bool skip_null) {
    auto result = ServiceXmlValue::object({});
    if (accounts.empty())
        result.fields.push_back({"result", "null"});
    std::size_t index = 0;
    for (const auto& account : accounts) {
        if (!account) {
            if (skip_null)
                continue;
            throw std::invalid_argument("Null search account");
        }
        result.fields.push_back({"account" + std::to_string(index++), account_value(*account)});
    }
    return service_xml(result);
}
}
HttpResponse AccountEndpoint::handle(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/xml";
    response.headers["Cache-Control"] = "no-store";
    if (request.method != "POST") {
        response.status = 405;
        response.headers["Allow"] = "POST";
        return response;
    }
    if (!auth_.authenticate(request, response))
        return response;
    response.body = failure;
    try {
        const auto fields = parse_service_form(framework::trim_utf8(request.body));
        auto method = fields.find("METHOD");
        if (method == fields.end())
            return response;
        const auto name = text(method->second);
        framework::Uuid scope_id;
        if (name == "getaccount") {
            if (!scope(fields, scope_id)) {
                response.body = single({});
                return response;
            }
            AccountOperations::Account account;
            auto id = fields.find("UserID");
            if (id == fields.end())
                id = fields.find("PrincipalID");
            if (id != fields.end()) {
                framework::Uuid principal;
                if (framework::try_parse_sdk_uuid(text(id->second), principal))
                    account = operations_.by_id(scope_id, principal);
            } else if (fields.contains("Email"))
                account = operations_.by_email(scope_id, text(fields.at("Email")));
            else if (fields.contains("FirstName") && fields.contains("LastName"))
                account =
                    operations_.by_name(scope_id, text(fields.at("FirstName")), text(fields.at("LastName")));
            response.body = single(account);
        } else if (name == "getaccounts") {
            if (!fields.contains("query") || !scope(fields, scope_id))
                return response;
            auto query = framework::trim_utf8(text(fields.at("query")));
            AccountOperations::Accounts accounts;
            if (!query.empty())
                accounts = operations_.search(scope_id, query);
            response.body = many(accounts, false);
        } else if (name == "getmultiaccounts") {
            if (!scope(fields, scope_id) || !fields.contains("IDS"))
                return response;
            auto ids = std::get_if<std::vector<std::string>>(&fields.at("IDS"));
            if (!ids)
                return response;
            std::vector<std::string> valid;
            for (const auto& s : *ids) {
                framework::Uuid id;
                if (framework::try_parse_sdk_uuid(s, id))
                    valid.push_back(s);
            }
            AccountOperations::Accounts accounts;
            if (!valid.empty())
                accounts = operations_.multi(scope_id, std::move(valid));
            response.body = many(accounts, true);
        } else if (name == "setaccount" && allow_set_) {
            framework::Uuid principal;
            auto i = fields.find("PrincipalID");
            if (i == fields.end() || !framework::try_parse_sdk_uuid(text(i->second), principal) ||
                principal.is_zero() || !scope(fields, scope_id))
                return response;
            auto account = operations_.by_id(scope_id, principal);
            if (!account)
                return response;
            auto string = [&](const char* key, std::optional<std::string>& out) {
                auto value = fields.find(key);
                if (value != fields.end())
                    out = text(value->second);
            };
            auto integer = [&](const char* key, int& out) {
                auto value = fields.find(key);
                std::int32_t parsed = 0;
                if (value != fields.end() && services::parse_account_integer(text(value->second), parsed))
                    out = parsed;
            };
            string("FirstName", account->first_name);
            string("LastName", account->last_name);
            string("Email", account->email);
            integer("Created", account->created);
            integer("UserLevel", account->user_level);
            integer("UserFlags", account->user_flags);
            string("UserTitle", account->user_title);
            if (operations_.store(*account))
                response.body = single(account);
        } else if (name == "createuser" && allow_create_ && operations_.create) {
            if (!fields.contains("FirstName") || !fields.contains("LastName") ||
                !fields.contains("Password") || !scope(fields, scope_id))
                return response;
            auto principal = framework::Uuid::random();
            auto id = fields.find("PrincipalID");
            if (id != fields.end() && !framework::try_parse_sdk_uuid(text(id->second), principal))
                return response;
            auto optional = [&](const char* key) {
                auto i = fields.find(key);
                return i == fields.end() ? std::string{} : text(i->second);
            };
            auto account = operations_.create(scope_id, principal, text(fields.at("FirstName")),
                                              text(fields.at("LastName")), text(fields.at("Password")),
                                              optional("Email"), optional("Model"));
            if (account)
                response.body = single(account);
        }
    } catch (...) {
        response.body = failure;
    }
    return response;
}
}
