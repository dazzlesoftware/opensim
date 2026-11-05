#include "opensim/network/web_http_clients.hpp"
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/services/open_profile_client.hpp"
#include "opensim/services/user_agent_connector.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/nini_xml_encoder.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using network::Value;
using framework::Uuid;
using framework::UserProfileProperties;
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
Value TextValue(const Text& p) {
    if (!p)
        return Value::null_reference();
    Value v(framework::utf8(*p));
    v.sdk_string = std::make_shared<const std::u16string>(*p);
    return v;
}
std::string Dump(const Value* input) {
    if (!input || input->kind == Value::Kind::NullReference)
        return "N";
    const auto& v = input->view();
    using K = Value::Kind;
    switch (v.kind) {
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
Uuid Id(int n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return Uuid(s.str());
}
std::shared_ptr<UserProfileProperties> Seed(const Text& text) {
    auto p = std::make_shared<UserProfileProperties>();
    p->UserId = Id(1);
    p->PartnerId = Id(2);
    p->ImageId = Id(3);
    p->FirstLifeImageId = Id(4);
    p->WebUrl = u"seed url";
    p->AboutText = text;
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
std::string Props(const std::shared_ptr<UserProfileProperties>& p) {
    if (!p)
        return "N";
    return p->UserId.str() + ":" + p->PartnerId.str() + ":" + p->ImageId.str() + ":" +
           p->FirstLifeImageId.str() + ":" + H(p->WebUrl) + ":" + H(p->AboutText) + ":" +
           H(p->FirstLifeText) + ":" + std::to_string(p->WantToMask) + ":" + H(p->WantToText) + ":" +
           std::to_string(p->SkillsMask) + ":" + H(p->SkillsText) + ":" + H(p->Language) + ":" +
           (p->PublishProfile ? "True" : "False") + ":" + (p->PublishMature ? "True" : "False");
}
std::string Error() {
    try {
        throw;
    } catch (const services::OpenProfileError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const network::NwcXmlRpcError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::ConfigXmlHttpError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::ConfigHttpHeaderError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::ConfigUriComponentsError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::NiniXmlProviderError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::InvariantNumberError& e) {
        return std::string(e.kind == framework::InvariantNumberError::Kind::Overflow
                               ? "System.OverflowException"
                               : "System.FormatException") +
               ":" + H(e.message);
    } catch (const framework::XmlEncoderError& e) {
        return std::string(e.source_type) + ":" + H(e.message);
    } catch (const std::exception& e) {
        return "System.Exception:" + H(framework::utf16_replacing(e.what()));
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
int main(int argc, char**) {
    framework::ConfigXmlNativeHttpServices services;
    services.proxy = [](auto, auto) -> std::optional<framework::ConfigXmlHttpRoute> { return {}; };
    services.connect_error = framework::config_http_connect_error;
    services.dns_error = framework::config_http_dns_error;
    services.read_error = framework::config_http_read_error;
    services.write_error = framework::config_http_write_error;
    services.error = [](std::string phase, int code) {
        if (phase == "timeout" || phase == "headers")
            return std::make_exception_ptr(
                network::NwcXmlRpcError("System.Net.Http.HttpRequestException", u"request timeout"));
        return std::make_exception_ptr(
            std::runtime_error("UNMAPPED HTTP " + phase + ":" + std::to_string(code)));
    };
    auto make_transport = [&] {
        if (argc == 1)
            return network::NwcXmlRpcHttpTransport(services);
        auto clients =
            std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory(services));
        clients->setup(false, false, nullptr, 32);
        return network::NwcXmlRpcHttpTransport(std::move(clients));
    };
    auto transport = make_transport();
    int request_number = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, timeout;
        std::string m, p, u;
        in >> mode >> timeout >> m >> p >> u;
        auto method = D(m), payload = D(p), url = D(u);
        auto props = mode == 1 || mode == 3 ? Seed(payload) : nullptr;
        std::vector<std::u16string> logs;
        framework::Vector3 position{9, 8, 7}, look{6, 5, 4};
        Text bool_reason = u"seed";
        std::string result;
        try {
            if (mode == 4 || mode == 5 || mode == 6 || mode == 7) {
                services::UserAgentConnectorServices binding;
                binding.simulation_options.automatic_purge = false;
                binding.client = [&](int n) {
                    return network::xml_rpc_http_client(
                        std::make_shared<network::NwcXmlRpcHttpTransport>(transport), n);
                };
                binding.debug = binding.error =
                    binding.warn = [&](const auto& text) { logs.push_back(text); };
                services::UserAgentServiceConnector agent(url, std::move(binding));
                if (mode == 6)
                    result = "G:" + Region(agent.get_home_region(Id(1), position, look));
                else if (mode == 7) {
                    auto list = agent.get_online_friends(Id(1), std::make_shared<std::vector<Text>>());
                    result = "F:";
                    for (auto& id : list) {
                        if (result != "F:")
                            result += ',';
                        result += id.str();
                    }
                } else if (mode == 4) {
                    auto info = agent.get_user_info(Id(1));
                    result = "U:" + Dump(&info);
                } else {
                    auto r = std::make_shared<network::NwcXmlRpcRequest>();
                    r->method_name = u"verify_agent";
                    r->parameters->push_back(std::make_shared<Value>(Value::object({})));
                    result = agent.get_bool_response(r, bool_reason) ? "B:True" : "B:False";
                }
            } else if (mode == 1 || mode == 3) {
                services::OpenProfileClient client(
                    url,
                    [&](const Value& data, std::u16string_view rpc_method,
                        const Text& address) -> std::shared_ptr<Value> {
                        network::XmlRpcRequesterServices binding;
                        binding.request_number = [&] { return request_number; };
                        binding.set_request_number = [&](int n) { request_number = n; };
                        binding.debug_level = [&] { return mode == 3 ? 5 : 0; };
                        binding.client = [&](int n) {
                            return network::xml_rpc_http_client(
                                std::make_shared<network::NwcXmlRpcHttpTransport>(transport), n);
                        };
                        binding.debug = binding.error =
                            binding.info = [&](const std::u16string& message) { logs.push_back(message); };
                        return network::XmlRpcRequester(std::move(binding))
                            .send_request(std::make_shared<Value>(data), std::u16string(rpc_method), address);
                    });
                result = client.request_avatar_properties(props) ? "P:1" : "P:0";
            } else {
                auto req = std::make_shared<network::NwcXmlRpcRequest>();
                req->method_name = method;
                req->parameters->push_back(std::make_shared<Value>(TextValue(payload)));
                auto r = transport.send(req, url, timeout);
                result = "R:" + std::to_string(r->is_fault ? 1 : 0) + ":" + Dump(r->value.get()) +
                         ";C:" + std::to_string(r->fault_code()) + ";S:" + H(r->fault_string());
            }
        } catch (...) {
            result = "E:" + Error();
        }
        std::u16string log_text;
        for (const auto& log : logs) {
            if (log.starts_with(u"[LOGHTTP]: HTTP OUT ") && log.find(u" took ") != std::u16string::npos)
                continue;
            if (!log_text.empty())
                log_text += u'\n';
            log_text += log;
        }
        std::cout << result << '|' << Props(props) << '|'
                  << (mode == 3 || mode == 4 || mode == 5 || mode == 6 || mode == 7 ? H(log_text) : "-")
                  << '|' << (mode == 5 ? H(bool_reason) : "-") << '|' << (mode == 6 ? Vec(position) : "-")
                  << '|' << (mode == 6 ? Vec(look) : "-") << '\n';
    }
}
