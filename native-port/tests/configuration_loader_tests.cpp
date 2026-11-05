#include "opensim/framework/configuration_loader.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
using T = NiniWriterText;
T t(std::u16string s) {
    return std::make_shared<const std::u16string>(std::move(s));
}
T dec(std::string s) {
    if (s == "null")
        return {};
    if (s == "-")
        return t(u"");
    std::u16string x;
    for (std::size_t i = 0; i < s.size(); i += 4)
        x += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return t(x);
}
std::string enc(T s) {
    if (!s)
        return "null";
    if (s->empty())
        return "-";
    const char* h = "0123456789abcdef";
    std::string x;
    for (auto c : *s)
        for (int i = 12; i >= 0; i -= 4)
            x += h[(c >> i) & 15];
    return x;
}
struct Exit : std::runtime_error {
    Exit() : runtime_error("exit:1") {
    }
};
std::shared_ptr<NiniConfigBackend> fixture_backend() {
    auto b = std::make_shared<NiniConfigBackend>();
    b->capture_number_format = [] {
        auto f = std::make_shared<NiniNumberFormat>();
        f->to_int32 = [](T) -> std::int32_t {
            throw std::logic_error("Unexpected fixture integer conversion");
        };
        f->to_int64 = [](T) -> std::int64_t { throw std::logic_error("Unexpected fixture long conversion"); };
        f->to_single = [](T) -> float { throw std::logic_error("Unexpected fixture float conversion"); };
        f->to_double = [](T) -> double { throw std::logic_error("Unexpected fixture double conversion"); };
        return f;
    };
    b->insensitive_table = [] {
        auto x = std::make_shared<NiniInsensitiveTable>();
        x->equal = [](T a, T b) {
            auto lower = [](std::u16string s) {
                for (auto& c : s)
                    if (c >= u'A' && c <= u'Z')
                        c += 32;
                return s;
            };
            return lower(*a) == lower(*b);
        };
        return x;
    };
    b->lower_current = [](T p) {
        auto s = *p;
        for (auto& c : s)
            if (c >= u'A' && c <= u'Z')
                c += 32;
        return t(s);
    };
    b->index_of_current = [](T p, T q, int start) {
        auto n = p->find(*q, start);
        return n == p->npos ? -1 : int(n);
    };
    b->document = native_nini_document_factory();
    b->file_reader = b->document.file_reader;
    b->stream_reader = b->document.stream_reader;
    return b;
}
struct Row {
    std::vector<std::string> trace;
    std::string fault;
    int nth = 1, hit = 0;
    T dir, env;
    std::shared_ptr<NiniConfigArena> arena = std::make_shared<NiniConfigArena>(fixture_backend());
    std::shared_ptr<RegionConfigurationBackend> backend;
    std::shared_ptr<RegionConfigurationLoader> loader;
    void event(std::string name, std::vector<T> args = {}) {
        auto s = name + ":";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i)
                s += ':';
            s += enc(args[i]);
        }
        trace.push_back(s);
        if (fault == "mutate_source" && name == "log" && args.size() > 2 &&
            args[2]->starts_with(u"[CONFIG]: Reading configuration file") && loader->config())
            loader->config()->source = nullptr;
        if (name == fault && ++hit == nth)
            throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"provider fault");
    }
    Row() {
        backend = std::make_shared<RegionConfigurationBackend>(native_local_region_configuration_backend(
            arena, std::make_shared<RegionApplicationConfigurationState>()));
        auto& b = *backend;
        b.application->ini_file_path = nullptr;
        b.config_directory = [this] {
            event("configdir");
            return dir;
        };
        auto c = b.combine;
        b.combine = [this, c](T a, T v) {
            event("combine", {a, v});
            return c(a, v);
        };
        for (auto pair : {std::pair{"full", &b.full_path}, std::pair{"extension", &b.extension}}) {
            auto f = *pair.second;
            *pair.second = [this, f, n = pair.first](T a) {
                event(n, {a});
                return f(a);
            };
        }
        for (auto pair :
             {std::pair{"fileexists", &b.file_exists}, std::pair{"direxists", &b.directory_exists}}) {
            auto f = *pair.second;
            *pair.second = [this, f, n = pair.first](T a) {
                event(n, {a});
                return f(a);
            };
        }
        for (auto pair : {std::pair{"files", &b.directory_files}, std::pair{"glob", &b.glob}}) {
            auto f = *pair.second;
            *pair.second = [this, f, n = pair.first](T a) {
                event(n, {a});
                return f(a);
            };
        }
        b.is_http_uri = [](T s) {
            if (!s)
                return false;
            auto v = *s;
            for (auto& c : v)
                if (c >= u'A' && c <= u'Z')
                    c += 32;
            return v.starts_with(u"http://") || v.starts_with(u"https://");
        };
        b.starts_with_current = [](T a, T p) {
            if (!a)
                throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                      u"Object reference not set to an instance of an object.");
            return a->starts_with(*p);
        };
        b.xml_uri_source = [this](T uri) -> std::shared_ptr<NiniConfigSourceBase> {
            event("xml", {uri, t(u"Ignore")});
            throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"xml provider fault");
        };
        b.merge_environment = [this](auto source) {
            event("environment");
            if (env && !env->empty())
                source->merge(arena->create_ini_source(nini_string_reader(*env)));
        };
        b.replace_key_values = [](auto s) { s->expand_key_values(); };
        b.create_network_info = [this] {
            event("networknew");
            return std::static_pointer_cast<void>(std::make_shared<int>(1));
        };
        b.load_network_info = [this](auto, auto) { event("networkload"); };
        b.log = [this](RegionLoaderLogLevel level, T f, std::vector<T> args, RegionLoaderLogCall call) {
            static const char16_t* names[] = {u"Info", u"Warn", u"Error", u"Fatal"};
            std::string x;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i)
                    x += ',';
                x += enc(args[i]);
            }
            event("log", {t(call == RegionLoaderLogCall::Format ? u"Format" : u"Message"),
                          t(names[int(level)]), f, t(utf16_replacing(x))});
        };
        b.exit = [this](int n) {
            trace.push_back("exit:" + std::to_string(n));
            throw Exit();
        };
        b.exception_message = [](std::exception_ptr p) {
            try {
                std::rethrow_exception(p);
            } catch (const NiniConfigError& e) {
                return t(e.message);
            } catch (const std::exception& e) {
                return t(utf16_replacing(e.what()));
            }
        };
        loader = std::make_shared<RegionConfigurationLoader>(backend);
    }
    std::string snapshot() {
        std::string out = enc(loader->ini_file_path()) + "/";
        if (auto s = loader->config_settings())
            out += enc(s->physics_engine) + "," + enc(s->mesh_engine_name) + "," + enc(s->clientstack_dll);
        out += '/';
        if (auto c = loader->config()) {
            if (!c->source)
                return out + "nullsource";
        }
        if (auto c = loader->config())
            for (auto e = c->source->configs()->get_enumerator(); e.move_next();) {
                auto cfg = std::dynamic_pointer_cast<NiniConfig>(e.current());
                out += enc(cfg->name()) + '[';
                for (auto& key : cfg->get_keys())
                    out += enc(key) + '=' + enc(cfg->get(key)) + ',';
                out += ']';
            }
        return out;
    }
};
std::string error(const std::string& type, T msg, std::uint32_t hr, T param = {}) {
    return type + ":" + enc(msg) + ":" + std::to_string(hr) + ":" + enc(param);
}
}
int main(int argc, char**) {
    if (argc == 1) {
        bool rejected = false;
        try {
            RegionConfigurationLoader bad(nullptr);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected)
            return 1;
        auto defaults = std::make_shared<RegionApplicationConfigurationState>();
        if (defaults->ini_file_path != runtime_empty_text())
            return 1;
        Row row;
        bool null_before_load = false;
        try {
            row.loader->read_config_settings();
        } catch (const NiniConfigError& e) {
            null_before_load = e.kind == NiniConfigError::Kind::NullReference && e.hresult() == 0x80004003u;
        }
        if (!null_before_load)
            return 1;
        RegionConfigurationLoader second(row.backend);
        auto retained = t(u"retained");
        row.backend->application->ini_file_path = retained;
        if (row.loader->ini_file_path() != retained || second.ini_file_path() != retained)
            return 1;
        int calls = 0;
        row.backend->is_http_uri = [&calls](T) {
            ++calls;
            return true;
        };
        if (!row.loader->is_uri(t(u"provider-test")) || calls != 1)
            return 1;
        row.backend->is_http_uri = [](T) { return false; };
        if (row.loader->is_uri(t(u"provider-test")))
            return 1;
        std::cout << "PASS: configuration loader required providers, pre-load NRE and shared caller state\n";
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string mode, argv, dir, list, env, fault;
        int nth;
        in >> mode >> argv >> dir >> list >> env >> fault >> nth;
        Row r;
        r.dir = dec(dir);
        r.env = dec(env);
        r.fault = fault;
        r.nth = nth;
        std::string result = "ok";
        try {
            auto source = r.arena->create_ini_source(nini_string_reader(*dec(argv)));
            if (mode == "R")
                result = r.loader->read_config(
                             std::make_shared<RegionLoaderConfigSource>(RegionLoaderConfigSource{source}),
                             dec(list))
                             ? "1"
                             : "0";
            else if (mode == "I") {
                std::vector<T> paths;
                std::u16string value;
                std::basic_istringstream<char16_t> unused;
                auto items = *dec(list);
                std::size_t start = 0;
                for (std::size_t n = 0; n <= items.size(); ++n)
                    if (n == items.size() || items[n] == u'\n') {
                        if (n > start)
                            paths.push_back(t(items.substr(start, n - start)));
                        start = n + 1;
                    }
                r.loader->add_includes(
                    std::make_shared<RegionLoaderConfigSource>(RegionLoaderConfigSource{source}), paths);
                result = "";
                for (std::size_t i = 0; i < paths.size(); ++i) {
                    if (i)
                        result += ',';
                    result += enc(paths[i]);
                }
            } else {
                std::shared_ptr<RegionLoaderConfigSettings> settings;
                std::shared_ptr<void> network;
                r.loader->load_config_settings(source, {}, settings, network);
                if (mode == "T") {
                    r.loader = std::make_shared<RegionConfigurationLoader>(r.backend);
                    r.loader->load_config_settings(r.arena->create_ini_source(nini_string_reader(*dec(list))),
                                                   {}, settings, network);
                }
            }
        } catch (const Exit&) {
            result = error("Shim.ExitException", t(u"exit:1"), 0x80131500u);
        } catch (const NiniConfigError& e) {
            result =
                error(e.source_type(), t(e.message), e.hresult(), e.parameter ? t(*e.parameter) : nullptr);
        } catch (const NiniStreamError& e) {
            result = error(e.source_type, t(e.message), e.hresult, e.parameter ? t(*e.parameter) : nullptr);
        } catch (const NiniReaderError& e) {
            result = error(e.source_type(), t(e.message_utf16()), e.hresult());
        } catch (const NiniDocumentError& e) {
            result =
                error(e.source_type(), t(e.message), e.hresult(), e.parameter ? t(*e.parameter) : nullptr);
        } catch (const DirectoryEntriesError& e) {
            result = error(e.source_type, t(e.message), e.hresult, e.parameter ? t(*e.parameter) : nullptr);
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
        std::cout << result << '/' << r.snapshot() << '/';
        for (std::size_t i = 0; i < r.trace.size(); ++i) {
            if (i)
                std::cout << ';';
            std::cout << r.trace[i];
        }
        std::cout << '\n';
    }
    return 0;
}
