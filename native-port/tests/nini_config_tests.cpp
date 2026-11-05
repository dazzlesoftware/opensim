// Actual bundled Nini configuration method probe. Nini MIT license.
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
#include <filesystem>
#include <fstream>
#include <bit>
#include <charconv>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>
#include <limits>
using namespace opensim::framework;
using Text = NiniWriterText;
Text t(std::u16string v) {
    return std::make_shared<const std::u16string>(std::move(v));
}
Text decode(const std::string& s) {
    if (s == "null")
        return {};
    std::u16string v;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return t(v);
}
std::string enc(const Text& p) {
    if (!p)
        return "null";
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (auto c : *p)
        o << std::setw(4) << unsigned(c);
    auto s = o.str();
    return s.empty() ? "-" : s;
}
std::string err(const NiniConfigError& e) {
    return std::string(e.source_type()) + ":" + enc(t(e.message)) + ":" + std::to_string(e.hresult()) + ":" +
           (e.parameter ? enc(t(*e.parameter)) : "null");
}
std::string err(const NiniDocumentError& e) {
    return std::string(e.source_type()) + ":" + enc(t(e.message)) + ":" +
           std::to_string(std::uint32_t(e.hresult())) + ":" + (e.parameter ? enc(t(*e.parameter)) : "null");
}

