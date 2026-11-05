#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/file_system_name.hpp"
#include "opensim/framework/nini_argv.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
using namespace opensim::framework;
using Text = ConfigCookieText;
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
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : *s)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string error(const std::exception& exception) {
    if (auto e = dynamic_cast<const ConfigHttpCookieError*>(&exception)) {
        std::ostringstream out;
        out << e->source_type << ':' << std::hex << e->hresult << ':'
            << hex(std::make_shared<const std::u16string>(e->message)) << ':'
            << hex(e->parameter ? std::make_shared<const std::u16string>(*e->parameter) : Text{}) << ':';
        if (e->inner)
            try {
                std::rethrow_exception(e->inner);
            } catch (const std::exception& nested) {
                out << error(nested);
            }
        else
            out << '~';
        return out.str();
    }
    return "NATIVE:" + std::string(exception.what());
}
std::optional<std::uint64_t> source_seed;
std::shared_ptr<ConfigHttpCookiesBackend> backend(Text fqdn) {
    ConfigHttpCookieNativeOptions options;
    options.fqdn_snapshot = fqdn;
    if (source_seed)
        options.hash_seed = *source_seed;
    else
        options.domain_hash = [](Text key) {
            std::uint32_t h = 2166136261u;
            for (auto c : *key)
                h = (h ^ c) * 16777619u;
            return h;
        };
    return native_invariant_config_http_cookies_backend(std::move(options));
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--fqdn") {
        std::cout << hex(native_config_http_cookie_fqdn_domain()) << '\n';
        return 0;
    }
    if (argc > 2 && std::string_view(argv[1]) == "--hash") {
        auto seed = std::stoull(argv[2], nullptr, 16);
        for (std::string line; std::getline(std::cin, line);)
            std::cout << std::hex << std::setw(8) << std::setfill('0')
                      << config_http_cookie_marvin_hash(*dec(line), seed) << '\n';
        return 0;
    }
    if (argc > 3 && std::string_view(argv[2]) == "--source-seed")
        source_seed = std::stoull(argv[3], nullptr, 16);
    if (argc == 1) {
        auto b = backend(std::make_shared<const std::u16string>());
        ConfigHttpCookies jar(b);
        auto uri = ConfigUriComponents::absolute(std::make_shared<const std::u16string>(u"http://example/a"),
                                                 native_config_uri_components_backend());
        jar.set_cookies(uri, std::make_shared<const std::u16string>(u"a=b"));
        if (*jar.header(uri) != u"a=b" || jar.count() != 1)
            return 1;
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::unique_ptr<ConfigHttpCookies> jar;
        std::vector<std::string> out;
        std::istringstream input(line);
        for (std::string operation; std::getline(input, operation, '|');) {
            std::istringstream op(operation);
            std::string command;
            op >> command;
            auto text = [&]() {
                std::string s;
                op >> s;
                return dec(s);
            };
            auto uri = [&]() {
                auto v = text();
                return v ? ConfigUriComponents::absolute(v, native_config_uri_components_backend())
                         : std::shared_ptr<ConfigUriComponents>{};
            };
            try {
                if (command == "N") {
                    int cap, per, size;
                    op >> cap >> per >> size;
                    jar = std::make_unique<ConfigHttpCookies>(backend(text()), cap, per, size);
                } else if (command == "S" || command == "RX") {
                    auto u = uri();
                    auto value = text();
                    if (command == "S")
                        jar->set_cookies(u, value);
                    else
                        jar->receive(u, {value});
                } else if (command == "H")
                    out.push_back(hex(jar->header(uri())));
                else if (command == "G") {
                    auto values = jar->cookies(uri());
                    std::string s;
                    for (auto& c : values)
                        s += hex(c.name) + ',' + hex(c.value) + ',' + hex(c.domain) + ',' + hex(c.path) +
                             ',' + hex(c.port) + ',' + std::to_string(c.version) + ',' +
                             std::to_string(c.variant) + ',' + (c.secure ? "1" : "0") + ',' +
                             (c.http_only ? "1" : "0") + ',' + (c.discard ? "1" : "0") + ';';
                    out.push_back(s);
                } else if (command == "C")
                    out.push_back(std::to_string(jar->count()));
                out.push_back("ok");
            } catch (const std::exception& e) {
                out.push_back(error(e));
            }
        }
        for (std::size_t i = 0; i < out.size(); ++i)
            std::cout << (i ? "/" : "") << out[i];
        std::cout << '\n';
    }
}
