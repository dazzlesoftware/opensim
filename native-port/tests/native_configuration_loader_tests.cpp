#include "opensim/framework/native_configuration_loader.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/network_servers_info.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <map>
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
    return native_nini_invariant_config_backend();
}
class StartupProxy : public NiniConfig {
    NiniConfig* inner_;
    std::function<void(T)> record_;

  public:
    StartupProxy(NiniConfigArena* arena, std::shared_ptr<NiniConfig> inner,
                 std::shared_ptr<NiniConfigSourceBase> source, std::function<void(T)> record)
        : NiniConfig(arena, inner->name(), source.get(), false), inner_(inner.get()),
          record_(std::move(record)) {
    }
    T managed_type() const override {
        return t(u"StartupProxy");
    }
    T get(T key) override {
        record_(key);
        return inner_->get(std::move(key));
    }
    void set(T key, std::shared_ptr<NiniConfigObject> value) override {
        inner_->set(std::move(key), std::move(value));
    }
    void remove(T key) override {
        inner_->remove(std::move(key));
    }
    std::vector<T> get_keys() const override {
        return inner_->get_keys();
    }
    std::vector<T> get_values() const override {
        return inner_->get_values();
    }
};
struct Row {
    std::vector<std::string> trace;
    std::string fault;
    int nth = 1, hit = 0;
    T dir, env;
    std::shared_ptr<NiniConfigArena> arena = std::make_shared<NiniConfigArena>(fixture_backend());
    std::shared_ptr<RegionConfigurationBackend> backend;
    std::shared_ptr<RegionConfigurationLoader> loader;
    std::shared_ptr<NiniConfigSourceBase> argv;
    T first_dll;
    bool have_first = false;
    std::map<std::u16string, T> log_literals, producer_literals;
    bool record_startup = false;
    std::string identity(const std::u16string& group, T value) {
        auto key = group + u":" + (value ? *value : u"");
        auto at = producer_literals.find(key);
        if (at != producer_literals.end())
            return at->second == value ? "same" : "different";
        producer_literals.emplace(key, value);
        return "first";
    }
    void wrap() {
        if (!record_startup)
            return;
        auto c = argv->configs()->at(t(u"Startup"));
        if (c) {
            argv->configs()->remove(c);
            auto value = std::unique_ptr<StartupProxy>(new StartupProxy(arena.get(), c, argv, [this](T key) {
                trace.push_back("key:" + enc(key) + ":" + identity(u"key", key));
            }));
            auto p = value.get();
            arena->adopt(std::move(value));
            argv->configs()->add(arena->handle(p));
        }
    }
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
        bind_native_startup_configuration_services(b);
        b.config_directory = [this] {
            event("configdir");
            return dir;
        };
        auto c = b.combine;
        b.combine = [this, c](T a, T v) {
            event("combine", {a, v, t(utf16_replacing(identity(u"combine2", v)))});
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
        b.xml_uri_source = [this](T uri) -> std::shared_ptr<NiniConfigSourceBase> {
            event("xml", {uri, t(u"Ignore")});
            throw NiniConfigError(NiniConfigError::Kind::InvalidOperation,
                                  u"Remote XML configuration is explicitly unsupported by this local host");
        };
        b.log = [this](RegionLoaderLogLevel level, T f, std::vector<T> args, RegionLoaderLogCall call) {
            static const char16_t* names[] = {u"Info", u"Warn", u"Error", u"Fatal"};
            std::string x;
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i)
                    x += ',';
                x += enc(args[i]);
            }
            auto at = log_literals.find(*f);
            auto identity = at == log_literals.end() ? u"first" : at->second == f ? u"same" : u"different";
            if (at == log_literals.end())
                log_literals.emplace(*f, f);
            event("log", {t(call == RegionLoaderLogCall::Format ? u"Format" : u"Message"),
                          t(names[int(level)]), f, t(utf16_replacing(x)), t(identity)});
        };
        b.exit = [this](int n) {
            trace.push_back("exit:" + std::to_string(n));
            throw Exit();
        };
        loader = std::make_shared<RegionConfigurationLoader>(backend);
    }
    std::string owner(std::shared_ptr<NiniConfigSourceBase> source) {
        if (source == argv)
            return "argv";
        if (loader->config() && source == loader->config()->source)
            return "main";
        if (auto ini = std::dynamic_pointer_cast<NiniIniConfigSource>(source); ini && ini->save_path())
            return "file:" + enc(ini->save_path());
        return utf8(*source->managed_type());
    }
    std::string snapshot() {
        std::string out = enc(loader->ini_file_path()) + "/";
        if (auto s = loader->config_settings())
            out += enc(s->physics_engine) + "," + enc(s->mesh_engine_name) + "," + enc(s->clientstack_dll) +
                   "," + enc(s->libraries_xml_file);
        out += '/';
        if (auto c = loader->config()) {
            if (!c->source)
                out += "nullsource";
            else
                for (auto e = c->source->configs()->get_enumerator(); e.move_next();) {
                    auto cfg = std::dynamic_pointer_cast<NiniConfig>(e.current());
                    out += enc(cfg->name()) + ':' + owner(cfg->config_source()) + '[';
                    auto keys = cfg->get_keys(), values = cfg->get_values();
                    for (std::size_t i = 0; i < keys.size(); ++i) {
                        auto value = cfg->get(keys[i]);
                        out += enc(keys[i]) + '=' + enc(values[i]) + '/' + enc(value) + '@' +
                               (value && value->empty() && value == runtime_empty_text() ? "1" : "0") + ',';
                    }
                    out += ']';
                }
        }
        out += '/';
        if (auto object = loader->network_info()) {
            auto n = std::static_pointer_cast<NetworkServersInfo>(object);
            out += std::to_string(n->HttpListenerPort) + ',' + std::to_string(n->secureInventoryServer) +
                   ',' + std::to_string(n->isSandbox) + ',' + std::to_string(n->HttpUsesSSL) + ',' +
                   enc(n->HttpSSLCN) + ',' + enc(n->HttpSSLCertPath) + ',' + enc(n->HttpSSLCNCertPass) + ',' +
                   std::to_string(n->httpSSLPort) + ',' + std::to_string(n->ssl_listener) + ',' +
                   std::to_string(n->ssl_external) + ',' + std::to_string(n->https_port) + ',' +
                   enc(n->cert_path) + ',' + enc(n->cert_pass) + ',' +
                   (n->cert_path == runtime_empty_text() ? "1" : "0");
        }
        out += '/';
        out += (have_first && loader->config_settings() &&
                        first_dll == loader->config_settings()->clientstack_dll
                    ? "1"
                    : "0");
        out += '/';
        std::istringstream pairs(utf8(*env));
        std::string pair;
        while (std::getline(pairs, pair, ',')) {
            auto sep = pair.find(':');
            if (sep == pair.npos)
                continue;
            auto name = dec(pair.substr(0, sep));
            out += enc(name) + '=' + enc(runtime_environment_get(name)) + ',';
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
        auto marker = std::u16string(u"raw-") + char16_t(0xd800);
        try {
            throw NiniConfigError(NiniConfigError::Kind::Argument, marker);
        } catch (...) {
            if (*native_configuration_exception_message(std::current_exception()) != marker)
                return 1;
        }
        RegionConfigurationBackend b;
        bind_native_startup_configuration_services(b);
        if (!b.is_http_uri(t(u"https://host/?q#f")) ||
            !b.starts_with_current(t(u"In\u00adclude-file"), t(u"Include-")) || b.xml_uri_source || b.log ||
            b.exit || b.config_directory)
            return 1;
        auto app = std::make_shared<RegionApplicationConfigurationState>();
        if (app->ini_file_path != runtime_empty_text())
            return 1;
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        auto local = std::make_shared<RegionConfigurationBackend>(
            native_local_region_configuration_backend(arena, app));
        bind_native_startup_configuration_services(*local);
        local->config_directory = [] { return t(u"."); };
        local->log = [](auto, auto, auto, auto) {};
        local->exit = [](int) { throw std::runtime_error("unit exit"); };
        local->xml_uri_source = [](T) -> std::shared_ptr<NiniConfigSourceBase> {
            throw std::runtime_error("Explicit unit XML unsupported");
        };
        RegionConfigurationLoader first(local), second(local);
        if (first.ini_file_path() != runtime_empty_text() || second.ini_file_path() != runtime_empty_text())
            return 1;
        auto retained = t(u"retained");
        app->ini_file_path = retained;
        if (first.ini_file_path() != retained || second.ini_file_path() != retained)
            return 1;
        std::cout
            << "PASS: native startup composition, typed UTF16 errors and canonical shared Application state\n";
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
        r.record_startup = mode == "K";
        std::string result = "ok";
        try {
            std::istringstream envpairs(utf8(*r.env));
            std::string envpair;
            while (std::getline(envpairs, envpair, ',')) {
                auto sep = envpair.find(':');
                if (sep != envpair.npos)
                    runtime_environment_set(dec(envpair.substr(0, sep)), dec(envpair.substr(sep + 1)));
            }
            auto source = r.arena->create_ini_source(nini_string_reader(*dec(argv)));
            r.argv = source;
            source->alias()->add_alias(t(u"On"), true);
            source->alias()->add_alias(t(u"Yes"), true);
            source->alias()->add_alias(t(u"Off"), false);
            r.wrap();
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
                if (mode == "T" || mode == "K") {
                    r.first_dll = settings->clientstack_dll;
                    r.have_first = true;
                    r.loader = std::make_shared<RegionConfigurationLoader>(r.backend);
                    r.argv = r.arena->create_ini_source(nini_string_reader(*dec(list)));
                    r.argv->alias()->add_alias(t(u"On"), true);
                    r.argv->alias()->add_alias(t(u"Yes"), true);
                    r.argv->alias()->add_alias(t(u"Off"), false);
                    r.wrap();
                    r.loader->load_config_settings(r.argv, {}, settings, network);
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
        } catch (const RuntimeEnvironmentError& e) {
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
