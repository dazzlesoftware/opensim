// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/migration.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
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
    if (s == ".")
        return "";
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += char(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
std::optional<std::string> nullable(const Value& v) {
    return v.kind == Value::Kind::Undefined ? std::nullopt
                                            : std::optional<std::string>(network::osd_string(v));
}
struct Resources : MigrationResources {
    Value input;
    std::string trace;
    explicit Resources(Value v) : input(std::move(v)) {
    }
    void event(const std::string& value) {
        trace += value + ";";
        if (network::osd_string(input.get("fail")) == value)
            throw std::runtime_error("injected");
    }
    std::vector<std::string> names() override {
        event("names");
        std::vector<std::string> result;
        for (auto& v : input.get("resources").array)
            result.push_back(network::osd_string(v.get("name")));
        return result;
    }
    std::optional<std::string> bytes(const std::string& name) override {
        event("read:" + name);
        for (auto& v : input.get("resources").array)
            if (network::osd_string(v.get("name")) == name) {
                if (v.get("data").kind == Value::Kind::Undefined)
                    return {};
                return unhex(network::osd_string(v.get("data")));
            }
        return {};
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto input = network::parse_osd_json(unhex(line));
        auto resources = std::make_shared<Resources>(input);
        std::string result = "OK";
        try {
            Migration m({}, resources, nullable(input.get("sub")), nullable(input.get("type")),
                        {[&](auto s) { resources->event("info:" + s); },
                         [&](auto s) { resources->event("debug:" + s); },
                         [&](auto s) { resources->event("error:" + s); }});
            auto scripts = m.migrations_after(network::osd_integer(input.get("after")));
            result += "|";
            for (auto& [version, batches] : scripts) {
                result += std::to_string(version) + "=";
                for (auto& sql : batches)
                    result += hex(sql) + ",";
                result += ";";
            }
        } catch (...) {
            result = "EX";
        }
        std::cout << result << '|' << hex(resources->trace) << '\n';
    }
}
