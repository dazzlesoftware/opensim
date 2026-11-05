// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_service_http.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace network;
using Text = std::optional<std::u16string>;
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
std::string H(const Text& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string out;
    for (auto c : *t)
        for (unsigned b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
int main(int argc, char**) {
    framework::ConfigXmlNativeHttpServices http;
    http.proxy = [](auto, auto) -> std::optional<framework::ConfigXmlHttpRoute> { return {}; };
    http.connect_error = framework::config_http_connect_error;
    http.dns_error = framework::config_http_dns_error;
    http.read_error = framework::config_http_read_error;
    http.write_error = framework::config_http_write_error;
    http.error = [](std::string phase, int n) {
        return std::make_exception_ptr(
            std::runtime_error("UNMAPPED HTTP " + phase + ":" + std::to_string(n)));
    };
    std::unique_ptr<OsdServiceNativeHttp> transport;
    if (argc > 1) {
        auto globals = std::make_shared<WebHttpClients>(native_web_http_handler_factory(http));
        globals->setup(false, false, nullptr, 200);
        transport = std::make_unique<OsdServiceNativeHttp>(globals, http.error);
    } else
        transport = std::make_unique<OsdServiceNativeHttp>(http);
    int number = 0, level = 0;
    std::vector<std::u16string> logs;
    OsdServiceServices services;
    services.request_number = [&] { return number; };
    services.set_request_number = [&](int n) { number = n; };
    services.debug_level = [&] { return level; };
    services.debug = services.info = [&](auto& t) { logs.push_back(t); };
    services.tick_count = [] { return 0; };
    services.elapsed = [](int) { return 0; };
    transport->bind(services);
    OsdServiceRequester requester(services);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int flags, timeout;
        std::string method, url, data;
        in >> flags >> level >> timeout >> method >> url >> data;
        logs.clear();
        std::string status;
        try {
            auto text = D(data);
            std::optional<Value> value;
            if (text)
                value = parse_osd_json_utf16(*text);
            auto result = requester.send(D(url), value ? &*value : nullptr, D(method), timeout,
                                         (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0);
            status = "R:" + H(framework::utf16_replacing(osd_json(result, true)));
        } catch (const OsdServiceError& e) {
            status = "E:" + e.source_type + ":" + H(e.message);
        } catch (const std::exception& e) {
            status = "UNEXPECTED:" + std::string(e.what());
        }
        std::u16string trace;
        for (auto& log : logs) {
            if (log.starts_with(u"[LOGHTTP]: HTTP OUT ") && log.find(u" took ") != std::u16string::npos)
                continue;
            if (!trace.empty())
                trace += u'\n';
            trace += log;
        }
        std::cout << status << '|' << H(trace) << '\n';
    }
}
