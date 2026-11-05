// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/network/sdk_llsd_notation.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/sdk_json_reader.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/sdk_json_reader.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/asset_base.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <bit>
using namespace opensim;
using namespace network;
std::string hex(std::string_view bytes) {
    std::string out;
    const char* digits = "0123456789abcdef";
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
std::string units(std::u16string_view value) {
    std::string out;
    const char* digits = "0123456789abcdef";
    for (auto c : value)
        for (int shift : {12, 8, 4, 0})
            out += digits[(c >> shift) & 15];
    return out;
}
std::string text(std::string_view input) {
    return units(framework::utf16_replacing(input));
}

std::string dump(const Value& value) {
    if (value.kind == Value::Kind::NullReference)
        return "N";
    switch (value.kind) {
    case Value::Kind::Undefined:
        return "U";
    case Value::Kind::Boolean:
        return value.boolean ? "B1" : "B0";
    case Value::Kind::Long:
        return "L" + std::to_string(value.long_value);
    case Value::Kind::Integer:
        return "I" + std::to_string(value.integer);
    case Value::Kind::Real: {
        std::ostringstream out;
        out << "R" << std::hex << std::setfill('0') << std::setw(16)
            << std::bit_cast<std::uint64_t>(value.real);
        return out.str();
    }
    case Value::Kind::Uuid:
        return "Q" + osd_uuid(value).str();
    case Value::Kind::String:
        return "S" + units(osd_string_utf16(value));
    case Value::Kind::Uri:
        return "V" + units(osd_string_utf16(value));
    case Value::Kind::Date: {
        auto bytes = osd_binary(value);
        return "D" + std::to_string(value.sdk_date->ticks()) + ":" + std::to_string(value.sdk_date->kind()) +
               ":" + text(osd_string(value)) + ":" +
               hex({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    }
    case Value::Kind::Binary: {
        auto bytes = osd_binary(value);
        return "X" + hex({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    }
    case Value::Kind::Array: {
        std::string out = "A" + std::to_string(value.array.size()) + "[";
        for (const auto& item : value.array)
            out += dump(item) + ";";
        return out + "]";
    }
    case Value::Kind::Map: {
        std::string out = "M" + std::to_string(value.map.size()) + "[";
        for (const auto& key : value.ordered_keys())
            out += units(sdk_map_key_utf16(key)) + "=" + dump(value.get(key)) + ";";
        return out + "]";
    }
    default:
        throw std::runtime_error("Unexpected SDK kind");
    }
}
int main(int argc, char** argv) {
    std::string row;
    while (std::getline(std::cin, row)) {
        std::string input;
        for (std::size_t i = 0; i < row.size(); i += 2)
            input += static_cast<char>(std::stoul(row.substr(i, 2), nullptr, 16));
        try {
            auto mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
            auto value = mode == "--json"
                             ? parse_osd_json_utf16(
                                   framework::decode_stream_reader_text(
                                       {reinterpret_cast<const std::uint8_t*>(input.data()), input.size()})
                                       .text)
                         : mode == "--text"     ? deserialize_sdk_osd_text(framework::utf16_replacing(input))
                         : mode == "--bytes"    ? deserialize_sdk_osd_bytes(input)
                         : mode == "--stream"   ? deserialize_sdk_osd_stream(input)
                         : mode == "--notation" ? deserialize_sdk_llsd_notation(input)
                         : mode == "--binary"   ? deserialize_sdk_llsd_binary(input)
                                                : deserialize_sdk_llsd_xml(input);
            auto output = dump(value);
            if (mode == "--json" || mode == "--stream" || mode == "--text" || mode == "--bytes")
                output += "|" + text(osd_json(value, true));
            std::cout << output << "\n";
        } catch (const SdkJsonError& e) {
            std::cout << "EX:" << units(e.message) << "\n";
        } catch (const SdkNotationError& e) {
            std::cout << "EX:" << text(e.message) << "\n";
        } catch (const std::exception& e) {
            std::cout << "EX:" << text(e.what()) << "\n";
        }
    }
}
