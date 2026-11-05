#include "opensim/framework/config_uri_components.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim::framework;
using Text = ConfigUriText;
Text dec(std::string s) {
    if (s == "~")
        return {};
    std::u16string r;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            r += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return std::make_shared<const std::u16string>(r);
}
std::string hex(Text s) {
    if (!s)
        return "~";
    if (s->empty())
        return "-";
    std::ostringstream r;
    r << std::hex << std::setfill('0');
    for (auto c : *s)
        r << std::setw(4) << static_cast<unsigned>(c);
    return r.str();
}
std::string error(const ConfigUriComponentsError& e) {
    std::ostringstream r;
    r << e.source_type << ':' << std::hex << e.hresult << ':'
      << hex(std::make_shared<const std::u16string>(e.message)) << ':'
      << hex(e.parameter ? std::make_shared<const std::u16string>(*e.parameter) : Text{});
    return r.str();
}
int main(int argc, char**) {
    auto backend = native_config_uri_components_backend();
    if (argc == 1) {
        auto value = ConfigUriComponents::absolute(
            dec("0068007400740070003a002f002f006500780061006d0070006c0065002f0061002f002e002e002f0062"),
            backend);
        if (*value->absolute_path() != u"/b")
            return 1;
        try {
            ConfigUriComponents::absolute({}, backend);
            return 2;
        } catch (const ConfigUriComponentsError& e) {
            if (e.source_type != "System.ArgumentNullException" ||
                e.parameter != std::optional<std::u16string>(u"uriString"))
                return 3;
        }
        auto bad = ConfigUriComponents::absolute(
            std::make_shared<const std::u16string>(u"http://\u200d.com/"), backend);
        if (*bad->host() != u"\u200d.com")
            return 4;
        try {
            bad->idn_host();
            return 5;
        } catch (const ConfigUriComponentsError& e) {
            if (e.source_type != "System.UriFormatException")
                return 6;
        }
        if (*bad->absolute_path() != u"/")
            return 7;
        auto short_slashes = ConfigUriComponents::resolve(value, dec("005c005c"));
        if (*short_slashes->scheme() != u"http" || *short_slashes->absolute_path() != u"//")
            return 8;
        auto padded = std::make_shared<const std::u16string>(u" \r\nhttp://example/a \t");
        auto replaced = ConfigUriComponents::resolve(value, padded);
        if (*replaced->original_string() != *padded || *replaced->absolute_uri() != u"http://example/a")
            return 9;
        return 0;
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream input(row);
        std::string mode, a, b;
        input >> mode >> a >> b;
        std::string out;
        try {
            auto uri = ConfigUriComponents::absolute(dec(a), backend);
            if (mode == "R" || mode == "RS")
                uri = ConfigUriComponents::resolve(uri, dec(b));
            out = "ok";
            if (mode == "RS") {
                std::cout << out << '/' << hex(uri->original_string()) << '/' << hex(uri->scheme()) << '\n';
                continue;
            }
            std::vector<std::function<Text()>> getters = {[&]() { return uri->original_string(); },
                                                          [&]() { return uri->scheme(); },
                                                          [&]() { return uri->host(); },
                                                          [&]() { return uri->idn_host(); },
                                                          [&]() { return uri->dns_safe_host(); },
                                                          [&]() {
                                                              auto p = std::to_string(uri->port());
                                                              return std::make_shared<const std::u16string>(
                                                                  p.begin(), p.end());
                                                          },
                                                          [&]() { return uri->user_info(); },
                                                          [&]() { return uri->absolute_path(); },
                                                          [&]() { return uri->query(); },
                                                          [&]() { return uri->fragment(); },
                                                          [&]() { return uri->absolute_uri(); }};
            for (auto& f : getters)
                try {
                    out += '/' + hex(f());
                } catch (const ConfigUriComponentsError& e) {
                    out += '/' + error(e);
                } catch (const std::exception& e) {
                    out += "/NATIVE:" + std::string(e.what());
                }
        } catch (const ConfigUriComponentsError& e) {
            out = error(e);
        } catch (const std::exception& e) {
            out = "NATIVE:" + std::string(e.what());
        }
        std::cout << out << '\n';
    }
}
