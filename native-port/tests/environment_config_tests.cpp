#include "opensim/framework/environment_config.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include <iostream>
#include <sstream>
#include <map>
#include <bit>
#include "opensim/framework/text.hpp"
#include <cstdlib>
using namespace opensim::framework;
namespace {
NiniWriterText t(std::u16string value) {
    return nini_writer_text(std::move(value));
}
struct Owned final : NiniConfigObject {
    int& destroyed;
    Owned(NiniConfigArena* a, int& n) : NiniConfigObject(a), destroyed(n) {
    }
    ~Owned() {
        ++destroyed;
    }
    NiniWriterText managed_type() const override {
        return t(u"Owned");
    }
};
std::string hex(const NiniWriterText& x) {
    if (!x)
        return "-";
    if (x->empty())
        return ".";
    std::string out;
    for (auto c : *x)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
NiniWriterText dec(const std::string& x) {
    if (x == "-")
        return {};
    std::u16string out;
    if (x != ".")
        for (std::size_t i = 0; i < x.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(x.substr(i, 2), nullptr, 16) |
                                         (std::stoul(x.substr(i + 2, 2), nullptr, 16) << 8));
    return t(std::move(out));
}
std::vector<std::string> split(const std::string& x, char separator) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        auto end = x.find(separator, start);
        out.push_back(x.substr(start, end == x.npos ? x.npos : end - start));
        if (end == x.npos)
            return out;
        start = end + 1;
    }
}
std::string type(NiniWriterText x) {
    if (!x)
        return "-";
    auto i = x->rfind(u'.');
    auto text = x->substr(i == x->npos ? 0 : i + 1);
    return utf8(text);
}
struct Probe {
    std::shared_ptr<NiniConfigArena> arena =
        std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    std::vector<std::shared_ptr<NiniConfigSourceBase>> masters;
    std::shared_ptr<NiniConfigSourceBase> current;
    std::shared_ptr<EnvironmentConfigBackend> backend;
    std::map<std::u16string, NiniWriterText> dictionary;
    int point = 0, nth = 1, hit = 0, mutation = 0;
    bool real = false;
    std::vector<std::string> trace;
    void event(int p, std::string text) {
        trace.push_back(std::move(text));
        if (point == p && ++hit == nth)
            throw NiniConfigError(NiniConfigError::Kind::Exception, u"fault");
    }
    void bind(std::shared_ptr<NiniConfigSourceBase> source) {
        auto id = masters.size();
        masters.push_back(source);
        source->saved.add(100 + id, [this, id](auto&, auto) { event(5, "S:" + std::to_string(id)); });
        source->configs()->config_removed.add(200 + id, [this, id](auto&, auto a) {
            event(6, "R:" + std::to_string(id) + ":" + hex(a->config->name()));
        });
        source->configs()->config_added.add(300 + id, [this, id](auto&, auto a) {
            auto c = a->config;
            event(4, "A:" + std::to_string(id) + ":" + hex(c->name()) + ":" + type(c->managed_type()));
            c->key_set.add(400 + id, [this, id](auto& c, auto a) {
                event(3, "K:" + std::to_string(id) + ":" + hex(c.name()) + ":" + hex(a->key) + ":" +
                             hex(a->value));
            });
        });
        current = source;
    }
    void mutate() {
        auto m = mutation;
        mutation = 0;
        if (m == 1) {
            auto env = current->configs()->at(t(u"Environment"));
            if (env)
                current->configs()->remove(env);
        } else if (m == 2) {
            auto c = current->configs()->add(t(u"Other"));
            c->set_text(t(u"x"), t(u"changed"));
        } else if (m == 3) {
            auto c = current->configs()->at(t(u"Environment"));
            if (c)
                c->set_text(t(u"added"), t(u"during-get"));
        } else if (m == 4)
            backend->map->append(t(u"Injected"), t(u"injected-value"));
        else if (m == 5)
            merge_environment_to_config(current, backend);
    }
    void setup(bool os) {
        real = os;
        if (real)
            backend = native_environment_config_backend();
        else {
            backend = std::make_shared<EnvironmentConfigBackend>();
            backend->map = std::make_shared<EnvironmentMap>();
            backend->get = [this](NiniWriterText key) {
                event(1, "G:" + hex(key));
                NiniWriterText value;
                if (key) {
                    auto i = dictionary.find(*key);
                    if (i != dictionary.end())
                        value = i->second;
                }
                mutate();
                return value;
            };
            backend->set = [this](NiniWriterText key, NiniWriterText value) {
                event(2, "E:" + hex(key) + ":" + hex(value));
                if (!key)
                    throw NiniConfigError(NiniConfigError::Kind::ArgumentNull,
                                          u"Value cannot be null. (Parameter 'key')", u"key");
                if (value)
                    dictionary[*key] = value;
                else
                    dictionary.erase(*key);
            };
        }
        bind(arena->create_source(t(u"ProbeSource")));
    }
    std::string snapshot() {
        std::string out = "";
        for (std::size_t i = 0; i < masters.size(); ++i) {
            out += std::to_string(i) + "{";
            auto s = masters[i];
            for (int j = 0; j < s->configs()->count(); ++j) {
                auto c = s->configs()->at(j);
                out += hex(c->name()) + ":" + type(c->managed_type()) + ":" +
                       type(c->config_source()->managed_type()) + "[";
                for (auto& key : c->get_keys())
                    out += hex(key) + "=" + hex(c->get(key)) + ";";
                out += "]";
            }
            out += "}";
        }
        out += "/M:";
        for (int i = 0; i < backend->map->count(); ++i) {
            auto e = backend->map->item(i);
            out += hex(e->name) + "=" + hex(e->value) + ";";
        }
        return out;
    }
    std::string run(const std::string& row) {
        std::istringstream in(row);
        std::string mode, ops;
        in >> mode >> point >> nth >> mutation >> ops;
        setup(mode == "R");
        std::string output;
        for (auto& token : split(ops, ',')) {
            auto a = split(token, ':');
            auto op = a[0];
            std::string result = op;
            try {
                if (op == "new")
                    bind(arena->create_source(t(u"ProbeSource")));
                else if (op == "sel")
                    current = masters.at(std::stoul(a.at(1)));
                else if (op == "null")
                    current = {};
                else if (op == "sec")
                    current->configs()->add(dec(a.at(1)));
                else if (op == "key")
                    current->configs()->at(dec(a.at(1)))->set_text(dec(a.at(2)), dec(a.at(3)));
                else if (op == "add")
                    current->configs()->at(dec(a.at(1)))->add(dec(a.at(2)), dec(a.at(3)));
                else if (op == "env") {
                    auto key = dec(a.at(1)), value = dec(a.at(2));
                    if (real)
                        native_environment_set(key, value);
                    else if (key) {
                        if (value)
                            dictionary[*key] = value;
                        else
                            dictionary.erase(*key);
                    }
                } else if (op == "get")
                    result =
                        "V:" + hex(real ? native_environment_get(dec(a.at(1))) : backend->get(dec(a.at(1))));
                else if (op == "emptyid") {
                    auto first = native_environment_get(dec(a.at(1))),
                         second = native_environment_get(dec(a.at(2)));
                    auto local = std::make_shared<EnvironmentConfigBackend>();
                    local->map = std::make_shared<EnvironmentMap>();
                    NiniWriterText fallback;
                    local->get = [](auto) { return NiniWriterText{}; };
                    local->set = [&](auto, auto value) { fallback = value; };
                    auto auxiliary = arena->create_source();
                    auxiliary->configs()->add(t(u"Environment"))->add(t(u"CHECK_MISSING"), t(u"ignored"));
                    merge_environment_to_config(auxiliary, local);
                    result = std::string("ID:") + (first && first->empty() && first == second &&
                                                           first == fallback &&
                                                           first == runtime_environment_empty_text() &&
                                                           first == native_environment_get(dec(a.at(1)))
                                                       ? "True"
                                                       : "False");
                } else if (op == "rawbytes") {
#ifndef _WIN32
                    auto bytes = [](const std::string& x) {
                        std::string o;
                        for (std::size_t i = 0; i < x.size(); i += 2)
                            o += static_cast<char>(std::stoul(x.substr(i, 2), nullptr, 16));
                        return o;
                    };
                    auto key = bytes(a.at(1)), value = bytes(a.at(2));
                    ::setenv(key.c_str(), value.c_str(), 1);
#else
                    throw std::runtime_error("raw libc probe is Unix only");
#endif
                } else if (op == "rawget") {
#ifndef _WIN32
                    auto key = utf8(*dec(a.at(1)));
                    auto value = std::getenv(key.c_str());
                    result = "V:" + hex(value ? t(utf16_replacing(value)) : NiniWriterText{});
#else
                    throw std::runtime_error("raw libc probe is Unix only");
#endif
                } else if (op == "rawset") {
#ifndef _WIN32
                    auto key = utf8(*dec(a.at(1)));
                    auto value = dec(a.at(2));
                    if (value)
                        ::setenv(key.c_str(), utf8(*value).c_str(), 1);
                    else
                        ::unsetenv(key.c_str());
#else
                    throw std::runtime_error("raw libc probe is Unix only");
#endif
                } else if (op == "merge")
                    merge_environment_to_config(current, backend);
                else if (op == "auto")
                    current->auto_save(a.at(1) == "1");
                else if (op == "ecase") {
                    auto c = current->configs()->at(t(u"Environment"));
                    std::dynamic_pointer_cast<EnvironmentConfigSource>(c->config_source())
                        ->case_sensitive(a.at(1) == "1");
                } else if (op == "cache")
                    backend->map->append(dec(a.at(1)), std::make_shared<EnvironmentItem>(
                                                           EnvironmentItem{dec(a.at(2)), dec(a.at(3))}));
                else if (op == "cv")
                    backend->map->item(std::stoi(a.at(1)))->value = dec(a.at(2));
                else if (op == "rm")
                    current->configs()->remove(current->configs()->at(dec(a.at(1))));
                else if (op == "mut")
                    mutation = std::stoi(a.at(1));
                else
                    throw std::runtime_error("operation");
            } catch (const NiniConfigError& e) {
                result = "EX:" + std::string(e.source_type()) + ":" + std::to_string(e.hresult()) + ":" +
                         hex(t(e.message)) + ":" + hex(e.parameter ? t(*e.parameter) : NiniWriterText{});
            } catch (const EnvironmentConfigError& e) {
                result = "EX:" + e.source_type + ":" +
                         std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) + ":" + hex(t(e.message)) +
                         ":" + hex(e.parameter ? t(*e.parameter) : NiniWriterText{});
            }
            if (!output.empty())
                output += '|';
            output += result + "/" + snapshot();
        }
        output += "/IO:";
        for (std::size_t i = 0; i < trace.size(); ++i) {
            if (i)
                output += ';';
            output += trace[i];
        }
        return output;
    }
};

