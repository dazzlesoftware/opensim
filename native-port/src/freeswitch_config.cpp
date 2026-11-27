// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/freeswitch_config.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_protocol.hpp"

#include <pugixml.hpp>
#include <sstream>
#include <stdexcept>

namespace opensim::server {
namespace {
void named_value(pugi::xml_node parent, const char *element, const char *name, const std::string &value) {
    auto child = parent.append_child(element);
    child.append_attribute("name") = name;
    child.append_attribute("value") = value.c_str();
}

void dialplan(pugi::xml_node section, const std::string &realm, const std::string &context) {
    auto context_node = section.append_child("context");
    context_node.append_attribute("name") = context.c_str();
    const auto condition = [&](const char *name, const char *expression) {
        auto extension = context_node.append_child("extension");
        extension.append_attribute("name") = name;
        auto rule = extension.append_child("condition");
        rule.append_attribute("field") = "destination_number";
        rule.append_attribute("expression") = expression;
        return rule;
    };
    for (const auto &[name, expression] :
         {std::pair{"opensim_conferences", "^confctl-(.*)$"}, std::pair{"opensim_conf", "^conf-(.*)$"}}) {
        auto rule = condition(name, expression);
        rule.append_child("action").append_attribute("application") = "answer";
        auto action = rule.append_child("action");
        action.append_attribute("application") = "conference";
        action.append_attribute("data") = ("$1-" + realm + '@' + context).c_str();
    }
    auto bridge = condition("avatar", "^(x.*)$").append_child("action");
    bridge.append_attribute("application") = "bridge";
    bridge.append_attribute("data") = "user/$1";
}

void directory(pugi::xml_node section, const std::string &realm, const std::string &context, const std::string &user,
               const std::string &invitee, bool invite, bool locate, bool sofia) {
    section.append_attribute("description") = "User Directory";
    auto domain = section.append_child("domain");
    domain.append_attribute("name") = realm.c_str();
    const auto dial_string = [](const std::string &contact, const std::string &destination) {
        const auto address = destination + "@${dialed_domain}";
        return "{sip_contact_user=" + contact + "}{presence_id=" + address + "}${sofia_contact(" + address + ")}";
    };
    if (locate || sofia) {
        named_value(domain.append_child("params"), "param", "dial-string",
                    dial_string("${dialed_user}", "${dialed_user}"));
    }
    if (locate) {
        auto entry = domain.append_child("user");
        entry.append_attribute("id") = user.c_str();
        auto variables = entry.append_child("variables");
        named_value(variables, "variable", "default_gateway", "$${default_provider}");
        named_value(variables, "variable", "presence_id", user + "@$${domain}");
        return;
    }
    if (sofia) {
        auto groups = domain.append_child("groups");
        groups.append_attribute("name") = "default";
        auto entry = groups.append_child("users").append_child("user");
        entry.append_attribute("id") = "$${default_provider}";
        auto gateway = entry.append_child("gateways").append_child("gateway");
        gateway.append_attribute("name") = "$${default_provider}";
        for (const auto &[name, value] :
             {std::pair{"username", "$${default_provider_username}"},
              std::pair{"password", "$${default_provider_password}"},
              std::pair{"from-user", "$${default_provider_username}"},
              std::pair{"from-domain", "$${default_provider_from_domain}"}, std::pair{"expire-seconds", "600"},
              std::pair{"register", "$${default_provider_register}"}, std::pair{"retry-seconds", "30"},
              std::pair{"extension", "$${default_provider_contact}"},
              std::pair{"contact-params", "domain_name=$${domain}"}}) {
            named_value(gateway, "param", name, value);
        }
        named_value(gateway, "param", "context", context);
        named_value(entry.append_child("params"), "param", "password", "$${default_provider_password}");
        named_value(domain.append_child("variables"), "variable", "default_gateway", "$${default_provider}");
        return;
    }
    const auto add_user = [&](const std::string &id) {
        auto entry = domain.append_child("user");
        entry.append_attribute("id") = id.c_str();
        auto parameters = entry.append_child("params");
        named_value(parameters, "param", "password", "1234");
        named_value(parameters, "param", "dial-string", dial_string(user, invite ? "$" + id : "${dialed_user}"));
        auto variables = entry.append_child("variables");
        named_value(variables, "variable", "user_context", context);
        named_value(variables, "variable", "presence_id", id + '@' + (invite ? "$${domain}" : realm));
    };
    add_user(user);
    if (invite)
        add_user(invitee);
}
} // namespace

std::map<std::string, std::string> parse_freeswitch_form(std::string_view body) {
    std::map<std::string, std::string> fields;
    while (!body.empty()) {
        const auto end = body.find('&');
        const auto field = body.substr(0, end);
        if (!framework::trim_utf8(field).empty()) {
            const auto separator = field.find('=');
            if (separator == std::string_view::npos)
                throw std::invalid_argument("Missing FreeSwitch form value");
            const auto value = field.substr(separator + 1);
            const auto [_, inserted] = fields.emplace(network::service_url_decode(field.substr(0, separator)),
                                                      network::service_url_decode(value.substr(0, value.find('='))));
            if (!inserted)
                throw std::invalid_argument("Duplicate FreeSwitch form field");
        }
        if (end == std::string_view::npos)
            break;
        body.remove_prefix(end + 1);
    }
    return fields;
}

network::HttpResponse freeswitch_config(std::string_view body, std::string_view realm, std::string_view context) {
    const auto form = parse_freeswitch_form(body);
    const auto field = [&](const char *name) -> std::string {
        const auto entry = form.find(name);
        return entry == form.end() ? std::string{} : entry->second;
    };
    const auto section_name = field("section");
    if (section_name != "directory" && section_name != "dialplan")
        return {500, "text/plain", {}};

    pugi::xml_document xml;
    auto document = xml.append_child("document");
    document.append_attribute("type") = "freeswitch/xml";
    auto section = document.append_child("section");
    section.append_attribute("name") = section_name.c_str();
    if (section_name == "dialplan") {
        if (!context.empty() && field("Hunt-Context") != context)
            return {200, "text/xml", {}};
        dialplan(section, std::string(realm), std::string(context));
    } else {
        if (field("domain") != realm)
            return {200, "text/xml", {}};
        const auto caller = field("Event-Calling-Function");
        const auto method = field("sip_auth_method");
        const bool sip_auth = caller.empty() || caller == "sofia_reg_parse_auth";
        const bool invite = sip_auth && method == "INVITE";
        const bool registration = caller == "user_outgoing_channel" || (sip_auth && method == "REGISTER");
        const bool locate = caller == "switch_xml_locate_user" || caller == "user_data_function";
        const bool sofia = caller == "config_sofia";
        if (!invite && !registration && !locate && !sofia)
            return {404, "text/xml", {}};
        directory(section, std::string(realm), std::string(context), field("user"), field("sip_request_user"), invite,
                  locate, sofia);
    }
    std::ostringstream output;
    xml.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return {200, "text/xml", output.str()};
}
} // namespace opensim::server
