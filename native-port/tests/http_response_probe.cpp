#include "opensim/server/http_response.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <iostream>
using namespace opensim;
using namespace server;
using network::Value;
std::string hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string unhex(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += char(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
HttpName name(const Value& v) {
    if (v.kind == Value::Kind::Undefined)
        return {};
    return unhex(network::osd_string(v.get("h")));
}
int32_t number(const Value& v) {
    return network::osd_integer(v);
}
int64_t wide(const Value& v) {
    return std::stoll(network::osd_string(v));
}
std::string named(const HttpName& v) {
    return v ? hex(*v) : "N";
}
std::string bytes(const HttpBytes& v) {
    return v ? hex(std::string(v->begin(), v->end())) : "N";
}
struct Control {
    Value input;
    std::string trace;
    std::weak_ptr<HttpResponseState> response;
    int failed = 0;
    bool mutated = false;
    void event(std::string op, std::string detail = "") {
        trace += op + (detail.empty() ? "" : ":" + detail) + ";";
        if (network::osd_string(input.get("failure")) == op &&
            ++failed == (input.contains("nth") ? number(input.get("nth")) : 1))
            throw std::runtime_error("injected");
        if (!mutated && network::osd_string(input.get("at")) == op) {
            mutated = true;
            if (auto r = response.lock()) {
                auto action = network::osd_string(input.get("mutation"));
                if (action == "clear")
                    r->clear();
                if (action == "continue")
                    r->check_send_next_async_continue();
            }
        }
    }
};
struct Clock : HttpClock {
    Control& c;
    explicit Clock(Control& x) : c(x) {
    }
    HttpDateTime local_now() const override {
        c.event("local");
        return {c.input.contains("local") ? wide(c.input.get("local")) : 638397614450000000, 0};
    }
    HttpDateTime utc_now() const override {
        c.event("utc");
        return {c.input.contains("utc") ? wide(c.input.get("utc")) : 638397614450000000, 0};
    }
};
struct Context : HttpResponseContext {
    Control& c;
    int32_t maximum = 20, timeout = 30000;
    explicit Context(Control& x) : c(x) {
        if (c.input.contains("max"))
            maximum = number(c.input.get("max"));
        if (c.input.contains("timeout"))
            timeout = number(c.input.get("timeout"));
    }
    bool is_closing() const override {
        c.event("closing");
        return network::osd_boolean(c.input.get("closing"));
    }
    int32_t max_requests() const override {
        c.event("max");
        return maximum;
    }
    int32_t timeout_keep_alive() const override {
        c.event("get_timeout");
        return timeout;
    }
    void timeout_keep_alive(int32_t v) override {
        c.event("set_timeout", std::to_string(v));
        timeout = std::min(v, 120000);
    }
    void start_send_response(std::shared_ptr<HttpResponseState>) override {
        c.event("start");
    }
    bool send_async_start(HttpBytes data, int32_t start, int32_t length) override {
        std::string contents = "BAD";
        if (data && start >= 0 && length >= 0 && size_t(start) <= data->size() &&
            size_t(length) <= data->size() - size_t(start))
            contents = hex(std::string(data->begin() + start, data->begin() + start + length));
        c.event("write", std::to_string(start) + ":" + std::to_string(length) + ":" + contents);
        return !c.input.contains("send_ok") || network::osd_boolean(c.input.get("send_ok"));
    }
    void end_send_response(uint32_t id, HttpConnectionType connection) override {
        c.event("end", std::to_string(id) + ":" + std::to_string(int32_t(connection)));
    }
    void continue_send_response() override {
        c.event("continue");
    }
};
struct Request : HttpResponseRequest {
    Control& c;
    std::shared_ptr<Context> ctx;
    Request(Control& x, std::shared_ptr<Context> context) : c(x), ctx(std::move(context)) {
    }
    HttpName http_version() const override {
        c.event("version");
        return c.input.contains("version") ? name(c.input.get("version")) : HttpName("HTTP/1.1");
    }
    std::shared_ptr<HttpResponseContext> context() const override {
        c.event("context");
        return network::osd_boolean(c.input.get("null_context")) ? nullptr : ctx;
    }
    HttpConnectionType connection() const override {
        c.event("connection");
        return static_cast<HttpConnectionType>(
            c.input.contains("connection") ? number(c.input.get("connection")) : 1);
    }
    uint32_t id() const override {
        c.event("id");
        return 123;
    }
    double arrival_timestamp() const override {
        c.event("arrival");
        return 1.25;
    }
};
struct Cookie : ResponseCookie {
    Control& c;
    std::string behavior;
    std::weak_ptr<ResponseCookies> collection;
    Cookie(Control& x, std::string how, std::shared_ptr<ResponseCookies> values)
        : ResponseCookie("custom", "value", {}), c(x), behavior(std::move(how)),
          collection(std::move(values)) {
    }
    HttpName to_string(HttpClock&) const override {
        c.event("cookie");
        if (behavior == "throw")
            throw std::runtime_error("cookie");
        if (behavior == "null")
            return {};
        if (behavior == "clear")
            collection.lock()->clear();
        if (behavior == "add")
            collection.lock()->add(std::make_shared<ResponseCookie>("added", "v", HttpDateTime{}));
        if (behavior == "replace")
            collection.lock()->set("custom", nullptr);
        return "custom";
    }
};
struct FakeBody : HttpBodyStream {
    Control& c;
    int64_t size;
    bool open;
    FakeBody(Control& x, int64_t length, bool readable) : c(x), size(length), open(readable) {
    }
    bool can_read() const override {
        c.event("body_read");
        return open;
    }
    int64_t length() const override {
        c.event("body_len");
        return size;
    }
    void dispose() override {
        c.event("body_dispose");
        open = false;
    }
};
namespace opensim::server {
struct HttpResponseProbe {
    static std::shared_ptr<HttpBodyStream> body(const HttpResponseState& r) {
        return r.body_;
    }
    static void body(HttpResponseState& r, std::shared_ptr<HttpBodyStream> value) {
        r.body_ = std::move(value);
    }
};
}
std::string snapshot(const std::shared_ptr<HttpResponseState>& r, const Context& context) {
    if (!r)
        return "NULL";
    auto encoding = r->encoding();
    std::string out = std::to_string(r->status()) + "," + std::to_string(int32_t(r->connection())) + "," +
                      std::to_string(r->priority()) + "," + std::to_string(r->keep_alive()) + "," +
                      std::to_string(r->headers_sent()) + "," + std::to_string(r->sent()) + "," +
                      std::to_string(r->raw_start()) + "," + std::to_string(r->raw_length()) + "," +
                      std::to_string(r->content_length()) + "," + std::to_string(r->request_id()) + "," +
                      std::to_string(std::bit_cast<uint64_t>(r->request_timestamp())) + "," +
                      std::to_string(r->chunked()) + "," + (encoding ? hex(encoding->name) : "N") + "," +
                      std::to_string(encoding == http_utf8_encoding()) + "," + named(r->protocol_version()) +
                      "," + named(r->reason()) + "," + named(r->content_type()) + "," +
                      std::to_string(context.timeout) + "," + std::to_string(r->cookies()->count());
    auto body = HttpResponseProbe::body(*r);
    std::string b = "N";
    if (body) {
        auto memory = std::dynamic_pointer_cast<HttpMemoryStream>(body);
        if (!memory)
            b = "F";
        else {
            b = body->can_read() ? std::to_string(body->length()) : "X";
            b += ":" + std::to_string(memory->get_buffer()->size()) + ":" +
                 (body->can_read() ? std::to_string(memory->position()) : "X");
        }
    }
    return out + "/" + bytes(r->raw_buffer()) + "/" + bytes(r->header_bytes()) + "/" + b;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Control c{network::parse_osd_json(unhex(line))};
        Clock clock(c);
        auto context = std::make_shared<Context>(c);
        std::shared_ptr<HttpResponseState> r;
        std::string result;
        try {
            if (network::osd_boolean(c.input.get("internal")))
                r = std::make_shared<HttpResponseState>(
                    network::osd_boolean(c.input.get("null_context")) ? nullptr : context,
                    c.input.contains("version") ? name(c.input.get("version")) : HttpName("HTTP/1.1"),
                    static_cast<HttpConnectionType>(
                        c.input.contains("connection") ? number(c.input.get("connection")) : 1),
                    clock);
            else
                r = std::make_shared<HttpResponseState>(network::osd_boolean(c.input.get("null_request"))
                                                            ? nullptr
                                                            : std::make_shared<Request>(c, context),
                                                        clock);
        } catch (...) {
            result = "CTOR;";
        }
        c.response = r;
        for (const auto& op : c.input.get("ops").array) {
            const auto action = network::osd_string(op.get("op"));
            std::string returned = "OK";
            try {
                if (!r)
                    throw std::runtime_error("null response");
                const auto value = op.get("v");
                if (action == "status")
                    r->status(number(value));
                else if (action == "priority")
                    r->priority(number(value));
                else if (action == "keep")
                    r->keep_alive(number(value));
                else if (action == "connection")
                    r->connection(static_cast<HttpConnectionType>(number(value)));
                else if (action == "chunked")
                    r->chunked(network::osd_boolean(value));
                else if (action == "protocol")
                    r->protocol_version(name(value));
                else if (action == "reason")
                    r->reason(name(value));
                else if (action == "type")
                    r->content_type(name(value));
                else if (action == "declared")
                    r->content_length(wide(value));
                else if (action == "encoding") {
                    auto text = network::osd_string(value);
                    r->encoding(text == "null"   ? nullptr
                                : text == "utf8" ? http_utf8_encoding()
                                                 : std::make_shared<HttpEncoding>(
                                                       HttpEncoding{text == "ascii"     ? "us-ascii"
                                                                    : text == "unicode" ? "utf-16"
                                                                                        : "utf-8"}));
                } else if (action == "raw") {
                    auto text = name(value);
                    r->raw_buffer(text ? std::make_shared<std::vector<uint8_t>>(text->begin(), text->end())
                                       : nullptr);
                } else if (action == "start")
                    r->raw_start(number(value));
                else if (action == "length")
                    r->raw_length(number(value));
                else if (action == "header")
                    r->add_header(name(op.get("key")), name(value));
                else if (action == "redirect")
                    r->redirect(name(value), op.contains("code") ? number(op.get("code")) : 302);
                else if (action == "headers")
                    r->get_headers();
                else if (action == "send")
                    r->send();
                else if (action == "next")
                    returned = r->send_next_async(number(value)) ? "1" : "0";
                else if (action == "continue")
                    r->check_send_next_async_continue();
                else if (action == "clear")
                    r->clear();
                else if (action == "body_fake")
                    HttpResponseProbe::body(
                        *r, std::make_shared<FakeBody>(c, wide(value),
                                                       !op.contains("readable") ||
                                                           network::osd_boolean(op.get("readable"))));
                else if (action == "body_dispose")
                    r->body()->dispose();
                else if (action.starts_with("body")) {
                    auto body = std::dynamic_pointer_cast<HttpMemoryStream>(r->body());
                    if (!body && action != "body_touch")
                        throw std::runtime_error("Not a memory stream");
                    if (action == "body_write") {
                        auto text = name(value).value_or("");
                        body->write(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
                    } else if (action == "body_read") {
                        std::vector<uint8_t> data(static_cast<size_t>(number(value)));
                        auto count = body->read(data);
                        returned =
                            std::to_string(count) + ":" +
                            hex(std::string(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(count)));
                    } else if (action == "body_byte")
                        returned = std::to_string(body->read_byte());
                    else if (action == "body_seek")
                        returned = std::to_string(
                            body->seek(wide(value), static_cast<HttpSeekOrigin>(number(op.get("origin")))));
                    else if (action == "body_array") {
                        auto data = body->to_array();
                        returned = hex(std::string(data.begin(), data.end()));
                    } else if (action == "body_write_to") {
                        auto target = std::make_shared<HttpMemoryStream>();
                        body->write_to(target);
                        auto data = target->to_array();
                        returned = hex(std::string(data.begin(), data.end()));
                    } else if (action == "body_position")
                        body->position(wide(value));
                    else if (action == "body_length")
                        body->set_length(wide(value));
                    else if (action == "body_copy") {
                        auto data = body->get_buffer();
                        auto start = number(op.get("start")), length = number(op.get("count"));
                        if (start < 0 || length < 0 || size_t(start) > data->size() ||
                            size_t(length) > data->size() - size_t(start))
                            throw std::out_of_range("copy");
                        body->write(std::span(data->data() + start, static_cast<size_t>(length)));
                    }
                } else if (action == "cookie") {
                    auto expires =
                        op.contains("expires") ? HttpDateTime{wide(op.get("expires")), 0} : HttpDateTime{};
                    if (op.contains("custom"))
                        r->cookies()->add(
                            std::make_shared<Cookie>(c, network::osd_string(op.get("custom")), r->cookies()));
                    else
                        r->cookies()->add(std::make_shared<ResponseCookie>(
                            name(op.get("key")), name(value), expires,
                            op.contains("path") ? name(op.get("path")) : HttpName("/"),
                            name(op.get("domain"))));
                } else if (action == "cookie_get" || action == "cookie_render" || action == "cookie_value" ||
                           action == "cookie_path" || action == "cookie_expiry") {
                    auto cookie = r->cookies()->get(name(op.get("key")));
                    if (!cookie)
                        throw std::runtime_error("Null cookie");
                    if (action == "cookie_get")
                        returned = named(HttpName(cookie->name())) + ":" + named(cookie->value()) + ":" +
                                   std::to_string(cookie->expires().ticks) + ":" +
                                   std::to_string(cookie->expires().kind) + ":" + named(cookie->path());
                    if (action == "cookie_render")
                        returned = named(cookie->to_string(clock));
                    if (action == "cookie_value")
                        cookie->value(name(value));
                    if (action == "cookie_path")
                        cookie->path(name(value));
                    if (action == "cookie_expiry")
                        cookie->expires({wide(value), op.contains("kind") ? number(op.get("kind")) : 0});
                } else if (action == "cookie_set") {
                    std::shared_ptr<ResponseCookie> cookie;
                    if (!network::osd_boolean(op.get("null")))
                        cookie = std::make_shared<ResponseCookie>(name(op.get("name")), name(value),
                                                                  HttpDateTime{});
                    r->cookies()->set(name(op.get("key")), cookie);
                } else if (action == "request_cookie" || action == "cookie_copy") {
                    auto cookie = std::make_shared<RequestCookie>(name(op.get("key")), name(value));
                    if (op.contains("after"))
                        cookie->value(name(op.get("after")));
                    if (action == "request_cookie")
                        returned = named(cookie->to_string(clock));
                    else
                        r->cookies()->add(cookie, {op.contains("expires") ? wide(op.get("expires")) : 0,
                                                   op.contains("kind") ? number(op.get("kind")) : 0});
                } else if (action == "cookie_clear")
                    r->cookies()->clear();
                else if (action == "cookie_null")
                    r->cookies()->set(name(value), nullptr);
            } catch (...) {
                returned = "EX";
            }
            result += returned + ":" + snapshot(r, *context) + ";";
        }
        std::cout << result << "|" << hex(c.trace) << "|" << snapshot(r, *context) << '\n';
    }
}
