#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_file_uri_object.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>
using namespace opensim::framework;
ConfigUriText decode(std::string s) {
    if (s == "~")
        return {};
    if (s == "-")
        return runtime_empty_text();
    std::u16string v;
    for (size_t i = 0; i < s.size(); i += 4)
        v += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return std::make_shared<const std::u16string>(v);
}
std::string hex(std::u16string_view value) {
    std::string s;
    for (auto c : value)
        for (int b = 12; b >= 0; b -= 4)
            s += "0123456789abcdef"[(c >> b) & 15];
    return s.empty() ? "-" : s;
}
std::string error(const ConfigUriComponentsError& e) {
    auto range = dynamic_cast<const ConfigFileUriRangeError*>(&e);
    return "E:" + e.source_type + ":" + std::to_string(static_cast<std::int32_t>(e.hresult)) + ":" +
           hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "~") + ":" +
           (range && range->actual_value ? std::to_string(*range->actual_value) : "~");
}

std::shared_ptr<ConfigUriObjectBackend> make_backend() {
    return native_config_uri_object_backend();
}

int copied_backend_unit() {
    try {
        std::weak_ptr<ConfigUriComponentsBackend> dependencies;
        {
            auto components = native_config_uri_components_backend();
            dependencies = components;
            ConfigNativeUriObjectOptions options;
            options.components = components;
            auto owner = native_config_uri_object_backend(std::move(options));
            std::weak_ptr<ConfigUriObjectBackend> original = owner;
            auto copied = *owner;
            owner.reset();
            components.reset();
            if (!original.expired())
                throw std::runtime_error("Factory owner must not be retained through a callback cycle");
            if (dependencies.expired())
                throw std::runtime_error("Copied callbacks must retain their dependencies");
            for (auto scheme : {u"a+b", u"http"}) {
                auto prefix = std::u16string(scheme) + u"://host";
                auto base = copied.create(std::make_shared<const std::u16string>(prefix + u"/a/b"),
                                          ConfigUriObjectKind::Absolute);
                auto relative = copied.create(std::make_shared<const std::u16string>(u"../c"),
                                              ConfigUriObjectKind::Relative);
                if (*copied.combine_uri(base, relative)->absolute_uri() != prefix + u"/c")
                    throw std::runtime_error("Detached URI-object resolution differs");
                if (*copied.combine_string(base, std::make_shared<const std::u16string>(u"../d"))
                         ->absolute_uri() != prefix + u"/d")
                    throw std::runtime_error("Detached string resolution differs");
                if (std::u16string_view(scheme) == u"http") {
                    auto view = base->http_components();
                    if (!view || view->absolute_uri() != base->absolute_uri())
                        throw std::runtime_error(
                            "HTTP object/component AbsoluteUri must share the original getter cache");
                    auto absolute_clone = copied.combine_uri(base, base);
                    if (!absolute_clone->http_components() ||
                        absolute_clone->http_components()->absolute_uri() != absolute_clone->absolute_uri())
                        throw std::runtime_error("HTTP clone components must view the cloned object cache");
                    auto empty_same_scheme = copied.create(std::make_shared<const std::u16string>(u"http:"),
                                                           ConfigUriObjectKind::RelativeOrAbsolute);
                    auto clone = copied.combine_uri(base, empty_same_scheme);
                    if (!clone->http_components() || *clone->absolute_uri() != prefix + u"/a/b")
                        throw std::runtime_error("Resolved HTTP clone must retain its transport components");
                }
                if (*copied.with_fragment(base, std::make_shared<const std::u16string>(u"f"))
                         ->absolute_uri() != prefix + u"/a/b#f")
                    throw std::runtime_error("Detached fragment builder differs");
            }
        }
        if (!dependencies.expired())
            throw std::runtime_error("URI callbacks retained an ownership cycle");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Copied URI backend lifetime: " << e.what() << "\n";
        return 10;
    }
}
int main(int argc, char**) {
    auto backend = native_config_uri_components_backend();
    bool windows =
#ifdef _WIN32
        true;
#else
        false;
#endif
    auto operations = make_backend();
    auto create = operations->create;
    if (argc == 1) {
        if (auto code = copied_backend_unit())
            return code;
        auto base = create(decode("0061002b0062003a002f002f0068006f00730074002f0061002f0062"),
                           ConfigUriObjectKind::Absolute);
        auto rel = create(std::make_shared<const std::u16string>(u"../c"), ConfigUriObjectKind::Relative);
        auto resolved = operations->combine_uri(base, rel);
        if (*resolved->absolute_uri() != u"a+b://host/c")
            return 1;
        auto changed = operations->with_fragment(base, std::make_shared<const std::u16string>(u"f"));
        if (*changed->absolute_uri() != u"a+b://host/a/b#f")
            return 2;
        return 0;
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        int op;
        std::string b, r;
        in >> op >> b >> r;
        try {
            auto bt = decode(b), rt = decode(r);
            auto base = bt ? create(bt, ConfigUriObjectKind::RelativeOrAbsolute) : ConfigUriObjectPtr{};
            ConfigUriObjectPtr relative, result;
            if (op < 2) {
                relative = rt ? create(rt, ConfigUriObjectKind::RelativeOrAbsolute) : ConfigUriObjectPtr{};
                if (op == 1)
                    for (auto u : {base, relative})
                        if (u)
                            for (int n = 0; n < 4; ++n)
                                try {
                                    switch (n) {
                                    case 0:
                                        u->to_string();
                                        break;
                                    case 1:
                                        u->absolute_uri();
                                        break;
                                    case 2:
                                        u->local_path();
                                        break;
                                    default:
                                        u->fragment();
                                    }
                                } catch (const ConfigUriComponentsError&) {
                                }
                result = operations->combine_uri(base, relative);
            } else
                result = operations->with_fragment(base, rt);
            std::cout << (result == base       ? "B"
                          : result == relative ? "R"
                                               : "N")
                      << ":" << (result->is_absolute() ? "A" : "R");
            for (char c : std::string("otalfsotalfs")) {
                std::cout << "|" << c << ":";
                try {
                    ConfigUriText value;
                    switch (c) {
                    case 'o':
                        value = result->original_string();
                        break;
                    case 't':
                        value = result->to_string();
                        break;
                    case 'a':
                        value = result->absolute_uri();
                        break;
                    case 'l':
                        value = result->local_path();
                        break;
                    case 'f':
                        value = result->fragment();
                        break;
                    default:
                        value = result->scheme();
                    }
                    std::cout << hex(*value);
                } catch (const ConfigUriComponentsError& e) {
                    std::cout << error(e);
                }
            }
        } catch (const ConfigUriComponentsError& e) {
            std::cout << error(e);
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what();
        }
        std::cout << "\n";
    }
}
