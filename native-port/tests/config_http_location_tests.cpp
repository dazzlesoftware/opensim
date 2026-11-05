#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
using Text = ConfigUriText;
Text dec(std::string s) {
    if (s == "~")
        return {};
    std::u16string text;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            text += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return std::make_shared<const std::u16string>(std::move(text));
}
std::string hex(Text text) {
    if (!text)
        return "~";
    if (text->empty())
        return "-";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : *text)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string error(const ConfigUriComponentsError& e) {
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':'
        << hex(std::make_shared<const std::u16string>(e.message)) << ':'
        << hex(e.parameter ? std::make_shared<const std::u16string>(*e.parameter) : Text{});
    return out.str();
}
std::string snapshot(ConfigUriObjectPtr uri) {
    if (!uri)
        return "null";
    auto value = std::string(uri->is_absolute() ? "A/" : "R/") + hex(uri->original_string());
    if (uri->is_absolute()) {
        for (auto getter :
             {&ConfigUriObject::scheme, &ConfigUriObject::fragment, &ConfigUriObject::absolute_uri,
              &ConfigUriObject::to_string, &ConfigUriObject::local_path})
            try {
                value += "/" + hex((uri.get()->*getter)());
            } catch (const ConfigUriComponentsError& e) {
                value += "/" + error(e);
            }
    } else
        value += "/" + hex(uri->to_string());
    return value;
}
int main(int argc, char**) {
    auto backend = native_http_uri_object_backend();
    ConfigHttpLocationAdapter adapter(backend);
    if (argc == 1) {
        auto base = backend.create(dec("0068007400740070003a002f002f0078002f006100230066"),
                                   ConfigUriObjectKind::Absolute);
        auto target = adapter.redirect(base, 302, {dec("002f0062")});
        if (!target || *target->absolute_uri() != u"http://x/b#f")
            return 1;
        auto retry_backend = backend;
        int attempts = 0;
        Text seen;
        retry_backend.try_create = [&](Text value, ConfigUriObjectKind) {
            ++attempts;
            if (attempts == 1) {
                seen = value;
                return ConfigUriObjectPtr{};
            }
            if (seen != value)
                throw std::runtime_error("Unchanged decode must retain input identity");
            return base;
        };
        ConfigHttpLocationAdapter retry(retry_backend);
        if (retry.parse_value(dec("0066006f006f")) != base || attempts != 2)
            return 2;
        attempts = 0;
        if (retry.redirect({}, 200, {dec("0066006f006f")}))
            return 3;
        if (attempts)
            return 4;
        auto invalid_backend = backend;
        attempts = 0;
        invalid_backend.try_create = [&](Text, ConfigUriObjectKind) {
            ++attempts;
            return ConfigUriObjectPtr{};
        };
        ConfigHttpLocationAdapter invalid(invalid_backend);
        if (invalid.redirect(base, 302, {dec("0066006f006f"), base->original_string()}) || attempts != 2)
            return 5;
        bool required = false;
        try {
            ConfigHttpLocationAdapter bad({});
        } catch (const std::invalid_argument&) {
            required = true;
        }
        if (!required)
            return 6;
        return 0;
    }
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream row(line);
        std::string mode, a, b;
        row >> mode;
        try {
            if (mode == "D") {
                row >> a;
                auto text = dec(a);
                auto result = config_http_location_decode_utf8(text);
                std::cout << hex(result) << ':' << (text == result ? "1" : "0") << '\n';
                continue;
            }
            if (mode == "P") {
                row >> a;
                std::cout << snapshot(adapter.parse_value(dec(a))) << '\n';
                continue;
            }
            if (mode == "T") {
                int kind;
                row >> a >> kind;
                std::cout << snapshot(backend.try_create(dec(a), static_cast<ConfigUriObjectKind>(kind)))
                          << '\n';
                continue;
            }
            if (mode == "C") {
                int kind;
                row >> a >> kind;
                std::cout << snapshot(backend.create(dec(a), static_cast<ConfigUriObjectKind>(kind))) << '\n';
                continue;
            }
            if (mode == "B") {
                row >> a >> b;
                std::cout << snapshot(backend.with_fragment(
                                 backend.create(dec(a), ConfigUriObjectKind::Absolute), dec(b)))
                          << '\n';
                continue;
            }
            if (mode == "O") {
                row >> a >> b;
                std::cout << snapshot(backend.combine_uri(
                                 backend.create(dec(a), ConfigUriObjectKind::Absolute),
                                 backend.create(dec(b), ConfigUriObjectKind::RelativeOrAbsolute)))
                          << '\n';
                continue;
            }
            if (mode == "R") {
                int status;
                row >> a >> status;
                auto request = a == "~" ? ConfigUriObjectPtr{}
                                        : backend.create(dec(a), ConfigUriObjectKind::RelativeOrAbsolute);
                std::vector<Text> values;
                while (row >> b)
                    values.push_back(dec(b));
                std::cout << snapshot(adapter.redirect(request, status, values)) << '\n';
                continue;
            }
            throw std::runtime_error("Unknown mode");
        } catch (const ConfigUriComponentsError& e) {
            std::cout << error(e) << '\n';
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what() << '\n';
        }
    }
}
