#include "opensim/services/simulator_features.hpp"
#include "opensim/server/http_route_registry.hpp"
#include <iostream>
#include <sstream>
#include "opensim/framework/nini_config_invariant.hpp"
using namespace opensim;
using namespace services;
std::string hex(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string hex(const server::PollBytes& value) {
    return value ? hex(std::string_view(reinterpret_cast<const char*>(value->data()), value->size())) : "-";
}
std::string dump(const FeatureData& value) {
    if (!value)
        return "-";
    try {
        return hex(simulator_feature_xml(value));
    } catch (...) {
        return "EX";
    }
}
namespace opensim::services {
struct SimulatorFeaturesProbe {
    static FeatureData root(SimulatorFeaturesModule& m) {
        return m.features_;
    }
    static std::string flags(SimulatorFeaturesModule& m) {
        return std::to_string(m.export_) + std::to_string(m.do_syntax_);
    }
};
}
struct Context {
    int profile = 0, reads = 0, random = 0;
    std::string failure, trace;
    void log(const std::string& text) {
        trace += text + ";";
        if (text == failure)
            throw std::runtime_error(text);
    }
    std::optional<std::string> text() const {
        switch ((profile % 8 + profile / 104) % 8) {
        case 0:
            return {};
        case 1:
            return "";
        case 2:
            return "<&\"'>";
        case 3:
            return "https://example/[USERID]/[USERID]";
        case 4:
            return "世界😀";
        case 5:
            return std::string("a\0b", 3);
        case 6:
            return "plain";
        default:
            return "a\r\nb";
        }
    }
    std::optional<std::string> key() const {
        switch ((profile / 13) % 8) {
        case 0:
            return "Test";
        case 1:
            return "OpenSimExtras";
        case 2:
            return "destination-guide-url";
        case 3:
            return "bad&key";
        case 4:
            return "";
        case 5:
            return {};
        case 6:
            return "世界";
        default:
            return std::string("nul\0key", 7);
        }
    }
};
FeatureData make_value(Context& c) {
    using V = network::Value;
    switch (c.profile % 13) {
    case 0:
        return {};
    case 1:
        return SimulatorFeatureValue::from_value(V{});
    case 2:
        return SimulatorFeatureValue::from_value(V(true));
    case 3:
        return SimulatorFeatureValue::from_value(V(int32_t(-1)));
    case 4:
        return SimulatorFeatureValue::from_value(V(c.text().value_or("")));
    case 5: {
        auto v = SimulatorFeatureValue::object();
        v->map["nested"] = SimulatorFeatureValue::from_value(V("original"));
        return v;
    }
    case 6: {
        auto v = SimulatorFeatureValue::list();
        v->array = {SimulatorFeatureValue::from_value(V(true)), SimulatorFeatureValue::from_value(V("text"))};
        return v;
    }
    case 7: {
        V v;
        v.kind = V::Kind::Real;
        v.real = 1. / 3.;
        return SimulatorFeatureValue::from_value(v);
    }
    case 8: {
        V v("01234567-89ab-cdef-0123-456789abcdef");
        v.kind = V::Kind::Uuid;
        return SimulatorFeatureValue::from_value(v);
    }
    case 9:
        return SimulatorFeatureValue::from_value(V::long_integer(-12345678901234));
    case 10: {
        auto bytes = std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{0, 128, 255});
        return SimulatorFeatureValue::from_value(network::osd_from_binary(bytes));
    }
    case 11: {
        V v("https://example/a?x=1&y=2");
        v.kind = V::Kind::Uri;
        return SimulatorFeatureValue::from_value(v);
    }
    default: {
        V v("2000-01-01T00:00:00Z");
        v.kind = V::Kind::Date;
        return SimulatorFeatureValue::from_value(v);
    }
    }
}
std::string syntax_text(int p) {
    switch (p % 6) {
    case 0:
        return "";
    case 1:
        return " 01234567-89ab-cdef-0123-456789abcdef \r\n <!--skip--> \n<map>\n \t \n<string>syntax</string>\r</map>\n";
    case 2:
        return "invalid\npayload";
    case 3:
        return "";
    case 4:
        return "00000000-0000-0000-0000-000000000000\nzero";
    default:
        return "01234567-89ab-cdef-0123-456789abcdef\n";
    }
}
struct Reader : SimulatorSyntaxReader {
    Context& c;
    std::string text;
    size_t at = 0;
    Reader(Context& x) : c(x), text(syntax_text(x.profile / 4)) {
    }
    std::optional<std::string> read_line() override {
        c.log("read" + std::to_string(c.reads++));
        if (at == text.size())
            return {};
        auto end = text.find_first_of("\r\n", at);
        if (end == text.npos)
            end = text.size();
        auto result = text.substr(at, end - at);
        at = end;
        if (at < text.size()) {
            char v = text[at++];
            if (v == '\r' && at < text.size() && text[at] == '\n')
                ++at;
        }
        return result;
    }
    void dispose() override {
        c.log("dispose");
    }
};
struct Backend : SimulatorFeaturesBackend {
    Context& c;
    explicit Backend(Context& v) : c(v) {
    }
    bool syntax_exists() override {
        if (c.profile >= 1000)
            return SimulatorFeaturesBackend::syntax_exists();
        c.log("exists");
        return (c.profile / 4) % 6 != 0;
    }
    std::shared_ptr<SimulatorSyntaxReader> open_syntax() override {
        if (c.profile >= 1000)
            return SimulatorFeaturesBackend::open_syntax();
        c.log("open");
        c.reads = 0;
        return std::make_shared<Reader>(c);
    }
    framework::Uuid random_uuid() override {
        c.log("random");
        auto digits = std::to_string(++c.random);
        auto id = std::string("00000000-0000-0000-0000-") + std::string(12 - digits.size(), '0') + digits;
        return framework::Uuid(id);
    }
    void error(const std::string& text) override {
        c.log("error:" + hex(text));
        if (c.failure == "log")
            throw std::runtime_error("log");
    }
    void warn(const std::string& text) override {
        c.log("warn:" + hex(text));
    }
};
struct GridInfo : SimulatorGridInfo {
    Context& c;
    explicit GridInfo(Context& x) : c(x) {
    }
    void set(const std::string& key, const std::optional<std::string>& value) override {
        c.log("grid:" + key + ":" + (value ? hex(*value) : "-"));
        if (c.failure == "grid")
            throw std::runtime_error("grid");
    }
    void aliases(std::vector<std::string> values) override {
        std::string out = "aliases:";
        for (auto& v : values)
            out += hex(v) + ",";
        c.log(out);
        if (c.failure == "grid")
            throw std::runtime_error("grid");
    }
};
struct Scene : SimulatorFeaturesScene {
    Context& c;
    std::string label;
    SceneCapabilityEvents ev;
    int registered = 0;
    explicit Scene(Context& x, std::string l)
        : c(x), label(std::move(l)), ev([&](const auto&, const auto&) { c.log("event-error"); }) {
    }
    SceneCapabilityEvents& events() override {
        return ev;
    }
    void register_module(SimulatorFeaturesModule*) override {
        c.log("register:" + label);
        ++registered;
    }
    std::shared_ptr<SimulatorGridInfo> grid_info() override {
        c.log("grid-info:" + label);
        return std::make_shared<GridInfo>(c);
    }
    std::shared_ptr<SimulatorGridExtras> extra_features() override {
        c.log("extras:" + label);
        auto fields = std::make_shared<SimulatorGridExtras>();
        switch (c.profile % 10) {
        case 1:
            for (auto [key, value] : std::initializer_list<std::pair<const char*, const char*>>{
                     {"GridName", "grid"},
                     {"GridNick", "nick"},
                     {"GridURL", "uri"},
                     {"GridURLAlias", "one,,two"},
                     {"search-server-url", "search"},
                     {"destination-guide-url", "guide"},
                     {"currency-base-uri", "ignored"},
                     {"Custom", "text"}})
                (*fields)[key] = std::string(value);
            break;
        case 2:
            (*fields)["Result"] = std::string("Failure");
            break;
        case 3:
            (*fields)["Result"] = std::string("Success");
            (*fields)["ExportSupported"] = std::string(" TrUe ");
            break;
        case 4:
            (*fields)["Result"] = nullptr;
            break;
        case 5:
            (*fields)["Custom"] = int32_t(12);
            break;
        case 6:
            (*fields)["Custom"] = std::string("kept");
            (*fields)["GridURLAlias"] = nullptr;
            break;
        case 7:
            (*fields)["ExportSupported"] = std::string("nonsense");
            break;
        case 8:
            (*fields)["Result"] = server::PollStringObject{[&] {
                c.log("to-string");
                return std::optional<std::string>("Failure");
            }};
            break;
        case 9:
            (*fields)["ExportSupported"] = std::string(" true\0 ", 7);
            break;
        }
        return fields;
    }
};
std::u16string cfg_raw(const std::string& text) {
    std::u16string out;
    if (text != "-")
        for (std::size_t i = 0; i < text.size(); i += 4)
            out += static_cast<char16_t>(std::stoi(text.substr(i, 4), nullptr, 16));
    return out;
}
std::string cfg_hex(const std::u16string& text) {
    std::string out;
    for (auto c : text)
        for (int n : {12, 8, 4, 0})
            out += "0123456789abcdef"[(c >> n) & 15];
    return out.empty() ? "-" : out;
}
std::string cfg_error(const std::exception& e) {
    auto show = [](const char* type, std::uint32_t code, const std::optional<std::u16string>& p,
                   const std::u16string& message) {
        return std::string("E:") + type + ":" + std::to_string(code) + ":" + (p ? cfg_hex(*p) : "~") + ":" +
               cfg_hex(message);
    };
    if (auto p = dynamic_cast<const framework::NiniConfigError*>(&e))
        return show(p->source_type(), p->hresult(), p->parameter, p->message);
    if (auto p = dynamic_cast<const framework::NiniDocumentError*>(&e))
        return show(p->source_type(), p->hresult(), p->parameter, p->message);
    if (auto p = dynamic_cast<const framework::NiniReaderError*>(&e))
        return show(p->source_type(), p->hresult(), {}, p->message_utf16());
    if (dynamic_cast<const EventQueueNullReference*>(&e))
        return show("System.NullReferenceException", 0x80004003u, {},
                    u"Object reference not set to an instance of an object.");
    return "X:" + hex(e.what());
}
std::string cfg_flow(const std::string& line) {
    Context c;
    auto backend = std::make_shared<Backend>(c);
    auto module =
        std::make_shared<SimulatorFeaturesModule>(backend, std::make_shared<SimulatorSyntaxState>());
    std::istringstream in(line);
    int aliases;
    in >> aliases;
    std::string result;
    for (std::string encoded; in >> encoded;) {
        if (!result.empty())
            result += '/';
        try {
            if (encoded == "~")
                module->initialise(nullptr);
            else {
                auto arena = std::make_shared<framework::NiniConfigArena>(
                    framework::native_nini_invariant_config_backend());
                auto source = arena->create_ini_source(framework::nini_string_reader(cfg_raw(encoded)));
                auto add = [](auto owner) {
                    for (auto name : {u"On", u"Yes"})
                        owner->add_alias(framework::nini_writer_text(name), true);
                    for (auto name : {u"Off", u"No"})
                        owner->add_alias(framework::nini_writer_text(name), false);
                };
                if (aliases == 1)
                    add(source->alias());
                if (aliases == 2) {
                    auto argv =
                        arena->create_source(framework::nini_writer_text(u"Nini.Config.ArgvConfigSource"));
                    add(argv->alias());
                    source->merge(argv);
                }
                if (aliases == 3) {
                    auto section = source->configs()->at(framework::nini_writer_text(u"SimulatorFeatures"));
                    if (section)
                        add(section->alias());
                }
                auto config = server::IniConfig::from_native_source(source);
                module->initialise(&config);
            }
            result += "OK";
        } catch (const std::exception& e) {
            result += cfg_error(e);
        }
        result += ':' + SimulatorFeaturesProbe::flags(*module);
    }
    return result + "|" + hex(c.trace);
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--config-probe") {
        for (std::string line; std::getline(std::cin, line);)
            std::cout << cfg_flow(line) << '\n';
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto first = line.find(':'), last = line.find(':', first + 1);
        Context c;
        c.profile = std::stoi(line.substr(0, first));
        c.failure = line.substr(first + 1, last - first - 1);
        auto backend = std::make_shared<Backend>(c);
        auto syntax = std::make_shared<SimulatorSyntaxState>();
        auto module = std::make_shared<SimulatorFeaturesModule>(backend, syntax);
        auto a = std::make_shared<Scene>(c, "A"), b = std::make_shared<Scene>(c, "B");
        auto registry =
            std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{});
        auto caps = std::make_shared<Caps>(registry, "host", 0, "seed",
                                           framework::Uuid("00000000-0000-0000-0000-000000000007"), "Region",
                                           [registry] { return registry; });
        if (c.profile & 2)
            caps->flags = Caps::TPBR;
        auto original = make_value(c);
        FeatureData saved;
        int config = c.profile % 4;
        std::string output;
        int target = 0;
        for (char op : line.substr(last + 1)) {
            std::string result = "OK";
            try {
                switch (op) {
                case 'I': {
                    auto cfg = server::IniConfig::load(std::filesystem::path(argv[1]) /
                                                       ("config-" + std::to_string(config) + ".ini"));
                    module->initialise(&cfg);
                    break;
                }
                case 'N':
                    module->initialise(nullptr);
                    break;
                case 'J':
                    config ^= 3;
                    break;
                case 'A':
                    module->add_region(a);
                    break;
                case 'B':
                    module->add_region(b);
                    break;
                case 'R':
                    module->remove_region(a);
                    break;
                case 'L':
                    module->region_loaded(a);
                    break;
                case 'M':
                    module->region_loaded(b);
                    break;
                case 'C':
                    module->register_caps({}, caps);
                    break;
                case 'Q':
                    module->register_caps({}, nullptr);
                    break;
                case 'E':
                    a->ev.trigger_register({}, caps);
                    break;
                case 'F':
                    module->add_feature(c.key(), original);
                    break;
                case 'X':
                    module->add_extra(c.key(), original);
                    break;
                case 'D':
                    result = module->remove_feature(c.key()) ? "1" : "0";
                    break;
                case 'T': {
                    FeatureData value;
                    bool yes = module->try_feature(c.key(), value);
                    result = (yes ? "1:" : "0:") + dump(value);
                    break;
                }
                case 'U': {
                    FeatureData value;
                    bool yes = module->try_extra(c.key(), value);
                    result = (yes ? "1:" : "0:") + dump(value);
                    break;
                }
                case 'V':
                    result = module->extra_contains(c.key()) ? "1" : "0";
                    break;
                case 'G':
                    result = dump(module->get_features());
                    break;
                case 'S':
                    module->try_feature(c.key(), saved);
                    break;
                case 'm':
                    if (saved) {
                        if (saved->scalar.kind == network::Value::Kind::Map)
                            saved->map["nested"] =
                                SimulatorFeatureValue::from_value(network::Value(int32_t(8)));
                        else if (saved->scalar.kind == network::Value::Kind::Array)
                            saved->array.push_back(
                                SimulatorFeatureValue::from_value(network::Value("changed")));
                        else if (saved->scalar.kind == network::Value::Kind::Binary)
                            (*network::osd_binary(saved->scalar))[0] = 17;
                    }
                    break;
                case '1':
                case '2':
                case '3':
                case '4': {
                    int id = op - '0';
                    module->on_request.add(
                        {{&target, std::to_string(id)}, [&, id](auto, FeatureData& copy) {
                             c.log("callback" + std::to_string(id));
                             if (id == 1) {
                                 if (!copy)
                                     throw EventQueueNullReference();
                                 copy->map["Added"] = SimulatorFeatureValue::from_value(network::Value(true));
                             }
                             if (id == 2) {
                                 copy = SimulatorFeatureValue::object();
                                 copy->map["Replacement"] =
                                     SimulatorFeatureValue::from_value(network::Value(int32_t(1)));
                             }
                             if (id == 3) {
                                 if (!copy)
                                     throw EventQueueNullReference();
                                 copy->map["Partial"] =
                                     SimulatorFeatureValue::from_value(network::Value(true));
                                 throw std::runtime_error("callback");
                             }
                             if (id == 4)
                                 copy = nullptr;
                         }});
                    break;
                }
                case 'H':
                case 'P':
                case 'Y':
                case 'Z': {
                    network::HttpRequest request;
                    request.method = op == 'P' || op == 'Z' ? "POST" : "GET";
                    SimulatorFeaturesResponse response;
                    response.status = 207;
                    response.raw_buffer =
                        std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{'o', 'l', 'd'});
                    std::string ok = "OK";
                    try {
                        if (op == 'Y' || op == 'Z')
                            module->process_syntax(request, response);
                        else
                            module->process(request, response, caps);
                    } catch (...) {
                        ok = "EX";
                    }
                    result = ok + ":" + std::to_string(response.status) + ":" + hex(response.raw_buffer);
                    break;
                }
                case 'h': {
                    network::HttpRequest request;
                    request.method = "GET";
                    auto paths = caps->handlers().local_paths();
                    if (!paths.contains("SimulatorFeatures"))
                        throw std::runtime_error("route");
                    std::shared_ptr<server::CapabilitySimpleHandler> route;
                    registry->routes.try_simple(paths.at("SimulatorFeatures"), route);
                    auto response = route->handle(request);
                    result = std::to_string(response.status) + ":" + hex(response.body);
                    break;
                }
                case 'x':
                    module->add_feature("Raw", SimulatorFeatureValue::raw_xml(
                                                   "<map><key>raw</key><integer>7</integer></map>"));
                    break;
                case 'z':
                    module->add_feature("Utf8",
                                        SimulatorFeatureValue::utf8(std::make_shared<std::vector<uint8_t>>(
                                            std::initializer_list<uint8_t>{'t', 'e', 'x', 't'})));
                    break;
                case 'O':
                    module->close();
                    result = std::string(module->name);
                    break;
                }
            } catch (...) {
                result = "EX";
            }
            output += result + ",";
        }
        std::vector<std::string> paths;
        for (auto& [name, path] : caps->handlers().local_paths())
            paths.push_back(name + ":" + path.value_or(""));
        std::sort(paths.begin(), paths.end());
        std::string routes;
        for (auto& path : paths)
            routes += path + ",";
        std::cout << output << "|" << dump(SimulatorFeaturesProbe::root(*module)) << "|"
                  << SimulatorFeaturesProbe::flags(*module) << "|" << syntax->id.str() << ":"
                  << hex(syntax->xml) << "|" << a->registered << ":"
                  << a->ev.on_register_caps.snapshot().size() << ":" << b->registered << ":"
                  << b->ev.on_register_caps.snapshot().size() << "|" << routes << "|" << hex(c.trace) << '\n';
        caps->dispose();
    }
}
