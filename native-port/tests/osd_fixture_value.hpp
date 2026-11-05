// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <sstream>
namespace fixture {
inline std::string unhex(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result += static_cast<char>(std::stoi(text.substr(i, 2), nullptr, 16));
    return result;
}
inline std::string base64(const std::string& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t bits = static_cast<unsigned char>(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            bits |= static_cast<unsigned char>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size())
            bits |= static_cast<unsigned char>(bytes[i + 2]);
        result += alphabet[(bits >> 18) & 63];
        result += alphabet[(bits >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[bits & 63] : '=';
    }
    return result;
}
inline opensim::network::Value build(const std::string& line) {
    using opensim::network::Value;
    const auto space = line.find(' ');
    const auto kind = line.substr(0, space),
               text = space == std::string::npos ? std::string{} : line.substr(space + 1);
    Value value;
    if (kind == "real") {
        value.kind = Value::Kind::Real;
        value.real = std::bit_cast<double>(std::stoull(text, nullptr, 16));
    } else if (kind == "integer")
        value = Value(static_cast<std::int32_t>(std::stol(text)));
    else if (kind == "long")
        value = Value::long_integer(std::stoll(text));
    else if (kind == "unsigned") {
        const auto number = std::stoull(text);
        if (number > 4294967295ull)
            throw std::out_of_range("unsigned fixture value");
        value = opensim::network::osd_from_unsigned(static_cast<std::uint32_t>(number));
    } else if (kind == "bool")
        value = Value(text == "1");
    else if (kind == "string")
        value = Value(unhex(text));
    else if (kind == "binary") {
        value.kind = Value::Kind::Binary;
        value.text = base64(unhex(text));
    } else if (kind == "uuid")
        value.kind = Value::Kind::Uuid;
    else if (kind == "uri") {
        value.kind = Value::Kind::Uri;
        value.text = "http://example.test/";
    } else if (kind == "date") {
        value.kind = Value::Kind::Date;
        value.text = text;
    } else if (kind == "array") {
        value.kind = Value::Kind::Array;
        std::istringstream input(text);
        std::string part;
        while (std::getline(input, part, ','))
            value.array.emplace_back(static_cast<std::int32_t>(std::stol(part)));
    } else if (kind == "map")
        value.kind = Value::Kind::Map;
    return value;
}
}
