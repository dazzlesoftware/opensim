// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/config.hpp"
#include "opensim/services/native_flotsam_config.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/filesystem.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace framework;
using NiniConfigView = server::IniConfig;
namespace {
std::string hex(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out.empty() ? "-" : out;
}
std::string unhex(std::string_view text) {
    if (text == "-")
        return {};
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16));
    return out;
}
std::u16string raw(std::string_view text) {
    std::u16string out;
    if (text != "-")
        for (std::size_t i = 0; i < text.size(); i += 4)
            out += static_cast<char16_t>(std::stoi(std::string(text.substr(i, 4)), nullptr, 16));
    return out;
}
std::string rawhex(std::u16string_view text) {
    std::string out;
    for (auto c : text)
        for (int n : {12, 8, 4, 0})
            out += "0123456789abcdef"[(c >> n) & 15];
    return out.empty() ? "-" : out;
}
NiniWriterText t(const std::string& s) {
    return nini_writer_text(utf16_replacing(s));
}
std::string error(const std::exception& e) {
    auto show = [](const std::string& type, std::uint32_t code, const std::optional<std::u16string>& p,
                   const std::u16string& m) {
        return "E:" + type + ":" + std::to_string(code) + ":" + (p ? rawhex(*p) : "~") + ":" + rawhex(m);
    };
    if (std::string(e.what()) ==
        "INI Include-* loading is not ported yet; use an explicit native configuration")
        return "GUARD";
    if (auto p = dynamic_cast<const services::FlotsamConfigValueError*>(&e))
        return show(p->kind == services::FlotsamConfigValueError::Kind::Format ? "System.FormatException"
                    : p->kind == services::FlotsamConfigValueError::Kind::Overflow
                        ? "System.OverflowException"
                        : "System.ArgumentException",
                    p->hresult(), p->parameter, p->raw_message);
    if (auto p = dynamic_cast<const NiniConfigError*>(&e))
        return show(p->source_type(), p->hresult(), p->parameter, p->message);
    if (auto p = dynamic_cast<const NiniDocumentError*>(&e))
        return show(p->source_type(), p->hresult(), p->parameter, p->message);
    if (auto p = dynamic_cast<const NiniReaderError*>(&e))
        return show(p->source_type(), p->hresult(), {}, p->message_utf16());
    if (auto p = dynamic_cast<const NiniStreamError*>(&e))
        return show(p->source_type, p->hresult, p->parameter, p->message);
    return "X:" + hex(e.what());
}
std::string execute(const std::string& line) {
    std::array<std::optional<NiniConfigView>, 4> views;
    std::string output;
    std::istringstream sequence(line);
    for (std::string operation; std::getline(sequence, operation, '|');) {
        if (!output.empty())
            output += '/';
        try {
            std::istringstream row(operation);
            std::string op;
            int i;
            row >> op >> i;
            auto text = [&]() {
                std::string s;
                row >> s;
                return unhex(s);
            };
            auto& v = views.at(i);
            std::string result = "OK";
            if (!v && op != "NEW" && op != "ARG" && op != "TEXT" && op != "LOAD" && op != "HOSTLOAD" &&
                op != "COPY" && op != "DROP")
                throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                      u"Object reference not set to an instance of an object.");
            if (op == "NEW")
                v.emplace();
            else if (op == "ARG") {
                auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                v.emplace(server::IniConfig::from_native_source(
                    arena->create_source(nini_writer_text(u"Nini.Config.ArgvConfigSource"))));
            } else if (op == "TEXT") {
                auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                v.emplace(server::IniConfig::from_native_source(
                    arena->create_ini_source(nini_string_reader(utf16_replacing(text())))));
            } else if (op == "HOSTLOAD")
                v = server::IniConfig::load(utf8_path(text()));
            else if (op == "LOAD")
                v = server::IniConfig::from_native_source(
                    server::NiniConfigView::load_raw(utf8_path(text())).native_source());
            else if (op == "COPY") {
                int from;
                row >> from;
                v = views.at(from);
            } else if (op == "DROP")
                v.reset();
            else if (op == "SET") {
                auto section = text(), key = text(), value = text();
                v->set(section, key, value);
            } else if (op == "GET") {
                auto section = text(), key = text(), fallback = text();
                result = "G:" + hex(v->get(section, key, fallback));
            } else if (op == "INT") {
                auto section = text(), key = text();
                std::int32_t fallback;
                row >> fallback;
                result = "I:" + std::to_string(v->integer(section, key, fallback));
            } else if (op == "BOOL") {
                auto section = text(), key = text();
                bool fallback;
                row >> fallback;
                result = "B:" + std::to_string(v->boolean(section, key, fallback));
            } else if (op == "FI" || op == "FL" || op == "FR" || op == "FB") {
                auto section = text(), key = text();
                auto host = std::make_shared<server::IniConfig>(*v);
                auto native = services::native_flotsam_config_source(host)->section(section);
                if (op == "FI")
                    result = "I:" + std::to_string(native->integer(key, 17));
                else if (op == "FL")
                    result = "L:" + std::to_string(native->long_integer(key, INT64_MIN));
                else if (op == "FR")
                    result = "R:" + std::to_string(std::bit_cast<std::uint64_t>(native->real(key, -0.0)));
                else
                    result = "B:" + std::to_string(native->boolean(key, false));
            } else if (op == "HAS")
                result = "B:" + std::to_string(v->has_section(text()));
            else if (op == "CON") {
                auto section = text(), key = text();
                result = "B:" + std::to_string(v->contains(section, key));
            } else if (op == "ORDER") {
                auto pairs = v->ordered_section(text());
                result = "O:" + std::to_string(pairs.size());
                for (const auto& [k, value] : pairs)
                    result += ":" + hex(k) + "=" + hex(value);
            } else if (op == "ALIAS") {
                auto name = text();
                bool value;
                row >> value;
                v->native_source()->alias()->add_alias(t(name), value);
            } else if (op == "MERGE") {
                int from;
                row >> from;
                v->native_source()->merge(views.at(from)->native_source());
            } else if (op == "CASE") {
                bool value;
                row >> value;
                std::dynamic_pointer_cast<NiniIniConfigSource>(v->native_source())->case_sensitive(value);
            } else if (op == "REPLACE")
                v->replace_key_values();
            else if (op == "RAW") {
                auto section = text(), key = text();
                std::string value;
                row >> value;
                auto config = v->native_section(section);
                if (!config)
                    config = v->native_source()->add_config(t(section));
                config->set_text(t(key), nini_writer_text(raw(value)));
            } else if (op == "OWNER") {
                auto section = text();
                int other;
                row >> other;
                auto config = v->native_section(section);
                result = "B:" + std::to_string(config &&
                                               config->config_source() == views.at(other)->native_source());
            } else if (op == "SAVE")
                std::dynamic_pointer_cast<NiniIniConfigSource>(v->native_source())->save(t(text()));
            else
                throw std::runtime_error("Unknown fixture command");
            output += result;
        } catch (const std::exception& e) {
            output += error(e);
        }
    }
    return output;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void unit() {
    std::shared_ptr<NiniConfig> retained;
    std::weak_ptr<NiniConfigSourceBase> owner;
    {
        NiniConfigView original;
        original.set("s", "z", "old");
        original.set("s", "a", "second");
        auto copy = original;
        copy.set("s", "z", "new");
        require(original.get("s", "z") == "new", "Copies must share source graph");
        auto ordered = original.ordered_section("s");
        require(ordered.size() == 2 && ordered[0].first == "z" && ordered[0].second == "new" &&
                    ordered[1].first == "a",
                "GetKeys order and current Get values");
        require(original.section("s").at("z") == "new", "Map projection must use live values");
        retained = original.native_section("s");
        owner = original.native_source();
    }
    require(!owner.expired() && *retained->get(t("z")) == u"new",
            "Native section retains arena after views retire");
    retained.reset();
    require(owner.expired(), "Arena must retire after last retained native handle");
    {
        auto config = std::make_shared<server::IniConfig>();
        config->set("Probe", "x", "On");
        auto production = services::native_flotsam_config_source(config);
        auto section = production->section("Probe");
        auto rejects = [&] {
            try {
                section->boolean("x", false);
                return false;
            } catch (const services::FlotsamConfigValueError& e) {
                return e.kind == services::FlotsamConfigValueError::Kind::Alias;
            }
        };
        require(rejects(), "Production file source must not acquire argv aliases");
        auto fixture = services::native_flotsam_config_source(config, true)->section("Probe");
        require(fixture->boolean("x", false) && rejects(),
                "Explicit fixture aliases must not mutate production source");
        config->native_source()->alias()->add_alias(t("On"), true);
        require(section->boolean("x", false), "Production getter uses current owning-source alias");
        config->native_section("Probe")->set_text(t("x"), nini_writer_text(std::u16string(1, 0xd800)));
        std::weak_ptr wrapper = config;
        config.reset();
        require(wrapper.expired(), "Typed adapter retains native graph without borrowing wrapper");
        try {
            section->integer("x", 0);
            throw std::runtime_error("Expected raw format diagnostic");
        } catch (const services::FlotsamConfigValueError& e) {
            require(e.kind == services::FlotsamConfigValueError::Kind::Format &&
                        e.raw_message.find(char16_t(0xd800)) != e.raw_message.npos,
                    "Typed adapter keeps raw source diagnostics after wrapper retirement");
        }
    }
    try {
        auto bad = server::IniConfig::from_native_source(nullptr);
        throw std::runtime_error("Null source accepted");
    } catch (const std::invalid_argument&) {
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            for (std::string line; std::getline(std::cin, line);)
                std::cout << execute(line) << '\n';
            return 0;
        }
        unit();
        std::cout
            << "PASS: actual IniConfig retained source ownership, shared copies and live ordered values\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
