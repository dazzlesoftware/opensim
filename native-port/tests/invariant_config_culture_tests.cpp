#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
NiniWriterText dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string v;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(s.substr(i, 2), nullptr, 16) |
                                       (std::stoul(s.substr(i + 2, 2), nullptr, 16) << 8));
    return nini_writer_text(v);
}
std::string hex(const NiniWriterText& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string out;
    for (auto c : *t)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
std::string run(const std::string& row) {
    std::istringstream in(row);
    char op;
    std::string a, b;
    int start = 0;
    in >> op >> a;
    auto left = dec(a);
    try {
        if (op == 'L') {
            auto result = invariant_config_lower(left);
            return "L:" + hex(result) + (result == left ? "@R" : "@N");
        }
        in >> b;
        auto right = dec(b);
        if (op == 'E') {
            auto comparison = invariant_config_compare(left, right);
            auto equal = invariant_config_equal(left, right);
            return "C:" + std::to_string(comparison) + ":" + (left && right ? (equal ? "1" : "0") : "-");
        }
        in >> start;
        return "I:" + std::to_string(invariant_config_index_of(left, right, start));
    } catch (const InvariantConfigCultureError& e) {
        return "EX:" + e.source_type + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) + ":" +
               hex(nini_writer_text(e.message)) + ":" + hex(nini_writer_text(e.parameter));
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << run(row) << '\n';
            return 0;
        }
        auto api = make_invariant_config_culture();
        if (api.index_of(nini_writer_text(u"x\u0d4ey"), nini_writer_text(u"\u0d4e"), 0) != -1 ||
            invariant_index_of(u"x\u0d4ey", u"\u0d4e") != -1)
            throw std::runtime_error("Source grapheme-boundary IndexOf regression");
        auto lone = nini_writer_text(std::u16string(1, static_cast<char16_t>(0xd800)));
        if (api.insensitive_equal(lone, nini_writer_text(u"\ufffd")))
            throw std::runtime_error("Raw surrogate equality replaced through UTF8");
        if (*api.lower(nini_writer_text(u"ΟΣ İ")) != u"οσ İ")
            throw std::runtime_error("Invariant simple scalar lower became contextual/expanding");
        if (api.index_of(nini_writer_text(std::u16string(u"xxa\0b", 5)), nini_writer_text(u"ab"), 1) != 2)
            throw std::runtime_error("Invariant ignorable collation search");
        auto text = nini_writer_text(u"unchanged");
        if (api.lower(text) != text)
            throw std::runtime_error("ASCII lowercase identity");
        std::cout << "PASS invariant config culture rawUTF16\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