std::string identity() {
    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    auto master = arena->create_source();
    auto config = master->configs()->add(t(u"Environment"));
    config->add(t(u"A"), t(u"ignored"));
    config->add(t(u"B"), t(u"ignored"));
    auto backend = std::make_shared<EnvironmentConfigBackend>();
    backend->map = std::make_shared<EnvironmentMap>();
    std::vector<NiniWriterText> defaults;
    backend->get = [](auto) { return NiniWriterText{}; };
    backend->set = [&](auto, auto value) { defaults.push_back(value); };
    merge_environment_to_config(master, backend);
    try {
        merge_environment_to_config(master, backend);
    } catch (const NiniConfigError&) {
    }
    if (defaults.size() != 3 || !defaults[0] || !defaults[0]->empty() || defaults[0] != defaults[1] ||
        defaults[0] != defaults[2] || defaults[0] != runtime_environment_empty_text())
        throw std::runtime_error("Original helper string.Empty identity must persist across keys and calls");
    auto source = EnvironmentConfigSource::create(arena, backend);
    source->load_env();
    auto first = source->configs()->at(0)->name();
    source->load_env();
    if (first != source->configs()->at(0)->name())
        throw std::runtime_error("Compiled Environment literal identity must persist across LoadEnv");
    return "3:True";
}
void units() {
    identity();
    auto backend = native_nini_invariant_config_backend();
    auto arena = std::make_shared<NiniConfigArena>(backend);
    int destroyed = 0;
    auto adopted = arena->adopt(std::make_unique<Owned>(arena.get(), destroyed));
    std::weak_ptr<NiniConfigArena> weak = arena;
    arena.reset();
    if (weak.expired() || destroyed)
        throw std::runtime_error("Adopted node alias must own arena");
    adopted.reset();
    if (!weak.expired() || destroyed != 1)
        throw std::runtime_error("Arena must destroy adopted node exactly once");
    arena = std::make_shared<NiniConfigArena>(backend);
    auto other = std::make_shared<NiniConfigArena>(backend);
    bool caught = false;
    try {
        arena->adopt(std::make_unique<Owned>(other.get(), destroyed));
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    if (!caught || destroyed != 2)
        throw std::runtime_error("Adopt must reject foreign arena node");
    auto master = arena->create_source();
    auto access = std::make_shared<EnvironmentConfigBackend>();
    access->map = std::make_shared<EnvironmentMap>();
    int gets = 0, sets = 0;
    access->get = [&](NiniWriterText) {
        ++gets;
        return t(u"from-env");
    };
    access->set = [&](NiniWriterText, NiniWriterText) { ++sets; };
    merge_environment_to_config(master, access);
    if (gets || sets)
        throw std::runtime_error("Absent Environment must not access environment");
    auto section = master->configs()->add(t(u"Environment"));
    section->set_text(t(u"KEY"), t(u"requested"));
    merge_environment_to_config(master, access);
    if (gets != 1 || sets || *section->get(t(u"KEY")) != u"from-env" || access->map->count() != 1)
        throw std::runtime_error("Environment merge actual typed source");
    caught = false;
    try {
        merge_environment_to_config(master, access);
    } catch (const NiniConfigError& e) {
        caught = e.kind == NiniConfigError::Kind::Argument;
    }
    if (!caught || gets != 2 || access->map->count() != 1)
        throw std::runtime_error("Process map duplicates must read then fail");
    auto empty = arena->create_source();
    auto e = empty->configs()->add(t(u"Environment"));
    e->add(t(u"OTHER"), t(u""));
    access->get = [&](NiniWriterText) { return t(u"other"); };
    merge_environment_to_config(empty, access);
    if (e->get_keys().size() != 2 || *e->get(t(u"KEY")) != u"from-env")
        throw std::runtime_error("Persistent map previous keys must enter later source");
    auto fresh = std::make_shared<EnvironmentConfigBackend>();
    fresh->map = std::make_shared<EnvironmentMap>();
    fresh->get = [](NiniWriterText) { return NiniWriterText{}; };
    fresh->set = [&](NiniWriterText key, NiniWriterText value) {
        ++sets;
        if (*key != u"missing" || !value || !value->empty())
            throw std::runtime_error("Missing environment default provider arguments");
    };
    auto changed = arena->create_source();
    auto original = changed->configs()->add(t(u"Environment"));
    original->add(t(u"missing"), t(u"old"));
    fresh->get = [&](NiniWriterText) {
        changed->configs()->remove(original);
        return NiniWriterText{};
    };
    merge_environment_to_config(changed, fresh);
    auto replacement = changed->configs()->at(t(u"Environment"));
    auto envsource = std::dynamic_pointer_cast<EnvironmentConfigSource>(replacement->config_source());
    if (!envsource || *replacement->managed_type() != u"Nini.Config.EnvConfig" ||
        *envsource->managed_type() != u"Nini.Config.EnvConfigSource")
        throw std::runtime_error("Merge must retain real EnvConfig/EnvConfigSource identity");
    envsource->case_sensitive(false);
    replacement->set_text(t(u"MISSING"), t(u"new"));
    if (*replacement->get(t(u"missing")) != u"new" || replacement->get(t(u"MISSING")))
        throw std::runtime_error("EnvConfig CaseSensitive changes Set only");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--identity") {
            std::cout << identity() << '\n';
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row)) {
                Probe probe;
                std::cout << probe.run(row) << '\n';
            }
            return 0;
        }
        units();
        std::cout << "PASS environment merge/arena ownership\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