std::u16string lower(std::u16string v) {
    for (auto& c : v)
        if (c >= u'A' && c <= u'Z')
            c += 32;
    return v;
}
struct Sign {
    std::u16string negative = u"-";
};
std::u16string number_text(const Text& p) {
    return p ? *p : u"";
}
template <class I> I integer(Text input, const Sign& sign) {
    if (!input)
        return 0;
    auto v = *input;
    auto white = [](char16_t c) { return c == 32 || (c >= 9 && c <= 13); };
    while (!v.empty() && white(v.front()))
        v.erase(v.begin());
    bool negative = false;
    if (!sign.negative.empty() && v.starts_with(sign.negative)) {
        negative = true;
        v.erase(0, sign.negative.size());
    } else if (v.starts_with(u"+"))
        v.erase(0, 1);
    std::size_t end = 0;
    while (end < v.size() && v[end] >= u'0' && v[end] <= u'9')
        ++end;
    auto digits = v.substr(0, end);
    while (end < v.size() && white(v[end]))
        ++end;
    while (end < v.size() && v[end] == 0)
        ++end;
    if (digits.empty() || end != v.size())
        throw NiniConfigError(NiniConfigError::Kind::Format,
                              u"The input string '" + *input + u"' was not in a correct format.");
    std::uint64_t n = 0;
    auto limit = std::uint64_t(std::numeric_limits<I>::max()) + (negative ? 1 : 0);
    bool overflow = false;
    for (auto c : digits) {
        auto d = unsigned(c - u'0');
        if (n > (limit - d) / 10)
            overflow = true;
        else if (!overflow)
            n = n * 10 + d;
    }
    if (overflow)
        throw NiniConfigError(NiniConfigError::Kind::Overflow,
                              sizeof(I) == 4 ? u"Value was either too large or too small for an Int32."
                                             : u"Value was either too large or too small for an Int64.");
    return std::bit_cast<I>(negative ? std::make_unsigned_t<I>(0) - std::make_unsigned_t<I>(n)
                                     : std::make_unsigned_t<I>(n));
}
template <class F> F real(Text p) {
    if (!p)
        return 0;
    std::string v;
    for (auto c : *p)
        if (c != u',')
            v += char(c);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
        v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
        v.pop_back();
    F out{};
    auto r = std::from_chars(v.data(), v.data() + v.size(), out);
    if (r.ec != std::errc{} || r.ptr != v.data() + v.size())
        throw NiniConfigError(NiniConfigError::Kind::Format,
                              u"The input string '" + *p + u"' was not in a correct format.");
    return out;
}
struct Row {
    std::shared_ptr<Sign> sign = std::make_shared<Sign>();
    std::shared_ptr<NiniConfigBackend> backend = std::make_shared<NiniConfigBackend>();
    std::shared_ptr<NiniConfigArena> arena;
    std::vector<std::shared_ptr<NiniConfigSourceBase>> sources{8};
    std::map<int, std::shared_ptr<NiniConfig>> configs;
    std::map<int, std::shared_ptr<NiniConfigObject>> objects;
    std::map<int, std::shared_ptr<int>> calls;
    std::map<NiniConfig*, int> ids;
    int next_id = 1000;
    std::map<int, NiniConfigCollection::Enumerator> enumerators;
    std::vector<std::string> events, results;
    Row() {
        backend->capture_number_format = [this] {
            auto captured = sign;
            auto f = std::make_shared<NiniNumberFormat>();
            f->to_int32 = [captured](auto p) { return integer<std::int32_t>(p, *captured); };
            f->to_int64 = [captured](auto p) { return integer<std::int64_t>(p, *captured); };
            f->to_single = [](auto p) { return real<float>(p); };
            f->to_double = [](auto p) { return real<double>(p); };
            return f;
        };
        backend->insensitive_table = [] {
            auto c = std::make_shared<NiniInsensitiveTable>();
            c->equal = [](auto a, auto b) { return lower(*a) == lower(*b); };
            return c;
        };
        backend->lower_current = [](auto v) { return t(lower(*v)); };
        backend->index_of_current = [](auto text, auto pattern, int start) {
            auto p = text->find(*pattern, std::size_t(start));
            return p == text->npos ? -1 : static_cast<int>(p);
        };
        backend->document = native_nini_document_factory();
        backend->file_reader = backend->document.file_reader;
        backend->stream_reader = backend->document.stream_reader;
        arena = std::make_shared<NiniConfigArena>(backend);
    }
    int id(const std::shared_ptr<NiniConfig>& c) {
        if (!c)
            return -1;
        auto it = ids.find(c.get());
        if (it != ids.end())
            return it->second;
        auto n = next_id++;
        ids[c.get()] = n;
        configs[n] = c;
        return n;
    }
    void bind(int n, std::shared_ptr<NiniConfig> c) {
        configs[n] = c;
        if (c)
            ids[c.get()] = n;
    }
    void source(int n, bool ini) {
        sources.at(n) = ini ? std::static_pointer_cast<NiniConfigSourceBase>(arena->create_ini_source())
                            : arena->create_source(t(u"BaseSource"));
        auto s = sources[n];
        s->saved.add(1, [this, n](auto&, auto) { events.push_back("s" + std::to_string(n)); });
        s->reloaded.add(1, [this, n](auto&, auto) { events.push_back("r" + std::to_string(n)); });
        s->configs()->config_added.add(1, [this, n](auto&, auto e) {
            events.push_back("a" + std::to_string(n) + ":" + std::to_string(id(e->config)));
        });
        s->configs()->config_removed.add(1, [this, n](auto&, auto e) {
            events.push_back("d" + std::to_string(n) + ":" + std::to_string(id(e->config)));
        });
    }
    std::shared_ptr<NiniIniConfigSource> ini(int n) {
        return std::dynamic_pointer_cast<NiniIniConfigSource>(sources.at(n));
    }
    void command(const std::string& cmd) {
        std::istringstream in(cmd);
        std::string op, a, b;
        int n, m, k;
        in >> op;
        if (op == "S") {
            in >> n >> m;
            source(n, m != 0);
        } else if (op == "C") {
            in >> n >> m >> a >> k;
            bind(n, arena->create_config(decode(a), m < 0 ? nullptr : sources.at(m), k));
        } else if (op == "AN") {
            in >> n >> m >> a;
            bind(m, sources.at(n)->add_config(decode(a)));
        } else if (op == "A") {
            in >> n >> m;
            sources.at(n)->configs()->add(m < 0 ? nullptr : configs.at(m));
        } else if (op == "LOAD") {
            in >> n >> a;
            ini(n)->load(nini_string_reader(*decode(a)));
        } else if (op == "FILE") {
            in >> a >> b;
            auto path = decode(a), data = decode(b);
            std::ofstream file(std::filesystem::path(utf8(*path)), std::ios::binary | std::ios::trunc);
            file << utf8(*data);
            if (!file)
                throw std::runtime_error("fixture file write");
        } else if (op == "LP") {
            in >> n >> a;
            ini(n)->load(decode(a));
        } else if (op == "SP") {
            in >> n >> a;
            ini(n)->save(decode(a));
        } else if (op == "DOCNULL") {
            in >> n;
            ini(n)->load(std::shared_ptr<NiniDocument>{});
        } else if (op == "ADD" || op == "ST") {
            in >> n >> a >> b;
            if (op == "ADD")
                configs.at(n)->add(decode(a), decode(b));
            else
                configs.at(n)->set_text(decode(a), decode(b));
        } else if (op == "BOX") {
            in >> n >> a >> b;
            auto x = decode(a), y = decode(b);
            calls[n] = std::make_shared<int>();
            auto count = calls[n];
            objects[n] = arena->object(t(u"Box"), [count, x, y] { return ++*count % 2 ? x : y; });
        } else if (op == "SET") {
            in >> n >> a >> m;
            configs.at(n)->set(decode(a), m < 0 ? nullptr : objects.at(m));
        } else if (op == "BC") {
            in >> n;
            results.push_back(std::to_string(*calls.at(n)));
        } else if (op == "NAME") {
            in >> n >> a;
            configs.at(n)->name(decode(a));
        } else if (op == "REM") {
            in >> n >> a;
            configs.at(n)->remove(decode(a));
        } else if (op == "CLEAR") {
            in >> n;
            sources.at(n)->configs()->clear();
        } else if (op == "REMOVE") {
            in >> n >> m;
            sources.at(n)->configs()->remove(m < 0 ? nullptr : configs.at(m));
        } else if (op == "REMOVEAT") {
            in >> n >> m;
            sources.at(n)->configs()->remove_at(m);
        } else if (op == "AS") {
            in >> n >> m;
            sources.at(n)->auto_save(m);
        } else if (op == "CS") {
            in >> n >> m;
            ini(n)->case_sensitive(m);
        } else if (op == "MERGE") {
            in >> n >> m;
            sources.at(n)->merge(m < 0 ? nullptr : sources.at(m));
        } else if (op == "SAVE") {
            in >> n;
            sources.at(n)->save();
        } else if (op == "RELOAD") {
            in >> n;
            sources.at(n)->reload();
        } else if (op == "TEXT") {
            in >> n;
            results.push_back(enc(sources.at(n)->to_string()));
        } else if (op == "SW") {
            in >> n;
            auto writer = std::make_shared<NiniStringWriter>();
            ini(n)->save(writer);
            results.push_back(enc(writer->to_string()));
        } else if (op == "EXP") {
            in >> n >> m >> a;
            results.push_back(enc(sources.at(n)->get_expanded(configs.at(m), decode(a))));
        } else if (op == "ALL") {
            in >> n;
            sources.at(n)->expand_key_values();
        } else if (op == "CULT") {
            in >> a;
            sign = std::make_shared<Sign>();
            sign->negative = *decode(a);
        } else if (op == "AE") {
            in >> n >> a >> m;
            std::shared_ptr<NiniEnumReflection> reflection;
            if (m >= 0) {
                reflection = std::make_shared<NiniEnumReflection>();
                reflection->names = [] {
                    return std::vector<NiniWriterText>{t(u"Zero"), t(u"One"), t(u"Duplicate"), t(u"Minus")};
                };
                reflection->int_values = [m] {
                    if (m)
                        throw NiniConfigError(
                            NiniConfigError::Kind::InvalidCast,
                            m == 1
                                ? u"Unable to cast object of type 'ByteAliases[]' to type 'System.Int32[]'."
                                : u"Unable to cast object of type 'LongAliases[]' to type 'System.Int32[]'.");
                    return std::vector<std::int32_t>{0, 1, 1, -1};
                };
            }
            (n < 0 ? sources.at(-n - 1)->alias() : configs.at(n)->alias())->add_alias(decode(a), reflection);
        } else if (op == "AB") {
            in >> n >> a >> m;
            (n < 0 ? sources.at(-n - 1)->alias() : configs.at(n)->alias())->add_alias(decode(a), m != 0);
        } else if (op == "AI") {
            in >> n >> a >> b >> m;
            (n < 0 ? sources.at(-n - 1)->alias() : configs.at(n)->alias())
                ->add_alias(decode(a), decode(b), m);
        } else if (op == "GET") {
            in >> n >> a >> m >> k;
            auto c = configs.at(n);
            auto key = decode(a);
            switch (m) {
            case 0:
                results.push_back(enc(c->get(key)));
                break;
            case 1:
                results.push_back(std::to_string(c->get_int(key)));
                break;
            case 2:
                results.push_back(std::to_string(c->get_int(key, k)));
                break;
            case 3:
                results.push_back(std::to_string(c->get_int(key, true)));
                break;
            case 4:
                results.push_back(std::to_string(c->get_int(key, k, true)));
                break;
            case 5:
                results.push_back(std::to_string(c->get_long(key)));
                break;
            case 6:
                results.push_back(std::to_string(c->get_boolean(key)));
                break;
            case 7:
                results.push_back(std::to_string(c->get_boolean(key, k != 0)));
                break;
            case 8:
                results.push_back(std::to_string(std::bit_cast<std::uint32_t>(c->get_float(key))));
                break;
            case 9:
                results.push_back(std::to_string(std::bit_cast<std::uint64_t>(c->get_double(key))));
                break;
            case 10:
                results.push_back(std::to_string(c->contains(key)));
                break;
            case 11:
                results.push_back(enc(c->get(key, t(u"fallback"))));
                break;
            case 12:
                results.push_back(std::to_string(c->get_long(key, k)));
                break;
            case 13:
                results.push_back(std::to_string(std::bit_cast<std::uint32_t>(c->get_float(key, float(k)))));
                break;
            case 14:
                results.push_back(
                    std::to_string(std::bit_cast<std::uint64_t>(c->get_double(key, double(k)))));
                break;
            case 15:
                results.push_back(std::to_string(c->get_int(key, false)));
                break;
            case 16:
                results.push_back(std::to_string(c->get_int(key, k, false)));
                break;
            }
        } else if (op == "KS" || op == "KR") {
            in >> n >> m >> k;
            auto fn = [this, n, m, k, op](NiniConfig& c, auto e) {
                events.push_back(op + std::to_string(n) + ":" + enc(e->key) + ":" + enc(e->value));
                if (k == 1)
                    throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"event fault");
                if (k == 2) {
                    c.key_set.remove(m);
                    c.set_text(t(u"nested"), t(u"x"));
                }
            };
            if (op == "KS")
                configs.at(n)->key_set.add(m, fn);
            else
                configs.at(n)->key_removed.add(m, fn);
        } else if (op == "OFF") {
            in >> n >> m >> k;
            if (k)
                configs.at(n)->key_removed.remove(m);
            else
                configs.at(n)->key_set.remove(m);
        } else if (op == "EN") {
            in >> n >> m;
            enumerators.insert_or_assign(m, sources.at(n)->configs()->get_enumerator());
        } else if (op == "NEXT") {
            in >> n;
            results.push_back(enumerators.at(n).move_next() ? "1" : "0");
        } else if (op == "RESET") {
            in >> n;
            enumerators.at(n).reset();
        } else if (op == "CURRENT") {
            in >> n;
            auto o = enumerators.at(n).current();
            auto c = std::dynamic_pointer_cast<NiniConfig>(o);
            results.push_back(c ? std::to_string(id(c)) : (o ? "obj" : "null"));
        } else if (op == "COPYNULL") {
            in >> n >> m;
            sources.at(n)->configs()->copy_to(nullptr, m);
        } else if (op == "LADD") {
            in >> n >> m;
            results.push_back(
                std::to_string(sources.at(n)->configs()->list_add(m < 0 ? nullptr : objects.at(m))));
        } else if (op == "INSERT") {
            in >> n >> m >> k;
            sources.at(n)->configs()->insert(m, k < 0 ? nullptr : objects.at(k));
        } else if (op == "COLSET") {
            in >> n >> m >> k;
            sources.at(n)->configs()->list_set(m, k < 0 ? nullptr : objects.at(k));
        } else if (op == "COLGET") {
            in >> n >> m;
            results.push_back(std::to_string(id(sources.at(n)->configs()->at(m))));
        } else
            throw std::runtime_error("Unknown Nini configuration probe operation: " + op);
    }
    std::string snapshot() {
        std::ostringstream out;
        for (std::size_t n = 0; n < sources.size(); ++n)
            if (auto s = sources[n]) {
                out << "S" << n << ":" << s->auto_save() << ":";
                if (auto i = ini(n))
                    out << i->case_sensitive() << ":" << enc(i->save_path());
                out << "[";
                auto c = s->configs();
                for (int j = 0; j < c->count(); ++j) {
                    auto obj = c->list_at(j);
                    auto cfg = std::dynamic_pointer_cast<NiniConfig>(obj);
                    out << (cfg ? std::to_string(id(cfg)) : (obj ? "obj" : "null")) << ",";
                }
                out << "]";
            }
        for (auto& [n, c] : configs)
            if (c) {
                out << "C" << n << ":" << enc(c->managed_type()) << ":" << enc(c->name()) << ":";
                auto src = c->config_source();
                int sid = -1;
                for (int i = 0; i < int(sources.size()); ++i)
                    if (sources[i] == src)
                        sid = i;
                out << sid << "[";
                auto keys = c->get_keys(), values = c->get_values();
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    out << enc(keys[i]) << "=" << enc(values[i]) << "/";
                    try {
                        out << enc(c->get(keys[i]));
                    } catch (const NiniConfigError& e) {
                        out << err(e);
                    }
                    out << ",";
                }
                out << "]";
            }
        for (auto& [n, c] : calls)
            out << "B" << n << ":" << *c;
        return out.str();
    }
};
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::ios::sync_with_stdio(false);
            std::cin.tie(nullptr);
            std::string line;
            while (std::getline(std::cin, line)) {
                Row row;
                std::string error = "ok";
                std::istringstream parts(line);
                std::string command;
                while (std::getline(parts, command, '|')) {
                    try {
                        row.command(command);
                    } catch (const NiniConfigError& e) {
                        error = err(e);
                        break;
                    } catch (const NiniDocumentError& e) {
                        error = err(e);
                        break;
                    } catch (const NiniStreamError& e) {
                        error = e.source_type + ":" + enc(t(e.message)) + ":" +
                                std::to_string(std::uint32_t(e.hresult)) + ":" +
                                (e.parameter ? enc(t(*e.parameter)) : "null");
                        break;
                    } catch (const NiniReaderError& e) {
                        error = std::string(e.source_type()) + ":" + enc(t(e.message_utf16())) + ":" +
                                std::to_string(std::uint32_t(e.hresult())) + ":null";
                        break;
                    }
                }
                std::cout << error << "/";
                for (auto& s : row.results)
                    std::cout << s << ";";
                std::cout << "/" << row.snapshot() << "/";
                for (auto& s : row.events)
                    std::cout << s << ";";
                std::cout << '\n';
            }
            return 0;
        }
        Row r;
        r.source(0, false);
        r.bind(0, r.sources[0]->add_config(t(u"s")));
        r.configs[0]->add(t(u"k"), t(u"old"));
        r.configs[0]->set_text(t(u"k"), t(u"new"));
        if (*r.configs[0]->get(t(u"k")) != u"new" || *r.configs[0]->get_values()[0] != u"old")
            throw std::runtime_error("OrderedList stale Values quirk");
        auto only = r.configs[0];
        std::weak_ptr<NiniConfigArena> owner = r.arena;
        r.sources.clear();
        r.configs.clear();
        r.arena.reset();
        if (owner.expired() || !only->config_source())
            throw std::runtime_error("Config did not retain source arena");
        only.reset();
        if (!owner.expired())
            throw std::runtime_error("Intrinsic config/source graph leaked arena");
        std::cout << "PASS: Nini config ordered state and source-reference ownership\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
