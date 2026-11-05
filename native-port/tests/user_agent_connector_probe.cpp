// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/user_agent_connector.hpp"
#include "opensim/services/open_profile_client.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/network/osd_value.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <bit>
#include <iomanip>
using namespace opensim;
using network::Value;
using Text = std::optional<std::u16string>;
std::string H(const Text& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string o;
    for (char16_t c : *t)
        for (unsigned b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            o += "0123456789abcdef"[b >> 4];
            o += "0123456789abcdef"[b & 15];
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
std::string Dump(const Value* input) {
    if (!input || (!input->reference &&
                   (input->kind == Value::Kind::NullReference || input->kind == Value::Kind::Undefined)))
        return "N";
    auto& v = input->view();
    using K = Value::Kind;
    switch (v.kind) {
    case K::NullReference:
    case K::Undefined:
        return "N";
    case K::String:
        return "S:" + H(v.sdk_string ? Text(*v.sdk_string) : Text(framework::utf16_replacing(v.text)));
    case K::Integer:
        return "I:" + std::to_string(v.integer);
    case K::Boolean:
        return v.boolean ? "T" : "F";
    case K::Real: {
        std::ostringstream s;
        s << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(v.real);
        return "R:" + s.str();
    }
    case K::Date:
        return "D:" + std::to_string(v.sdk_date->ticks());
    case K::Binary: {
        std::string out = "B:";
        for (auto c : *network::osd_binary(v)) {
            out += "0123456789abcdef"[c >> 4];
            out += "0123456789abcdef"[c & 15];
        }
        return out;
    }
    case K::Array: {
        std::string out = "A[";
        for (auto& x : v.array) {
            if (out.back() != '[')
                out += ',';
            out += Dump(&x);
        }
        return out + "]";
    }
    case K::Map: {
        auto keys = v.ordered_keys();
        std::sort(keys.begin(), keys.end(),
                  [](auto& a, auto& b) { return framework::utf16(a) < framework::utf16(b); });
        std::string out = "M{";
        for (auto& k : keys) {
            if (out.back() != '{')
                out += ',';
            out += H(framework::utf16(k)) + "=" + Dump(&v.map.at(k));
        }
        return out + "}";
    }
    default:
        return "X";
    }
}
struct Host {
    int mode = 0;
    Text xml;
    std::string fail;
    std::vector<std::u16string> events;
    std::shared_ptr<network::NwcXmlRpcResponse> response;
    void hit(const std::u16string& t) {
        events.push_back(t);
        if (framework::utf8(t.substr(0, t.find(u':'))) == fail)
            throw network::NwcXmlRpcError("System.InvalidOperationException", framework::utf16(fail));
    }
};
struct Client : network::XmlRpcRequesterClient {
    Host& h;
    Client(Host& x) : h(x) {
    }
    void close() override {
        h.hit(u"close");
    }
    std::shared_ptr<network::NwcXmlRpcResponse> send(const std::shared_ptr<network::NwcXmlRpcRequest>& r,
                                                     const Text& url) override {
        h.hit(framework::utf16("send:" + H(r->method_name) + ":" + H(url) + ":" +
                               Dump(r->parameters->at(0).get())));
        if (h.xml == Text(u"NULL"))
            return {};
        network::NwcXmlRpcResponseDeserializer parser;
        auto reply = parser.deserialize(h.xml);
        if (h.mode >= 13 && h.mode <= 15 && !reply->is_fault && reply->value) {
            auto p = reply->value;
            while (p->reference)
                p = p->reference;
            if (p->kind == Value::Kind::Map) {
                p->map_order = p->ordered_keys();
                std::sort(p->map_order.begin(), p->map_order.end(),
                          [](auto& a, auto& b) { return framework::utf16(a) < framework::utf16(b); });
            }
        }
        return h.response = reply;
    }
};
std::string Error() {
    try {
        throw;
    } catch (const network::NwcXmlRpcError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::NiniXmlProviderError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::ConfigUriComponentsError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::InvariantNumberError& e) {
        return std::string(e.kind == framework::InvariantNumberError::Kind::Overflow
                               ? "System.OverflowException"
                               : "System.FormatException") +
               ":" + H(e.message);
    } catch (const std::exception& e) {
        return "UNEXPECTED:" + std::string(e.what());
    }
}
std::string Vec(framework::Vector3 v) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(v.x) << ':'
      << std::setw(8) << std::bit_cast<std::uint32_t>(v.y) << ':' << std::setw(8)
      << std::bit_cast<std::uint32_t>(v.z);
    return s.str();
}
std::string Region(const std::shared_ptr<services::GridRegion>& r) {
    if (!r)
        return "N";
    auto text = [](const std::optional<std::string>& t) -> Text {
        return t ? Text(framework::utf16_replacing(*t)) : Text{};
    };
    return r->id.str() + ":" + std::to_string(r->x) + ":" + std::to_string(r->y) + ":" +
           std::to_string(r->size_x) + ":" + std::to_string(r->size_y) + ":" + H(text(r->name)) + ":" +
           H(text(r->external_host)) + ":" + std::to_string(r->http_port) + ":" + H(text(r->raw_server_uri)) +
           ":" + H(framework::utf16_replacing(r->server_uri())) + ":" +
           (r->internal_endpoint
                ? r->internal_endpoint->address + ":" + std::to_string(r->internal_endpoint->port)
                : "N");
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, config;
        std::string url, xml, first, last;
        Host h;
        in >> mode >> config >> h.fail >> url >> xml >> first >> last;
        h.mode = mode;
        h.xml = D(xml);
        services::UserAgentConnectorServices s;
        s.simulation_options.automatic_purge = false;
        s.client = [&](int n) {
            h.hit(framework::utf16("factory:" + std::to_string(n)));
            return std::make_shared<Client>(h);
        };
        s.debug = [&](const auto& t) { h.hit(u"debug:" + t); };
        s.error = [&](const auto& t) { h.hit(u"error:" + t); };
        s.warn = [&](const auto& t) { h.hit(u"warn:" + t); };
        if (mode >= 13 && mode <= 15)
            s.object_text = [&](const Value& v) -> Text {
                if (v.view().kind == Value::Kind::String) {
                    auto t = services::profile_boxed_object_text(v);
                    if (t == u"__throw__" || t == u"__null__") {
                        h.hit(u"text");
                        if (t == u"__throw__")
                            throw network::NwcXmlRpcError("System.InvalidOperationException", u"text");
                        return {};
                    }
                }
                return services::profile_boxed_object_text(v);
            };
        std::unique_ptr<services::UserAgentServiceConnector> c;
        framework::Vector3 position{9, 8, 7}, look{6, 5, 4};
        Text reason = u"seed";
        std::string result;
        try {
            c = std::make_unique<services::UserAgentServiceConnector>(D(url), s, config != 0);
            framework::Uuid id("00000000-0000-0000-0000-000000000001");
            if (mode == 0) {
                auto info = c->get_user_info(id);
                bool same = true;
                for (const auto& [k, v] : info.map)
                    if (&v.view() != &h.response->value->view().map.at(k).view())
                        same = false;
                result = Dump(&info) + (same ? ":True" : ":False");
            } else if (mode == 1) {
                auto v = c->get_server_urls(id);
                result = Dump(&v);
            } else if (mode == 2)
                result = "S:" + H(c->locate_user(id));
            else if (mode == 3)
                result = "S:" + H(c->get_uui(id, framework::Uuid("00000000-0000-0000-0000-000000000002")));
            else if (mode == 4)
                result = "U:" + c->get_uuid(D(first), D(last)).str();
            else if (mode == 6) {
                auto r = std::make_shared<network::NwcXmlRpcRequest>();
                r->method_name = u"bool";
                r->parameters->push_back(std::make_shared<Value>(Value::object({})));
                result = c->get_bool_response(r, reason) ? "B:True" : "B:False";
            } else if (mode == 7)
                result = c->verify_agent(id, D(first)) ? "B:True" : "B:False";
            else if (mode == 8)
                result = c->verify_client(id, D(first)) ? "B:True" : "B:False";
            else if (mode == 9)
                result = c->is_agent_coming_home(id, D(first)) ? "B:True" : "B:False";
            else if (mode == 10) {
                c->logout_agent(id, framework::Uuid("00000000-0000-0000-0000-000000000002"));
                result = "V";
            } else if (mode == 11) {
                c->set_client_token(id, D(first));
                result = "V";
            } else if (mode == 12)
                result = "G:" + Region(c->get_home_region(id, position, look));
            else if (mode >= 13 && mode <= 15) {
                services::UserAgentServiceConnector::Friends friends;
                auto f = D(first);
                if (f) {
                    friends = std::make_shared<std::vector<Text>>();
                    if (*f != u"empty")
                        *friends = {f, D(last)};
                }
                auto list = mode == 13 ? c->get_online_friends(id, friends)
                                       : c->status_notification(friends, id, mode == 14);
                std::vector<std::string> names;
                for (auto& id : list)
                    names.push_back(id.str());
                result = "F:";
                for (auto& name : names) {
                    if (result != "F:")
                        result += ',';
                    result += name;
                }
            } else
                result = "C";
        } catch (...) {
            result = "E:" + Error();
        }
        std::u16string events;
        for (auto& t : h.events) {
            if (!events.empty())
                events += u'\n';
            events += t;
        }
        std::cout << result << '|' << H(c ? c->server_url() : Text{}) << '|' << H(events) << '|' << H(reason)
                  << '|' << Vec(position) << '|' << Vec(look) << '\n';
    }
}
