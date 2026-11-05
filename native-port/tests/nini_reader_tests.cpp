// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/nini_reader.hpp"
#include <iostream>
#include <sstream>
#include <vector>
using namespace opensim::framework;
namespace {
std::u16string decode(std::string_view text) {
    std::u16string out;
    if (text == ".")
        return out;
    for (std::size_t i = 0; i < text.size(); i += 4) {
        auto lo = std::stoi(std::string(text.substr(i, 2)), nullptr, 16),
             hi = std::stoi(std::string(text.substr(i + 2, 2)), nullptr, 16);
        out.push_back(static_cast<char16_t>(lo | (hi << 8)));
    }
    return out;
}
std::string hex(std::u16string_view text) {
    std::string out;
    for (auto c : text)
        for (auto b : {static_cast<unsigned>(c & 255), static_cast<unsigned>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out.empty() ? "." : out;
}
std::string hex(const std::u16string& text) {
    return hex(std::u16string_view(text));
}
std::string hex(const std::optional<std::u16string>& text) {
    return text ? hex(std::u16string_view(*text)) : "-";
}
struct FaultReader : NiniTextReader {
    std::shared_ptr<NiniTextReader> inner;
    std::vector<std::string>& trace;
    int point, nth, seen = 0;
    FaultReader(std::u16string text, std::vector<std::string>& log, int p, int n)
        : inner(nini_string_reader(std::move(text))), trace(log), point(p), nth(n) {}
    void hit(int p, const char* name) {
        trace.emplace_back(name);
        if (point == p && ++seen == nth)
            throw std::runtime_error("fault");
    }
    int peek() override {
        hit(1, "P");
        return inner->peek();
    }
    int read() override {
        hit(2, "R");
        return inner->read();
    }
    void close() override {
        hit(3, "C");
        inner->close();
    }
};
std::string error(std::exception_ptr saved) {
    if (!saved)
        return "-";
    try {
        std::rethrow_exception(saved);
    } catch (const NiniReaderError& e) {
        return std::string(e.source_type()) + ":" + std::to_string(static_cast<std::uint32_t>(e.hresult())) +
               ":" + hex(e.message_utf16()) + ":" +
               std::to_string(e.kind == NiniReaderError::Kind::Ini ? e.position->line : 0) + ":" +
               std::to_string(e.kind == NiniReaderError::Kind::Ini ? e.position->column : 0);
    } catch (const std::exception& e) {
        std::string text = e.what();
        return "System.Exception:" + std::to_string(0x80131500u) + ":" +
               hex(std::u16string(text.begin(), text.end())) + ":0:0";
    }
}
std::string run(const std::string& line) {
    std::istringstream input(line);
    std::string text, operations;
    int flags, point, nth;
    input >> text >> flags >> point >> nth >> operations;
    std::vector<std::string> trace;
    auto backend = text == "-" ? std::shared_ptr<NiniTextReader>{}
                               : std::make_shared<FaultReader>(decode(text), trace, point, nth);
    NiniReader reader(backend);
    reader.ignore_comments(flags & 1);
    reader.line_continuation(flags & 2);
    reader.accept_comment_after_key(flags & 4);
    reader.accept_no_assignment(flags & 8);
    reader.consume_all_key_text(flags & 16);
    std::shared_ptr<std::u16string> comments, assign;
    std::exception_ptr saved;
    std::string output;
    std::istringstream ops(operations);
    for (std::string op; std::getline(ops, op, ',');) {
        std::string result = op;
        try {
            if (op == "r")
                result = reader.read() ? "1" : "0";
            else if (op == "s")
                result = reader.move_to_next_section() ? "1" : "0";
            else if (op == "k")
                result = reader.move_to_next_key() ? "1" : "0";
            else if (op == "c")
                reader.close();
            else if (op == "d")
                reader.dispose();
            else if (op == "q")
                result = error(saved);
            else if (op == "gc") {
                auto copy = reader.comment_delimiters();
                result = hex(copy);
                if (!copy.empty())
                    copy[0] = u'!';
            } else if (op == "ga") {
                auto copy = reader.assign_delimiters();
                result = hex(copy);
                if (!copy.empty())
                    copy[0] = u'!';
            } else if (op.starts_with("sc")) {
                comments =
                    op.substr(2) == "-" ? nullptr : std::make_shared<std::u16string>(decode(op.substr(2)));
                reader.comment_delimiters(comments);
            } else if (op.starts_with("sa")) {
                assign =
                    op.substr(2) == "-" ? nullptr : std::make_shared<std::u16string>(decode(op.substr(2)));
                reader.assign_delimiters(assign);
            } else if (op == "mc") {
                if (comments && !comments->empty())
                    (*comments)[0] = u'#';
            } else if (op == "ma") {
                if (assign && !assign->empty())
                    (*assign)[0] = u':';
            } else if (op.size() == 2) {
                bool value = op[1] == '1';
                if (op[0] == 'i')
                    reader.ignore_comments(value);
                else if (op[0] == 'l')
                    reader.line_continuation(value);
                else if (op[0] == 't')
                    reader.accept_comment_after_key(value);
                else if (op[0] == 'a')
                    reader.accept_no_assignment(value);
                else if (op[0] == 'v')
                    reader.consume_all_key_text(value);
                else
                    throw std::runtime_error("operation");
            } else
                throw std::runtime_error("operation");
        } catch (...) {
            saved = std::current_exception();
            result = "EX:" + error(saved);
        }
        int bits = (reader.ignore_comments() ? 1 : 0) | (reader.line_continuation() ? 2 : 0) |
                   (reader.accept_comment_after_key() ? 4 : 0) | (reader.accept_no_assignment() ? 8 : 0) |
                   (reader.consume_all_key_text() ? 16 : 0);
        if (!output.empty())
            output += '|';
        output += result + "/" + std::to_string(static_cast<int>(reader.type())) + "," +
                  std::to_string(static_cast<int>(reader.state())) + "," + std::to_string(reader.line()) +
                  "," + std::to_string(reader.column()) + "," + hex(reader.name()) + "," +
                  hex(reader.value()) + "," + hex(reader.comment()) + "," + hex(reader.comment_delimiters()) +
                  "," + hex(reader.assign_delimiters()) + "," + std::to_string(bits) + "/" + error(saved);
    }
    output += "/IO:";
    for (auto& event : trace)
        output += event;
    return output;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            for (std::string line; std::getline(std::cin, line);)
                std::cout << run(line) << '\n';
            return 0;
        }
        NiniReader reader(std::u16string(u"[ Section ]\nx=\"a;b\"; tail\n"));
        if (!reader.read() || reader.name() != u" Section" || !reader.read() || reader.value() != u"a;b" ||
            reader.comment() != std::optional<std::u16string>(u"tail"))
            throw std::runtime_error("Original INI token semantics");
        NiniReader eof(std::u16string(u" "));
        if (!eof.read() || eof.state() != NiniReadState::EndOfFile || eof.read())
            throw std::runtime_error("Source whitespace recursion EOF quirk");
        auto delimiters = std::make_shared<std::u16string>(u"=");
        NiniReader aliases(std::u16string(u"x:y"));
        aliases.assign_delimiters(delimiters);
        (*delimiters)[0] = u':';
        if (!aliases.read() || aliases.value() != u"y")
            throw std::runtime_error("Delimiter array identity");
        std::cout << "PASS: native Nini reader tokens/state/quotes/delimiter ownership\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
