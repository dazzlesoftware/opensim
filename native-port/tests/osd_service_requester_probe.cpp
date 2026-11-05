// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
using namespace opensim;
using namespace network;
using Text = std::u16string;
std::string H(const std::optional<Text>& t) {
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
std::string Hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
Text N(int n) {
    return framework::utf16(std::to_string(n));
}
struct Timer;
struct Host {
    int level, number, ticks, flags, mode;
    std::optional<std::int64_t> length;
    std::optional<int> code;
    std::string fail;
    Text events;
    std::optional<Text> wire;
    std::shared_ptr<Timer> current;
    void hit(const Text& text) {
        if (!events.empty())
            events += u'\n';
        events += text;
        auto tag = framework::utf8(text.substr(0, text.find(u':')));
        bool match = false;
        std::istringstream in(fail);
        std::string part;
        while (std::getline(in, part, '+'))
            if (part == tag)
                match = true;
        if (tag == "debug") {
            if (fail == "debug-final" && text.find(u" took ") != Text::npos)
                match = true;
            if (fail == "debug-catch" && text.starts_with(u"debug:[WEB UTIL]: Exception"))
                match = true;
            if (fail == "debug-failed" && text.starts_with(u"debug:[LOGHTTP]: request"))
                match = true;
        }
        if (match)
            throw OsdServiceError((flags & 16) ? "System.Net.Http.HttpRequestException"
                                               : "System.InvalidOperationException",
                                  framework::utf16(fail), (flags & 16) ? code : std::optional<int>{});
    }
    Text reply() {
        switch ((mode >> 8) % 7) {
        case 1:
            return u"true";
        case 2:
            return u"false";
        case 3:
            return u"not json";
        case 4:
            return u"null";
        case 5:
            return u"[]";
        case 6:
            return u"<llsd><map><key>success</key><boolean>true</boolean></map></llsd>";
        default:
            return u"{\"success\":true,\"reason\":\"ok \u00e9\U0001f600\"}";
        }
    }
};
struct Request : OsdServiceHttpRequest {
    Host& h;
    Request(Host& x) : h(x) {
    }
    void close() override {
        h.hit(u"request-close");
    }
};
struct Stream : OsdServiceStream {
    Host& h;
    Stream(Host& x) : h(x) {
    }
    void close() override {
        h.hit(u"stream-close");
    }
};
struct Response : OsdServiceResponse {
    Host& h;
    std::shared_ptr<Stream> stored;
    Response(Host& x) : h(x) {
    }
    void ensure_success() override {
        h.hit(u"ensure");
    }
    std::shared_ptr<OsdServiceStream> stream() override {
        h.hit(u"stream");
        return stored = (h.mode & 1) ? nullptr : std::make_shared<Stream>(h);
    }
    std::optional<std::int64_t> content_length() override {
        h.hit(u"length");
        return h.length;
    }
    void close() override {
        h.hit(u"response-close");
        if (stored)
            stored->close();
    }
};
struct Client : OsdServiceHttpClient {
    Host& h;
    Client(Host& x) : h(x) {
    }
    std::shared_ptr<OsdServiceResponse> send(const std::shared_ptr<OsdServiceHttpRequest>& r) override {
        h.hit(u"send");
        std::map<std::string, Text> headers;
        for (auto& [k, v] : r->headers) {
            auto key = k;
            std::transform(key.begin(), key.end(), key.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
            headers.emplace(key, v);
        }
        Text wire = r->method.value_or(u"") + u"\n" + r->url.value_or(u"") + u"\n" +
                    framework::utf16(r->body ? Hex(*r->body) : "-");
        for (auto& [k, v] : headers)
            wire += u"\n" + framework::utf16(k) + u":" + v;
        h.wire = wire;
        return std::make_shared<Response>(h);
    }
    void close() override {
        h.hit(u"client-close");
    }
};
struct Timer : OsdServiceCancellation {
    Host& h;
    std::function<void()> callback;
    Timer(Host& x) : h(x) {
    }
    void on_cancel(std::function<void()> f) override {
        h.hit(u"register");
        callback = std::move(f);
    }
    void close() override {
        h.hit(u"timer-close");
    }
};
struct Reader : OsdServiceReader {
    Host& h;
    std::shared_ptr<OsdServiceStream> stream;
    Reader(Host& x, std::shared_ptr<OsdServiceStream> s) : h(x), stream(std::move(s)) {
    }
    Text read_to_end() override {
        h.hit(u"read");
        if (h.mode & 2)
            h.current->callback();
        return h.reply();
    }
    void close() override {
        h.hit(u"reader-close");
        stream->close();
    }
};
Value Data() {
    auto d = Value::object({});
    Value name(framework::utf8(u"\u00e9\U0001f600\r\n<&>"));
    name.sdk_string = std::make_shared<const Text>(u"\u00e9\U0001f600\r\n<&>");
    d.set("name", name);
    d.set("count", Value(std::int32_t{47}));
    Value array;
    array.kind = Value::Kind::Array;
    array.array = {Value(true), Value("")};
    d.set("nested", array);
    return d;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        Host h;
        std::string length, code;
        in >> h.level >> h.number >> h.ticks >> h.flags >> h.mode >> length >> h.fail >> code;
        if (length != "-")
            h.length = std::stoll(length);
        if (code != "-")
            h.code = std::stoi(code);
        OsdServiceServices s;
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
            return h.ticks;
        };
        s.debug = [&](const auto& t) { h.hit(u"debug:" + t); };
        s.info = [&](const auto& t) { h.hit(u"info:" + t); };
        s.client = [&](int n) {
            h.hit(u"factory:" + N(n));
            return std::make_shared<Client>(h);
        };
        s.request = [&](const auto& method, const auto& url) {
            h.hit(u"request");
            auto r = std::make_shared<Request>(h);
            r->method = method;
            r->url = url;
            return r;
        };
        s.reader = [&](const auto& stream) {
            h.hit(u"reader");
            return std::make_shared<Reader>(h, stream);
        };
        s.cancellation = [&](int delay) -> std::shared_ptr<OsdServiceCancellation> {
            h.hit(u"timer:" + N(delay));
            if (delay < -1)
                throw OsdServiceError(
                    "System.ArgumentOutOfRangeException",
                    u"Specified argument was out of the range of valid values. (Parameter 'millisecondsDelay')");
            return h.current = std::make_shared<Timer>(h);
        };
        s.outgoing_detail = [&](const auto& method, int n, const auto& text) {
            h.hit(u"detail-out:" + method.value_or(u"") + u":" + N(n) + u":" + text);
            if (h.mode & 8) {
                h.level = 0;
                h.number = 700;
            }
        };
        s.response_detail = [&](int n, const auto& text) { h.hit(u"detail-response:" + N(n) + u":" + text); };
        s.serialize_text = [&](const auto& d) {
            h.hit(u"serialize-text");
            return (h.mode & 4) ? Text{} : framework::utf16_replacing(osd_json(d));
        };
        s.serialize_bytes = [&](const auto& d) {
            h.hit(u"serialize-bytes");
            return (h.mode & 4) ? std::string{} : osd_json(d);
        };
        s.utf8_bytes = [&](const auto& t) {
            h.hit(u"utf8");
            return framework::utf8(t);
        };
        s.canonicalize = [&](const auto& t) {
            h.hit(u"canonical");
            return canonical_osd_response_utf16(t);
        };
        auto data = Data();
        std::optional<Value> result;
        std::string status = "O";
        try {
            result = OsdServiceRequester(s).send(u"http://example.test/path", (h.flags & 1) ? nullptr : &data,
                                                 u"POST", 1234, (h.flags & 2) != 0, (h.flags & 4) != 0,
                                                 (h.flags & 8) != 0);
        } catch (const OsdServiceError& e) {
            status = "E:" + e.source_type + ":" + H(e.message);
        } catch (const std::exception& e) {
            status = "UNEXPECTED:" + std::string(e.what());
        }
        std::cout << status << '|' << h.number << '|' << h.level << '|' << H(h.events) << '|'
                  << (result ? Hex(osd_json(*result, true)) : "-") << '|' << H(h.wire) << '\n';
    }
}
