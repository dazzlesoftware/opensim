#include "opensim/framework/nini_xml_stream_writer.hpp"
#include "nini_xml_encoding_tables.hpp"
#include <bit>
namespace opensim::framework {
namespace {
[[noreturn]] void fail(std::string type, unsigned h, std::u16string message,
                       std::optional<std::u16string> param = {}, std::optional<int> actual = {}) {
    throw NiniStreamWriterError(std::move(type), std::bit_cast<int>(h), std::move(message), std::move(param),
                                actual);
}
std::u16string line() {
#ifdef _WIN32
    return u"\r\n";
#else
    return u"\n";
#endif
}
int max_bytes(int page, int n) {
    if (n < 0) {
        if (page == 65001)
            fail("System.ArgumentOutOfRangeException", 0x80131502,
                 u"Non-negative number required. (Parameter 'charCount')", u"charCount");
        auto s = std::to_string(n);
        std::u16string v(s.begin(), s.end());
        fail("System.ArgumentOutOfRangeException", 0x80131502,
             u"charCount ('" + v + u"') must be a non-negative value. (Parameter 'charCount')" + line() +
                 u"Actual value was " + v + u".",
             u"charCount", n);
    }
    std::int64_t count = static_cast<std::int64_t>(n) + 1;
    count *= page == 65001 ? 3 : page == 1200 || page == 1201 ? 2 : page == 12000 || page == 12001 ? 4 : 1;
    if (count > 2147483647)
        fail(
            "System.ArgumentOutOfRangeException", 0x80131502,
            u"Too many characters. The resulting number of bytes is larger than what can be returned as an int. (Parameter 'charCount')",
            u"charCount");
    return static_cast<int>(count);
}
struct Encoder {
    int page;
    char16_t pending = 0;
    unsigned char latin(char16_t c) {
        if (c <= 255)
            return static_cast<unsigned char>(c);
        auto p = std::lower_bound(xml_latin1_best_fit.begin(), xml_latin1_best_fit.end(), c,
                                  [](auto pair, char16_t x) { return pair.first < x; });
        return p != xml_latin1_best_fit.end() && p->first == c ? p->second : '?';
    }
    int get_bytes(std::span<const char16_t> input, std::span<std::uint8_t> destination, bool flush) {
        std::vector<std::uint8_t> bytes;
        auto word = [&](std::uint16_t n) {
            if (page == 1201) {
                bytes.push_back(n >> 8);
                bytes.push_back(n & 255);
            } else {
                bytes.push_back(n & 255);
                bytes.push_back(n >> 8);
            }
        };
        auto scalar = [&](std::uint32_t n) {
            if (page == 65001) {
                if (n < 0x80)
                    bytes.push_back(n);
                else if (n < 0x800) {
                    bytes.push_back(0xc0 | (n >> 6));
                    bytes.push_back(0x80 | (n & 63));
                } else if (n < 0x10000) {
                    bytes.push_back(0xe0 | (n >> 12));
                    bytes.push_back(0x80 | ((n >> 6) & 63));
                    bytes.push_back(0x80 | (n & 63));
                } else {
                    bytes.push_back(0xf0 | (n >> 18));
                    bytes.push_back(0x80 | ((n >> 12) & 63));
                    bytes.push_back(0x80 | ((n >> 6) & 63));
                    bytes.push_back(0x80 | (n & 63));
                }
            } else if (page == 1200 || page == 1201) {
                if (n < 0x10000)
                    word(n);
                else {
                    auto v = n - 0x10000;
                    word(0xd800 + (v >> 10));
                    word(0xdc00 + (v & 1023));
                }
            } else if (page == 12000) {
                for (int k = 0; k < 32; k += 8)
                    bytes.push_back((n >> k) & 255);
            } else if (page == 12001) {
                for (int k = 24; k >= 0; k -= 8)
                    bytes.push_back((n >> k) & 255);
            } else if (page == 28591) {
                if (n <= 0xffff)
                    bytes.push_back(latin(n));
                else {
                    bytes.push_back('?');
                    bytes.push_back('?');
                }
            }
        };
        if (page == 20127) {
            for (auto c : input)
                bytes.push_back(c < 128 ? static_cast<std::uint8_t>(c) : '?');
        } else {
            auto held = pending;
            std::size_t i = 0;
            if (held) {
                if (!input.empty() && input[0] >= 0xdc00 && input[0] <= 0xdfff) {
                    scalar(0x10000 + ((held - 0xd800) << 10) + (input[0] - 0xdc00));
                    held = 0;
                    i = 1;
                } else if (!input.empty() || flush) {
                    scalar(0xfffd);
                    held = 0;
                }
            }
            for (; i < input.size(); ++i) {
                auto c = input[i];
                if (c >= 0xd800 && c <= 0xdbff) {
                    if (i + 1 < input.size()) {
                        auto low = input[i + 1];
                        if (low >= 0xdc00 && low <= 0xdfff) {
                            scalar(0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00));
                            ++i;
                        } else
                            scalar(0xfffd);
                    } else if (flush)
                        scalar(0xfffd);
                    else
                        held = c;
                } else if (c >= 0xdc00 && c <= 0xdfff)
                    scalar(0xfffd);
                else
                    scalar(c);
            }
            pending = held;
        }
        if (bytes.size() > destination.size())
            fail("System.ArgumentException", 0x80070057,
                 u"The output byte buffer is too small to contain the encoded data.");
        std::copy(bytes.begin(), bytes.end(), destination.begin());
        return static_cast<int>(bytes.size());
    }
};
std::shared_ptr<XmlStreamEncoding> create(int page) {
    auto e = std::make_shared<XmlStreamEncoding>();
    e->xml_encoding = std::make_shared<NativeXmlTextEncoding>();
    auto name = page == 65001   ? u"utf-8"
                : page == 1200  ? u"utf-16"
                : page == 1201  ? u"utf-16BE"
                : page == 12000 ? u"utf-32"
                : page == 12001 ? u"utf-32BE"
                : page == 20127 ? u"us-ascii"
                                : u"iso-8859-1";
    e->xml_encoding->web_name = [name] { return XmlWriterText(name); };
    e->preamble = [page] {
        switch (page) {
        case 65001:
            return std::vector<std::uint8_t>{0xef, 0xbb, 0xbf};
        case 1200:
            return std::vector<std::uint8_t>{0xff, 0xfe};
        case 1201:
            return std::vector<std::uint8_t>{0xfe, 0xff};
        case 12000:
            return std::vector<std::uint8_t>{0xff, 0xfe, 0, 0};
        case 12001:
            return std::vector<std::uint8_t>{0, 0, 0xfe, 0xff};
        default:
            return std::vector<std::uint8_t>{};
        }
    };
    e->max_byte_count = [page](int n) { return max_bytes(page, n); };
    e->get_encoder = [page] {
        auto state = std::make_shared<Encoder>();
        state->page = page;
        auto encoder = std::make_shared<XmlStreamEncoder>();
        encoder->get_bytes = [state](auto in, auto out, bool flush) {
            return state->get_bytes(in, out, flush);
        };
        return encoder;
    };
    return e;
}
}
std::shared_ptr<XmlStreamEncoding> native_xml_intrinsic_encoding(NiniWriterText name) {
    if (!name)
        fail("System.ArgumentNullException", 0x80004003, u"Value cannot be null. (Parameter 'name')",
             u"name");
    std::u16string lower = *name;
    for (auto& c : lower)
        if (c >= u'A' && c <= u'Z')
            c += 32;
    int page = 0;
    for (auto pair : xml_encoding_aliases)
        if (pair.first == lower) {
            page = pair.second;
            break;
        }
    if (page == 65000)
        fail(
            "System.NotSupportedException", 0x80131515,
            u"Support for UTF-7 is disabled. See https://aka.ms/dotnet-warnings/SYSLIB0001 for more information.");
    if (!page)
        fail(
            "System.ArgumentException", 0x80070057,
            u"'" + *name +
                u"' is not a supported encoding name. For information on defining a custom encoding, see the documentation for the Encoding.RegisterProvider method. (Parameter 'name')",
            u"name");
    static auto utf8 = create(65001), u16le = create(1200), u16be = create(1201), u32le = create(12000),
                u32be = create(12001), ascii = create(20127), latin = create(28591);
    switch (page) {
    case 65001:
        return utf8;
    case 1200:
        return u16le;
    case 1201:
        return u16be;
    case 12000:
        return u32le;
    case 12001:
        return u32be;
    case 20127:
        return ascii;
    default:
        return latin;
    }
}
}
