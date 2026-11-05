#include "opensim/framework/config_http_headers_body.hpp"
#include <iostream>
#include <algorithm>
using namespace opensim::framework;
void reuse_tests() {
    int scenario = 0;
    auto require = [&](bool value) {
        if (!value)
            throw std::runtime_error("HTTP connection reuse framing assertion failed in scenario " +
                                     std::to_string(scenario));
    };
    auto check = [&](ConfigHttpResponseHead head, std::string bytes, std::size_t entity_end,
                     bool initially_complete, bool finally_complete, bool fails) {
        ++scenario;
        std::size_t at = 0;
        bool transport_failed = false;
        ConfigHttpXmlBodyReader reader(head, [&](std::span<std::uint8_t> out) -> std::int32_t {
            if (bytes == "socket-error") {
                transport_failed = true;
                throw std::runtime_error("socket failed");
            }
            auto n = std::min<std::size_t>({out.size(), bytes.size() - at, 1});
            std::copy_n(bytes.begin() + at, n, out.begin());
            at += n;
            return static_cast<std::int32_t>(n);
        });
        require(reader.completed_for_reuse() == initially_complete);
        require(reader.read({}) == 0 && reader.completed_for_reuse() == initially_complete);
        std::uint8_t byte;
        bool failed = false;
        try {
            while (reader.read({&byte, 1})) {
            };
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed == fails && reader.completed_for_reuse() == finally_complete);
        require(transport_failed || at == entity_end);
        if (failed) {
            // Even a later successful read cannot make a failed transport reusable.
            bytes = std::string(at + 32, 'x');
            try {
                reader.read({&byte, 1});
            } catch (const std::exception&) {
            }
            require(!reader.completed_for_reuse());
        }
    };
    ConfigHttpResponseHead head;
    head.status = 200;
    auto length = head;
    length.headers.push_back({"Content-Length", u"3"});
    check(length, "abcNEXT", 3, false, true, false);
    check(length, "ab", 2, false, false, true);
    check(length, "socket-error", 0, false, false, true);
    auto zero = head;
    zero.headers.push_back({"Content-Length", u"0"});
    check(zero, "NEXT", 0, true, true, false);
    auto empty = head;
    empty.status = 204;
    check(empty, "NEXT", 0, true, true, false);
    empty.status = 304;
    check(empty, "NEXT", 0, true, true, false);
    check(head, "abc", 3, false, false, false);
    head.status = 101;
    check(head, "abc", 3, false, false, false);
    auto chunked = length;
    chunked.headers.push_back({"Transfer-Encoding", u"chunked"});
    std::string body = "3\r\nabc\r\n0\r\nTrailer: value\r\n\r\n";
    check(chunked, body + "NEXT", body.size(), false, true, false);
    check(chunked, "3\r\nabc\r\n", 8, false, false, true);
    check(chunked, "X\r\n", 3, false, false, true);
    check(chunked, "0\r\nInvalid\r\n\r\n", 12, false, false, true);
}
int main() {
    auto require = [](bool v) {
        if (!v)
            throw std::runtime_error("HTTP XML entity reader assertion failed");
    };
    try {
        reuse_tests();
        auto test = [&](ConfigHttpResponseHead head, std::string bytes, std::string expected) {
            std::size_t at = 0;
            ConfigHttpXmlBodyReader reader(head, [&](std::span<std::uint8_t> out) {
                auto size = std::min<std::size_t>({out.size(), bytes.size() - at, 2});
                std::copy_n(bytes.begin() + at, size, out.begin());
                at += size;
                return static_cast<std::int32_t>(size);
            });
            std::string result;
            std::uint8_t buffer[3];
            for (;;) {
                auto n = reader.read(buffer);
                if (!n)
                    break;
                result.append(reinterpret_cast<char*>(buffer), n);
            }
            require(result == expected);
        };
        ConfigHttpResponseHead close;
        close.status = 200;
        test(close, "abc", "abc");
        auto length = close;
        length.headers.push_back({"Content-Length", u"2"});
        test(length, "abc", "ab");
        auto empty = close;
        empty.status = 204;
        test(empty, "abc", "");
        auto chunked = close;
        chunked.headers.push_back({"Transfer-Encoding", u"chunked"});
        test(chunked, "3\r\nabc\r\n0\r\nTrailer: value\r\n\r\n", "abc");
        try {
            test(length, "a", "");
            return 2;
        } catch (const ConfigHttpHeaderError& e) {
            require(e.source_type == "System.Net.Http.HttpIOException" && e.request_error == 10);
        }
        std::cout << "PASS native HTTP XML entity stream assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
