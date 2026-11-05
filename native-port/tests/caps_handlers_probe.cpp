#include "opensim/server/caps_handlers.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace opensim::server;
std::string hex(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
struct Handler : CapabilityStreamHandler, CapabilitySimpleHandler {
    std::optional<std::string> value;
    explicit Handler(std::optional<std::string> s) : value(std::move(s)) {
    }
    std::optional<std::string> path() const override {
        return value;
    }
    std::string method() const override {
        return "HEAD";
    }
    opensim::network::HttpResponse handle(const opensim::network::HttpRequest&) override {
        return {};
    }
};
struct Listener : CapabilityListener {
    std::string trace, fail;
    bool ssl = false;
    bool use_ssl() const override {
        return ssl;
    }
    void log(std::string s) {
        trace += s + ";";
        if (s == fail)
            throw std::runtime_error("injected");
    }
    void add_stream(const std::shared_ptr<CapabilityStreamHandler>& h) override {
        log("A" + h->path().value_or(""));
    }
    void remove_stream(const std::string& m, const std::optional<std::string>& p) override {
        log(m + p.value_or(""));
    }
    void add_simple(const std::shared_ptr<CapabilitySimpleHandler>& h) override {
        log("S" + h->path().value_or(""));
    }
    void remove_simple(const std::optional<std::string>& p) override {
        log("R" + p.value_or(""));
    }
};
template <class F> std::string attempt(F f) {
    try {
        return f();
    } catch (const std::exception&) {
        return "EX";
    }
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int ssl, setup, action, failure;
        in >> ssl >> setup >> action >> failure;
        std::string cap = (setup & 8) ? "試験😀<&'\"" : "X";
        auto l = std::make_shared<Listener>();
        l->ssl = ssl != 0;
        CapsHandlers c(l, "host&\"", 9000);
        auto n = std::make_shared<Handler>("/normal<&'\"");
        auto s = std::make_shared<Handler>("/simple");
        auto seed = std::make_shared<Handler>("/seed");
        if (setup & 16) {
            n->value = std::nullopt;
            s->value = std::nullopt;
        }
        c.set("SEED", seed);
        if (setup & 1)
            c.add_simple(cap, s, false);
        if (setup & 2)
            c.set(cap, n);
        if (setup & 4)
            c.set("Other", n);
        l->trace.clear();
        std::string fs[] = {
            "",         "POST/normal<&'\"", "PUT/normal<&'\"", "GET/normal<&'\"", "DELETE/normal<&'\"",
            "R/simple", "A/normal<&'\"",    "S/simple"};
        l->fail = fs[failure];
        std::string result = attempt([&]() {
            switch (action) {
            case 0:
                c.remove(cap);
                break;
            case 1:
                c.set(cap, n);
                break;
            case 2:
                c.set(cap, nullptr);
                break;
            case 3:
                c.add_simple(cap, s);
                break;
            case 4:
                c.add_simple(cap, s, false);
                break;
            case 5:
                return c.get(cap) == n ? std::string("N") : std::string("?");
            case 6:
                c.remove(std::nullopt);
                break;
            case 7:
                c.set(std::nullopt, n);
                break;
            case 8:
                return c.contains(cap) ? std::string("1") : std::string("0");
            }
            return std::string("OK");
        });
        auto dump = [](const auto& map) {
            std::vector<std::string> a;
            for (const auto& e : map)
                a.push_back(hex(e.first) + ":" + (e.second ? hex(*e.second) : "-"));
            std::sort(a.begin(), a.end());
            std::string r;
            for (const auto& v : a)
                r += v + ",";
            return r;
        };
        std::vector<std::string> names = c.caps();
        std::sort(names.begin(), names.end());
        std::string caps;
        for (const auto& name : names)
            caps += name + ",";
        CapsHandlers::Requested req = {cap, "SEED", "Other", "missing", cap};
        std::string xml;
        c.append_details_xml(&req, &xml);
        auto snapshot = c.streams();
        snapshot.erase("SEED");
        std::cout << result << "|" << hex(l->trace) << "|" << caps << "|" << dump(c.details(true, nullptr))
                  << "|" << dump(c.details(true, &req)) << "|" << dump(c.local_paths()) << "|" << hex(xml)
                  << "|" << c.contains("SEED") << "\n";
    }
}
