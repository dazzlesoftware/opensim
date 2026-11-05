#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim::framework;
std::string hex(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : s)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string error(const ConfigHttpHeaderError& e) {
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':' << hex(e.message) << ':' << std::dec
        << e.request_error << ':' << (e.inner ? error(*e.inner) : "~");
    return out.str();
}
std::string full_error(const ConfigHttpHeaderError& e) {
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':' << hex(e.message) << ':' << std::dec
        << e.request_error << ':' << (e.retry_on_connection_failure ? "1" : "0") << ':'
        << (e.inner ? full_error(*e.inner) : "~");
    return out.str();
}
int main(int argc, char** argv) {
    if (argc == 1) {
        auto bytes = config_http_encode_request_header(u"hello");
        if (bytes.size() != 5 || config_http_decode_response_header({bytes}) != u"hello")
            return 1;
        try {
            config_http_encode_request_header(u"\u0080");
            return 2;
        } catch (const ConfigHttpHeaderError& e) {
            if (e.source_type != "System.Net.Http.HttpRequestException")
                return 3;
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "--map-eof") {
        for (std::string row; std::getline(std::cin, row);) {
            try {
                config_http_response_headers_ended(row == "1");
            } catch (const ConfigHttpHeaderError& e) {
                std::cout << full_error(e) << '\n';
            }
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "--selected-decode") {
        for (std::string row; std::getline(std::cin, row);) {
            std::istringstream input(row);
            std::string name, encoding, raw;
            input >> name >> encoding >> raw;
            std::vector<std::uint8_t> bytes;
            if (raw != "-")
                for (std::size_t i = 0; i < raw.size(); i += 2)
                    bytes.push_back(std::stoul(raw.substr(i, 2), nullptr, 16));
            auto decoder = [encoding](std::span<const std::uint8_t> value) {
                if (encoding == "U")
                    return utf16_replacing(
                        std::string_view(reinterpret_cast<const char*>(value.data()), value.size()));
                std::u16string text;
                for (auto b : value)
                    text += encoding == "A" && b > 127 ? u'?' : char16_t(b);
                return text;
            };
            std::cout << hex(config_http_decode_named_response_header(name == "L" ? "Location" : "Set-Cookie",
                                                                      bytes, decoder))
                      << '\n';
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "--decode") {
        for (std::string row; std::getline(std::cin, row);) {
            std::vector<std::uint8_t> bytes;
            if (row != "-")
                for (std::size_t i = 0; i < row.size(); i += 2)
                    bytes.push_back(std::stoul(row.substr(i, 2), nullptr, 16));
            std::cout << hex(config_http_decode_named_response_header("Location", bytes)) << '\n';
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "--framing") {
        for (std::string row; std::getline(std::cin, row);) {
            std::vector<std::u16string> lengths, encodings;
            std::istringstream input(row);
            for (std::string op; std::getline(input, op, '|');) {
                if (op.size() < 2)
                    continue;
                std::u16string text;
                auto raw = op.substr(2);
                if (raw != "-")
                    for (std::size_t i = 0; i < raw.size(); i += 4)
                        text += char16_t(std::stoul(raw.substr(i, 4), nullptr, 16));
                (op[0] == 'C' ? lengths : encodings).push_back(text);
            }
            auto length = config_http_content_length(lengths);
            std::cout << (config_http_transfer_encoding_chunked(encodings) ? "chunked" : "other") << ':'
                      << (length ? std::to_string(*length) : "~") << '\n';
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "--encode") {
        for (std::string row; std::getline(std::cin, row);) {
            std::u16string text;
            if (row != "-")
                for (std::size_t i = 0; i < row.size(); i += 4)
                    text += char16_t(std::stoul(row.substr(i, 4), nullptr, 16));
            try {
                config_http_encode_request_header(text);
                std::cout << "ok\n";
            } catch (const ConfigHttpHeaderError& e) {
                std::cout << error(e) << '\n';
            }
        }
        return 0;
    }
    for (std::string row; std::getline(std::cin, row);) {
        std::vector<std::uint8_t> raw;
        for (std::size_t i = 0; i < row.size(); i += 2)
            raw.push_back(std::stoul(row.substr(i, 2), nullptr, 16));
        try {
            auto head = config_http_parse_response_head(raw);
            if (!head) {
                std::cout << "MORE\n";
                continue;
            }
            std::cout << "ok:" << head->status << ":1." << head->minor_version << ':' << hex(head->reason);
            for (auto& h : head->headers) {
                std::u16string name(h.name.begin(), h.name.end());
                std::cout << "/H:" << hex(name) << ':' << hex(h.value);
            }
            std::cout << '\n';
        } catch (const ConfigHttpHeaderError& e) {
            std::cout << error(e) << '\n';
        }
    }
}
