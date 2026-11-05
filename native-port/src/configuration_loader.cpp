// Copyright (c) Contributors, http://opensimulator.org/; BSD: native-port/LICENSE.txt.
#include "opensim/framework/configuration_loader.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
namespace opensim::framework {
namespace {
using T = NiniWriterText;
using Source = std::shared_ptr<RegionLoaderConfigSource>;
T t(std::u16string s) {
    return s.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(std::move(s));
}
T t(const char16_t* literal) {
    if (!*literal)
        return runtime_empty_text();
    static std::mutex mutex;
    static std::map<std::u16string, T> literals;
    std::lock_guard guard(mutex);
    auto at = literals.find(literal);
    if (at != literals.end())
        return at->second;
    return literals.emplace(literal, t(std::u16string(literal))).first->second;
}
T default_clientstack_literal() {
    static const auto value = t(u"OpenSim.Region.ClientStack.LindenUDP.dll");
    return value;
}
std::u16string v(const T& s) {
    return s ? *s : u"";
}
[[noreturn]] void nullref() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
bool same(const T& a, const T& b) {
    return (!a && !b) || (a && b && *a == *b);
}
bool has(const std::vector<T>& items, const T& p) {
    return std::any_of(items.begin(), items.end(), [&](const T& item) { return same(item, p); });
}
void add(std::vector<T>& items, T p) {
    if (!has(items, p))
        items.push_back(std::move(p));
}
std::shared_ptr<NiniConfigSourceBase> src(const Source& s) {
    if (!s || !s->source)
        nullref();
    return s->source;
}
bool ini_extension(T s) {
    return s && s->size() == 4 && (*s)[0] == u'.' && ((*s)[1] == u'i' || (*s)[1] == u'I') &&
           ((*s)[2] == u'n' || (*s)[2] == u'N') && ((*s)[3] == u'i' || (*s)[3] == u'I');
}
}
RegionConfigurationLoader::RegionConfigurationLoader(std::shared_ptr<RegionConfigurationBackend> b)
    : backend_(std::move(b)) {
    if (!backend_ || !backend_->application || !backend_->config_directory || !backend_->combine ||
        !backend_->full_path || !backend_->extension || !backend_->file_exists ||
        !backend_->directory_exists || !backend_->directory_files || !backend_->glob ||
        !backend_->is_http_uri || !backend_->starts_with_current || !backend_->empty_ini_source ||
        !backend_->ini_file_source || !backend_->xml_uri_source || !backend_->merge_environment ||
        !backend_->replace_key_values || !backend_->create_network_info || !backend_->load_network_info ||
        !backend_->log || !backend_->exit || !backend_->exception_message)
        throw std::invalid_argument(
            "Region configuration loader requires path, file, URI/XML, culture, Nini, environment, network, log and process providers");
}
bool RegionConfigurationLoader::is_uri(T p) {
    return backend_->is_http_uri(std::move(p));
}
void RegionConfigurationLoader::add_includes(Source config, std::vector<T>& sources) {
    auto e = src(config)->configs()->get_enumerator();
    while (e.move_next()) {
        auto c = std::dynamic_pointer_cast<NiniConfig>(e.current());
        if (!c)
            nullref();
        auto keys = c->get_keys();
        for (auto k : keys) {
            if (backend_->starts_with_current(k, t(u"Include-"))) {
                auto file = c->get_string(k);
                if (is_uri(file))
                    add(sources, file);
                else {
                    auto base = backend_->full_path(backend_->config_directory());
                    if (!file)
                        nullref();
                    auto chunk = file;
                    auto wildcard = t(u"");
                    auto at = file->find_first_of(u"*?");
                    if (at != std::u16string::npos) {
                        chunk = t(file->substr(0, at));
                        wildcard = t(file->substr(at));
                    }
                    auto path = backend_->combine(base, chunk);
                    path = t(v(backend_->full_path(path)) + v(wildcard));
                    auto paths = backend_->glob(path);
                    if (at == std::u16string::npos && paths.empty())
                        backend_->log(RegionLoaderLogLevel::Warn,
                                      t(u"[CONFIG]: Could not find include file {0}"), {path},
                                      RegionLoaderLogCall::Format);
                    else
                        for (auto p : paths)
                            add(sources, p);
                }
            }
        }
    }
}
bool RegionConfigurationLoader::read_config(Source config, T path) {
    bool success = false;
    if (!is_uri(path)) {
        backend_->log(RegionLoaderLogLevel::Info, t(u"[CONFIG]: Reading configuration file {0}"),
                      {backend_->full_path(path)}, RegionLoaderLogCall::Format);
        // C# evaluates the Merge receiver before constructing the incoming source.
        if (!config)
            nullref();
        auto receiver = config->source;
        auto incoming = backend_->ini_file_source(path);
        if (!receiver)
            nullref();
        receiver->merge(std::move(incoming));
        success = true;
    } else {
        backend_->log(RegionLoaderLogLevel::Info, t(u"[CONFIG]: {0} is a http:// URI, fetching ..."), {path},
                      RegionLoaderLogCall::Format);
        try {
            auto incoming = backend_->xml_uri_source(path);
            src(config)->merge(std::move(incoming));
            success = true;
        } catch (...) {
            auto message = backend_->exception_message(std::current_exception());
            backend_->log(RegionLoaderLogLevel::Fatal,
                          t(u"[CONFIG]: Exception reading config from URI " + v(path) + u"\n " + v(message)),
                          {}, RegionLoaderLogCall::Message);
            backend_->exit(1);
        }
    }
    return success;
}
std::shared_ptr<RegionLoaderConfigSource> RegionConfigurationLoader::load_config_settings(
    std::shared_ptr<NiniConfigSourceBase> argv, std::shared_ptr<NiniConfigSourceBase>,
    std::shared_ptr<RegionLoaderConfigSettings>& settings, std::shared_ptr<void>& network) {
    config_settings_ = settings = std::make_shared<RegionLoaderConfigSettings>();
    network_info_ = network = backend_->create_network_info();
    bool exists = false;
    if (!argv)
        nullref();
    auto startup = argv->configs()->at(t(u"Startup"));
    std::vector<T> sources;
    if (!startup)
        nullref();
    auto master = startup->get_string(t(u"inimaster"), t(u"OpenSimDefaults.ini"));
    if (same(master, t(u"none")))
        master = t(u"");
    if (is_uri(master))
        add(sources, master);
    else {
        auto path = backend_->full_path(backend_->combine(backend_->config_directory(), master));
        if (!same(master, t(u""))) {
            if (backend_->file_exists(path))
                add(sources, path);
            else {
                backend_->log(RegionLoaderLogLevel::Error, t(u"Master ini file {0} not found"),
                              {backend_->full_path(path)}, RegionLoaderLogCall::Format);
                backend_->exit(1);
            }
        }
    }
    auto ini = startup->get_string(t(u"inifile"), t(u"OpenSim.ini"));
    if (is_uri(ini)) {
        add(sources, ini);
        backend_->application->ini_file_path = ini;
    } else {
        backend_->application->ini_file_path =
            backend_->full_path(backend_->combine(backend_->config_directory(), ini));
        if (!backend_->file_exists(backend_->application->ini_file_path)) {
            ini = t(u"OpenSim.xml");
            backend_->application->ini_file_path =
                backend_->full_path(backend_->combine(backend_->config_directory(), ini));
        }
        if (backend_->file_exists(backend_->application->ini_file_path))
            add(sources, backend_->application->ini_file_path);
    }
    config_ = std::make_shared<RegionLoaderConfigSource>();
    config_->source = backend_->empty_ini_source();
    backend_->log(RegionLoaderLogLevel::Info, t(u"[CONFIG]: Reading configuration settings"), {},
                  RegionLoaderLogCall::Message);
    for (std::size_t i = 0; i < sources.size(); ++i)
        if (read_config(config_, sources[i])) {
            exists = true;
            add_includes(config_, sources);
        }
    auto dirname = startup->get_string(t(u"inidirectory"), t(u"config"));
    auto dirpath = backend_->combine(backend_->config_directory(), dirname);
    if (backend_->directory_exists(dirpath)) {
        backend_->log(RegionLoaderLogLevel::Info, t(u"[CONFIG]: Searching folder {0} for config ini files"),
                      {dirpath}, RegionLoaderLogCall::Format);
        std::vector<T> overrides;
        auto files = backend_->directory_files(dirname);
        for (auto path : files)
            if (ini_extension(backend_->extension(path))) {
                if (!has(sources, backend_->full_path(path))) {
                    overrides.push_back(backend_->full_path(path));
                    sources.push_back(backend_->full_path(path));
                }
            }
        if (!overrides.empty()) {
            auto over = std::make_shared<RegionLoaderConfigSource>();
            over->source = backend_->empty_ini_source();
            for (std::size_t i = 0; i < overrides.size(); ++i)
                if (read_config(over, overrides[i])) {
                    exists = true;
                    add_includes(over, overrides);
                }
            auto receiver = src(config_);
            receiver->merge(src(over));
        }
    }
    if (sources.empty()) {
        backend_->log(RegionLoaderLogLevel::Fatal, t(u"[CONFIG]: Could not load any configuration"), {},
                      RegionLoaderLogCall::Format);
        backend_->exit(1);
    } else if (!exists) {
        backend_->log(RegionLoaderLogLevel::Fatal, t(u"[CONFIG]: Could not load any configuration"), {},
                      RegionLoaderLogCall::Format);
        backend_->log(RegionLoaderLogLevel::Fatal,
                      t(u"[CONFIG]: Configuration exists, but there was an error loading it!"), {},
                      RegionLoaderLogCall::Format);
        backend_->exit(1);
    }
    backend_->log(RegionLoaderLogLevel::Info, t(u"[CONFIG]: Loading environment variables for Config"), {},
                  RegionLoaderLogCall::Message);
    if (!config_)
        nullref();
    backend_->merge_environment(config_->source);
    auto receiver = src(config_);
    receiver->merge(argv);
    backend_->replace_key_values(src(config_));
    read_config_settings();
    return config_;
}
void RegionConfigurationLoader::read_config_settings() {
    auto startup = src(config_)->configs()->at(t(u"Startup"));
    if (startup) {
        auto target = config_settings_;
        auto value = startup->get_string(t(u"physics"));
        if (!target)
            nullref();
        target->physics_engine = value;
        target = config_settings_;
        value = startup->get_string(t(u"meshing"));
        if (!target)
            nullref();
        target->mesh_engine_name = value;
        target = config_settings_;
        value = startup->get_string(t(u"clientstack_plugin"), default_clientstack_literal());
        if (!target)
            nullref();
        target->clientstack_dll = value;
    }
    auto receiver = network_info_;
    if (!config_)
        nullref();
    auto source = config_->source;
    if (!receiver)
        nullref();
    backend_->load_network_info(receiver, source);
}
RegionConfigurationBackend
native_local_region_configuration_backend(std::shared_ptr<NiniConfigArena> arena,
                                          std::shared_ptr<RegionApplicationConfigurationState> application,
                                          std::shared_ptr<DirectoryEntriesBackend> directories) {
    if (!arena || !application)
        throw std::invalid_argument(
            "Native local region loader requires Nini arena and shared application state");
    if (!directories)
        directories = native_directory_entries_backend();
    auto globbackend = native_glob_backend(directories);
    RegionConfigurationBackend b;
    b.application = std::move(application);
    b.combine = [combine = globbackend.combine](T a, T c) {
        if (!a)
            throw NiniConfigError(NiniConfigError::Kind::ArgumentNull,
                                  u"Value cannot be null. (Parameter 'path1')", u"path1");
        if (!c)
            throw NiniConfigError(NiniConfigError::Kind::ArgumentNull,
                                  u"Value cannot be null. (Parameter 'path2')", u"path2");
        return t(combine(*a, *c));
    };
    b.full_path = directories->full_path;
    bool windows = directories->platform == DirectoryEntriesPlatform::Windows;
    b.extension = [windows](T p) -> T {
        if (!p)
            return {};
        for (auto n = p->size(); n > 0;) {
            auto ch = (*p)[--n];
            if (ch == u'.')
                return t(n + 1 == p->size() ? u"" : p->substr(n));
            if (ch == u'/' || (windows && ch == u'\\'))
                break;
        }
        return t(u"");
    };
    auto status = [](T p, bool directory) {
        if (!p || p->empty() || p->find(u'\0') != p->npos)
            return false;
        try {
#ifdef _WIN32
            std::filesystem::path path(*p);
#else
            std::filesystem::path path(utf8(*p));
#endif
            std::error_code ec;
            auto st = std::filesystem::status(path, ec);
            return !ec && (directory ? std::filesystem::is_directory(st)
                                     : std::filesystem::exists(st) && !std::filesystem::is_directory(st));
        } catch (...) {
            return false;
        }
    };
    b.file_exists = [status](T p) { return status(p, false); };
    b.directory_exists = [status](T p) { return status(p, true); };
    b.directory_files = [directories](T p) {
        return directory_get_files(p, file_system_name::star_literal(), directories);
    };
    b.glob = [globbackend](T p) {
        auto paths = framework::glob(p ? std::optional<std::u16string>(*p) : std::nullopt, globbackend);
        std::vector<T> out;
        for (auto& path : paths)
            out.push_back(t(std::move(path)));
        return out;
    };
    b.empty_ini_source = [arena] { return arena->create_ini_source(); };
    b.ini_file_source = [arena](T p) { return arena->create_ini_source(p); };
    return b;
}
}
