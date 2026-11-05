// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include "opensim/framework/text.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
using namespace opensim::framework;
std::u16string decode(const std::string& text) {
    if (text == ".")
        return {};
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); i += 4)
        result.push_back(static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16)));
    return result;
}
std::string cache_text(const std::shared_ptr<DotnetRegexAsciiCache>& cache) {
    if (!cache)
        return "-";
    if (cache->empty())
        return ".";
    std::ostringstream out;
    for (std::size_t i = 0; i < cache->size(); ++i) {
        if (i)
            out << ',';
        out << std::hex << std::setfill('0') << std::setw(8) << (*cache)[i].load();
    }
    return out.str();
}
std::string hex(std::u16string_view text) {
    std::ostringstream out;
    for (auto c : text)
        out << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    return out.str();
}
int main() {
    for (std::string row; std::getline(std::cin, row);) {
        DotnetRegexAsciiCacheSlot cache;
        try {
            std::istringstream in(row);
            std::string mode, text;
            in >> mode;
            if (mode == "W") {
                std::cout << "W:";
                for (unsigned i = 0; i < 65536; ++i) {
                    auto c = static_cast<char16_t>(i);
                    unsigned flags = unsigned(dotnet_regex_ecma_word_char(c)) |
                                     (dotnet_regex_word_char(c) << 1) |
                                     (dotnet_regex_boundary_word_char(c) << 2) | (unicode_whitespace(c) << 3);
                    std::cout << std::hex << std::setfill('0') << std::setw(2)
                              << unsigned(dotnet_regex_unicode_category(c)) << flags;
                }
                std::cout << std::dec << '\n';
            } else if (mode == "C") {
                in >> text;
                auto set = decode(text);
                std::array<unsigned char, 8192> bytes{};
                for (unsigned i = 0; i < 65536; ++i)
                    if (dotnet_regex_char_in_class(static_cast<char16_t>(i), set))
                        bytes[i >> 3] |= static_cast<unsigned char>(1u << (i & 7));
                std::cout << "C:";
                for (auto b : bytes)
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
                std::cout << std::dec << '\n';
            } else if (mode == "T") {
                in >> text;
                auto set = decode(text);
                std::atomic<unsigned> errors{0};
                std::vector<std::thread> threads;
                for (unsigned n = 0; n < 8; ++n)
                    threads.emplace_back([&, n]() {
                        for (unsigned i = 0; i < 4096; ++i) {
                            auto c = static_cast<char16_t>((i * 31 + n * 17) % 128);
                            if (dotnet_regex_char_in_class(c, set, cache) !=
                                dotnet_regex_char_in_class(c, set))
                                ++errors;
                        }
                    });
                for (auto& thread : threads)
                    thread.join();
                std::cout << "T:" << errors.load() << ':' << cache_text(cache.load()) << '\n';
            } else {
                unsigned c;
                std::string initial;
                in >> c >> text >> initial;
                if (initial != "-") {
                    std::vector<std::uint32_t> words;
                    if (initial != ".") {
                        std::istringstream numbers(initial);
                        for (std::string word; std::getline(numbers, word, ',');)
                            words.push_back(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
                    }
                    auto value = std::make_shared<DotnetRegexAsciiCache>(words.size());
                    for (std::size_t i = 0; i < words.size(); ++i)
                        (*value)[i].store(words[i]);
                    cache.store(value);
                }
                bool result = dotnet_regex_char_in_class(static_cast<char16_t>(c), decode(text), cache);
                std::cout << "L:" << (result ? 1 : 0) << ':' << cache_text(cache.load()) << '\n';
            }
        } catch (const DotnetRegexClassError& e) {
            std::cout << "E:" << e.source_type << ':' << std::hex << e.hresult << std::dec << ':'
                      << hex(utf16(e.what())) << ':' << cache_text(cache.load()) << '\n';
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what() << '\n';
        }
    }
}
