// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/server/config.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim;
namespace {
std::string hex(std::string_view bytes) {
    std::string out;
    for (unsigned char c : bytes) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::vector<std::uint8_t> bytes(const std::string& text) {
    std::vector<std::uint8_t> out;
    if (text == ".")
        return out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
    return out;
}
std::string utf16hex(std::u16string_view value) {
    std::string raw;
    for (auto c : value) {
        raw += static_cast<char>(c & 255);
        raw += static_cast<char>(c >> 8);
    }
    return hex(raw);
}
std::string describe(const framework::StreamReaderText& value) {
    auto lines = framework::stream_reader_lines(value.text);
    std::string output = std::to_string(static_cast<std::int32_t>(value.encoding)) + "|" +
                         std::to_string(value.bom_bytes) + "|" + utf16hex(value.text) + "|" +
                         std::to_string(lines.size()) + "|";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i)
            output += ',';
        output += utf16hex(lines[i]);
    }
    return output;
}
std::string shape(const server::IniConfig& config) {
    std::string result = "OK:";
    bool first_section = true;
    for (const std::string name : {"Probe", " Probe", "Probe ", "", "Other", "probe"}) {
        if (!first_section)
            result += '|';
        first_section = false;
        result += utf16hex(framework::utf16(name));
        if (!config.has_section(name)) {
            result += "=N";
            continue;
        }
        result += "=Y:";
        std::vector<std::pair<std::u16string, std::u16string>> entries;
        for (auto& [key, value] : config.section(name))
            entries.emplace_back(framework::utf16_replacing(key), framework::utf16_replacing(value));
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i)
                result += ',';
            result += utf16hex(entries[i].first) + ":" + utf16hex(entries[i].second);
        }
    }
    return result;
}
std::string probe(const std::string& mode, const std::string& line) {
    try {
        auto data = bytes(line);
        if (mode == "--probe")
            return describe(framework::decode_stream_reader_text(data));
        std::string name(data.begin(), data.end());
        auto path = framework::utf8_path(name);
        if (mode == "--config-shape-probe")
            return shape(server::IniConfig::load(path));
        if (mode == "--config-probe")
            return "OK:" +
                   utf16hex(framework::utf16_replacing(server::IniConfig::load(path).get("Probe", "x")));
        return describe(framework::read_stream_reader_text(path));
    } catch (const framework::StreamReaderTextError& e) {
        return "ERR:" + std::to_string(e.system_error.value());
    } catch (const std::exception& e) {
        if (mode == "--config-probe" || mode == "--config-shape-probe")
            return "ERR:" + hex(e.what());
        throw;
    }
}
void actual() {
    auto root_parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    auto root = root_parent / ("stream-text-" + framework::Uuid::random().str());
    std::filesystem::create_directory(root);
    auto resolved = std::filesystem::weakly_canonical(root);
    if (resolved.parent_path() != root_parent || !resolved.filename().string().starts_with("stream-text-"))
        throw std::runtime_error("Unexpected stream fixture path");
    {
        auto path = root / framework::utf8_path("配置 é漢😀.ini");
        auto data = bytes("fffe5b00500072006f00620065005d000a0078003d006500e900226f3dd800de0a00");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        auto decoded = framework::read_stream_reader_text(path);
        if (decoded.encoding != framework::StreamReaderEncoding::Utf16LE ||
            server::IniConfig::load(path).get("Probe", "x") != "eé漢😀")
            throw std::runtime_error("Unicode UTF16 configuration load");
        auto missing = root / framework::utf8_path("不存在 é漢😀.ini");
        auto p = missing.u8string();
        std::string expected(reinterpret_cast<const char*>(p.data()), p.size());
        try {
            server::IniConfig::load(missing);
            throw std::runtime_error("Missing Unicode file accepted");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find(expected) == std::string::npos)
                throw;
        }
    }
    auto target = std::filesystem::weakly_canonical(root);
    if (target.parent_path() != root_parent || !target.filename().string().starts_with("stream-text-"))
        throw std::runtime_error("Unexpected cleanup target");
    std::filesystem::remove_all(target);
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            std::string mode = argv[1], line;
            while (std::getline(std::cin, line))
                std::cout << probe(mode, line) << '\n';
            return 0;
        }
        auto double_bom = framework::decode_stream_reader_text(bytes("efbbbfefbbbf61"));
        if (double_bom.text != u"\ufeffa" || double_bom.bom_bytes != 3)
            throw std::runtime_error("BOM must be consumed exactly once");
        if (framework::stream_reader_lines(u"a\r\nb\rc\n") != std::vector<std::u16string>{u"a", u"b", u"c"})
            throw std::runtime_error("BCL line boundaries");
        actual();
        std::cout << "PASS: StreamReader decoding, lines, Unicode INI and missing filename\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
