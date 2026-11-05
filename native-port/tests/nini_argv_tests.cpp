#include "opensim/framework/nini_argv.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
using Text = NiniWriterText;
Text dec(std::string s) {
    if (s == "~")
        return {};
    std::u16string r;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            r.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return nini_writer_text(r);
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
int main(int argc, char**) {
    if (argc == 1) {
        auto b = native_nini_argv_dotnet8_backend();
        auto a = std::make_shared<NiniArgvArgumentArray>(std::vector<Text>{dec("002d002d0078003d0031")});
        NiniArgvParser p(a, b);
        if (*p.get(nini_writer_text(u"x")) != u"1")
            return 1;
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        auto source = NiniArgvConfigSource::create(arena, a, b);
        auto clone = source->get_arguments();
        if (clone == a || (*clone)[0] != (*a)[0])
            return 2;
        (*clone)[0] = nini_writer_text(u"changed");
        if ((*a)[0] == (*clone)[0])
            return 3;
        (*a)[0] = nini_writer_text(u"mutated");
        if ((*source->get_arguments())[0] != (*a)[0])
            return 4;
        source->add_switch(nini_writer_text(u"s"), nini_writer_text(u"x"));
        if (*source->configs()->at(nini_writer_text(u"s"))->get(nini_writer_text(u"x")) != u"1")
            return 5;
        try {
            source->save();
            return 6;
        } catch (const NiniConfigError& e) {
            if (e.kind != NiniConfigError::Kind::Argument)
                return 7;
        }
        try {
            (*clone)[1];
            return 8;
        } catch (const NiniConfigError& e) {
            if (e.kind != NiniConfigError::Kind::IndexOutOfRange)
                return 9;
        }
        return 0;
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        NiniArgvArguments args;
        std::unique_ptr<NiniArgvParser> parser;
        std::shared_ptr<NiniArgvConfigSource> source;
        std::vector<std::string> out;
        std::istringstream line(row);
        for (std::string op; std::getline(line, op, '|');) {
            std::istringstream in(op);
            std::string cmd;
            in >> cmd;
            auto text = [&]() {
                std::string s;
                in >> s;
                return dec(s);
            };
            try {
                if (cmd == "U") {
                    auto c = (*text())[0];
                    auto b = native_nini_argv_dotnet8_backend();
                    out.push_back(std::string(b->regex_word(c) ? "1" : "0") +
                                  (b->regex_space(c) ? "1" : "0"));
                } else if (cmd == "P" || cmd == "S") {
                    std::string s;
                    in >> s;
                    args.reset();
                    if (s != "~") {
                        std::vector<Text> values;
                        if (!s.empty() && s != "!")
                            values.push_back(dec(s));
                        while (in >> s)
                            values.push_back(dec(s));
                        args = std::make_shared<NiniArgvArgumentArray>(std::move(values));
                    }
                    if (cmd == "P")
                        parser = std::make_unique<NiniArgvParser>(args, native_nini_argv_dotnet8_backend());
                    else
                        source =
                            NiniArgvConfigSource::create(arena, args, native_nini_argv_dotnet8_backend());
                } else if (cmd == "C")
                    parser = std::make_unique<NiniArgvParser>(text(), native_nini_argv_dotnet8_backend());
                else if (cmd == "G")
                    out.push_back(hex(parser->get(text())));
                else if (cmd == "A") {
                    auto a = text(), b = text(), c = text();
                    source->add_switch(a, b, c);
                } else if (cmd == "M") {
                    std::size_t index;
                    in >> index;
                    (*args)[index] = text();
                } else if (cmd == "V")
                    source->save();
                else if (cmd == "R")
                    source->reload();
                else if (cmd == "AS") {
                    int v;
                    in >> v;
                    source->auto_save(v != 0);
                } else if (cmd == "APP")
                    setup_opensim_application_argv(source);
                else if (cmd == "EV") {
                    int mode;
                    in >> mode;
                    source->configs()->config_added.add(1, [&, mode](auto&, auto) {
                        out.push_back("added");
                        if (mode == 1)
                            throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"event-fault");
                        if (mode == 2)
                            source->configs()->clear();
                        if (mode == 3)
                            source->add_switch(nini_writer_text(u"nested"), nini_writer_text(u"x"));
                    });
                } else if (cmd == "ID") {
                    auto name = text(), key = text();
                    std::size_t index;
                    in >> index;
                    out.push_back(source->configs()->at(name)->get(key) == (*args)[index] ? "same"
                                                                                          : "different");
                } else if (cmd == "ARGS") {
                    std::string v;
                    bool first = true;
                    auto values = source->get_arguments();
                    for (auto& a : *values) {
                        if (!first)
                            v += ',';
                        first = false;
                        v += hex(a);
                    }
                    out.push_back(v);
                } else if (cmd == "SN") {
                    std::string v;
                    for (int i = 0; i < source->configs()->count(); ++i) {
                        auto c = source->configs()->at(i);
                        v += hex(c->name()) + '[';
                        for (auto& k : c->get_keys())
                            v += hex(k) + '=' + hex(c->get(k)) + ',';
                        v += ']';
                    }
                    out.push_back(v);
                }
                out.push_back("ok");
            } catch (const NiniConfigError& e) {
                std::ostringstream h;
                h << std::hex << std::setfill('0') << std::setw(8) << e.hresult();
                out.push_back(std::string(e.source_type()) + ":" + hex(nini_writer_text(e.message)) + ":" +
                              hex(e.parameter ? nini_writer_text(*e.parameter) : Text{}) + ":" + h.str());
            }
        }
        for (std::size_t i = 0; i < out.size(); ++i)
            std::cout << (i ? "/" : "") << out[i];
        std::cout << '\n';
    }
}
