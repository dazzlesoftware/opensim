// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace network;
using Text = std::u16string;
std::string H(const Text& t) {
    if (t.empty())
        return ".";
    std::string o;
    for (char16_t c : t)
        for (unsigned b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            o += "0123456789abcdef"[b >> 4];
            o += "0123456789abcdef"[b & 15];
        }
    return o;
}
struct Host {
    int level, number, elapsed, kind, mutate, compact = 0;
    std::string fail;
    Text events;
    Text hit(Text stage) {
        if (!events.empty())
            events += u'\n';
        events += stage;
        if (framework::utf8(stage) == fail)
            throw NwcXmlRpcError("System.InvalidOperationException", stage);
        return stage;
    }
};
Text N(int n) {
    return framework::utf16(std::to_string(n));
}
struct Client : XmlRpcRequesterClient {
    Host& h;
    Client(Host& x) : h(x) {
    }
    void close() override {
        h.hit(u"close");
    }
    std::shared_ptr<NwcXmlRpcResponse> send(const std::shared_ptr<NwcXmlRpcRequest>&,
                                            const std::optional<Text>&) override {
        h.hit(u"send");
        if (h.kind == 4)
            return {};
        auto r = std::make_shared<NwcXmlRpcResponse>();
        if (h.kind == 2)
            r->set_fault(9, u"fault");
        else if (h.kind == 3) {
            r->is_fault = true;
            r->value = std::make_shared<Value>(Value::object({}));
        } else if (h.kind != 5)
            r->set_value(std::make_shared<Value>(h.kind == 1 ? Value("text") : Value::object({})));
        return r;
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Host h;
        std::istringstream in(line);
        in >> h.level >> h.number >> h.elapsed >> h.kind >> h.fail >> h.mutate;
        if (h.fail != "-") {
            Text f;
            for (std::size_t i = 0; i < h.fail.size(); i += 4)
                f += char16_t(std::stoi(h.fail.substr(i, 2), nullptr, 16) |
                              (std::stoi(h.fail.substr(i + 2, 2), nullptr, 16) << 8));
            h.fail = framework::utf8(f);
        }
        XmlRpcRequesterServices s;
        s.request_number = [&] {
            h.hit(u"number-get");
            return h.number;
        };
        s.set_request_number = [&](int n) {
            h.hit(u"number-set");
            h.number = n;
        };
        s.debug_level = [&] {
            h.hit(u"level");
            return h.level;
        };
        s.tick_count = [&] {
            h.hit(u"tick");
            return 17;
        };
        s.elapsed = [&](int n) {
            h.hit(u"elapsed:" + N(n));
            return h.elapsed;
        };
        s.client = [&](int n) {
            h.hit(u"client:" + N(n));
            return std::make_shared<Client>(h);
        };
        s.debug = [&](const Text& t) { h.hit(u"debug:" + t); };
        s.error = [&](const Text& t) { h.hit(u"error:" + t); };
        s.info = [&](const Text& t) { h.hit(u"info:" + t); };
        s.outgoing_detail = [&](int n, const Text& t) {
            h.hit(u"out:" + N(n) + u":" + t);
            if (h.mutate)
                h.level = h.mutate;
        };
        s.response_detail = [&](int n, const Text& t) { h.hit(u"response:" + N(n) + u":" + t); };
        s.compact_xml = [&](const Text& t) {
            h.hit(u"compact" + N(++h.compact));
            return u"compact:" + t;
        };
        s.request_text = [&](auto) { return h.hit(u"request-text"); };
        s.response_text = [&](auto r) {
            if (!r)
                throw NwcXmlRpcError("System.NullReferenceException",
                                     u"Object reference not set to an instance of an object.");
            return h.hit(u"response-text");
        };
        std::string result;
        try {
            auto r = XmlRpcRequester(s).send_request(std::make_shared<Value>(Value::object({})), u"method",
                                                     u"url");
            result = r ? "M" : "N";
        } catch (const NwcXmlRpcError& e) {
            result = e.source_type + ":" + H(e.message);
        } catch (const std::exception& e) {
            result = "UNEXPECTED:" + std::string(e.what());
        }
        std::cout << result << '|' << h.number << '|' << H(h.events) << '\n';
    }
}
