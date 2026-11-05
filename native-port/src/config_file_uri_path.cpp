// Source .NET8.0.31 Uri.Compress; .NET Foundation MIT license.
#include "opensim/framework/config_file_uri_path.hpp"
namespace opensim::framework {
std::u16string config_file_uri_compress(std::u16string span, bool convert, bool file) {
    int num = 0, num2 = 0, num3 = 0, num4 = 0;
    for (int num5 = static_cast<int>(span.size()) - 1; num5 >= 0; --num5) {
        auto c = span[num5];
        if (c == u'\\' && convert)
            c = span[num5] = u'/';
        if (c == u'/')
            ++num;
        else {
            if (num > 1)
                num2 = num5 + 1;
            num = 0;
        }
        if (c == u'.') {
            ++num3;
            continue;
        }
        if (num3 != 0) {
            if ((file || (num3 <= 2 && c == u'/')) && c == u'/' &&
                (num2 == num5 + num3 + 1 ||
                 (num2 == 0 && num5 + num3 + 1 == static_cast<int>(span.size()))) &&
                num3 <= 2) {
                num2 = num5 + 1 + num3 + (num2 != 0 ? 1 : 0);
                span.erase(num5 + 1, num2 - num5 - 1);
                num2 = num5;
                if (num3 == 2)
                    ++num4;
                num3 = 0;
                continue;
            }
            num3 = 0;
        }
        if (c == u'/') {
            if (num4 != 0) {
                --num4;
                span.erase(num5 + 1, num2 - num5);
            }
            num2 = num5;
        }
    }
    if (!span.empty() && file && num <= 1) {
        if (num4 != 0 && span[0] != u'/') {
            ++num2;
            span.erase(0, num2);
            return span;
        }
        if (num3 != 0 && (num2 == num3 || (num2 == 0 && num3 == static_cast<int>(span.size())))) {
            num3 += num2 != 0 ? 1 : 0;
            span.erase(0, num3);
            return span;
        }
    }
    return span;
}

std::u16string config_file_uri_unescape_dots(std::u16string text, bool convert) {
    if (text.size() < 3)
        return text;
    auto hex = [](char16_t c) -> int {
        return c >= u'0' && c <= u'9'   ? c - u'0'
               : c >= u'A' && c <= u'F' ? c - u'A' + 10
               : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                        : -1;
    };
    auto decode = [&](char16_t a, char16_t b) {
        auto x = hex(a), y = hex(b);
        return x < 0 || y < 0 ? char16_t(0xffff) : char16_t((x << 4) | y);
    };
    auto third = convert ? u'\\' : char16_t(0xffff);
    auto begin = text.data(), pch = begin, ptr = begin + text.size() - 2;
    char16_t* ptr2 = nullptr;
    while (pch < ptr) {
        if (*(pch++) != u'%')
            continue;
        auto a = *(pch++);
        auto b = *(pch++);
        auto c = decode(a, b);
        if (c != u'.' && c != u'/' && c != third)
            continue;
        ptr2 = pch - 2;
        *(ptr2 - 1) = c;
        while (pch < ptr) {
            if ((*(ptr2++) = *(pch++)) == u'%') {
                auto a = *(ptr2++) = *(pch++);
                auto b = *(ptr2++) = *(pch++);
                c = decode(a, b);
                if (c == u'.' || c == u'/' || c == third) {
                    ptr2 -= 2;
                    *(ptr2 - 1) = c;
                }
            }
        }
        break;
    }
    ptr += 2;
    if (!ptr2)
        return text;
    if (pch == ptr) {
        text.resize(text.size() - (pch - ptr2));
        return text;
    }
    *(ptr2++) = *(pch++);
    if (pch == ptr) {
        text.resize(text.size() - (pch - ptr2));
        return text;
    }
    *(ptr2++) = *(pch++);
    text.resize(text.size() - (pch - ptr2));
    return text;
}

ConfigFileUriSchemeScan config_file_uri_scan_scheme(std::u16string_view input, bool windows) {
    ConfigFileUriSchemeScan result;
    auto alpha = [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); };
    auto lower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? char16_t(c + 32) : c; };
    auto lws = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
    auto sep = [](char16_t c) { return c == u'/' || c == u'\\'; };
    if (input.empty()) {
        result.error = 4;
        return result;
    }
    if (input.size() >= 65520) {
        result.error = 6;
        return result;
    }
    auto starts = [&](std::u16string_view value) {
        if (input.size() < value.size())
            return false;
        for (size_t i = 0; i < value.size(); ++i)
            if (lower(input[i]) != value[i])
                return false;
        return true;
    };
    if (starts(u"https:")) {
        result.scheme = u"https";
        result.flags = 6;
        return result;
    }
    if (starts(u"http:")) {
        result.scheme = u"http";
        result.flags = 5;
        return result;
    }
    size_t i = 0;
    while (i < input.size() && lws(input[i]))
        ++i;
    if (!windows && i < input.size() && input[i] == u'/' && (i + 1 >= input.size() || !sep(input[i + 1]))) {
        result.flags = 0x100020100000ull | i;
        result.scheme = u"file";
        result.unix_file_syntax = true;
        return result;
    }
    auto colon = input.substr(i).find(u':');
    if (i + 2 >= input.size() || colon == 0 || i >= input.size() || i + 1 >= input.size()) {
        result.error = 1;
        return result;
    }
    if (input[i + 1] == u':' || input[i + 1] == u'|') {
        if (alpha(input[i])) {
            if (sep(input[i + 2])) {
                result.flags = 0x28100000ull | i;
                result.scheme = u"file";
                return result;
            }
            result.error = 7;
            return result;
        }
        result.error = input[i + 1] != u':' ? 1 : 2;
        return result;
    }
    if (sep(input[i])) {
        if (sep(input[i + 1])) {
            i += 2;
            while (i < input.size() && sep(input[i]))
                ++i;
            result.flags = 0x30100000ull | i;
            result.scheme = u"file";
            return result;
        }
        result.error = 1;
        return result;
    }
    if (colon == std::u16string_view::npos) {
        result.error = 1;
        return result;
    }
    auto scheme = input.substr(i, colon);
    bool valid = !scheme.empty() && alpha(scheme[0]);
    for (auto c : scheme)
        valid = valid && (alpha(c) || (c >= u'0' && c <= u'9') || c == u'+' || c == u'-' || c == u'.');
    if (!valid) {
        result.error = 2;
        return result;
    }
    if (scheme.size() > 1024) {
        result.error = 5;
        return result;
    }
    for (auto c : scheme)
        result.scheme += lower(c);
    result.flags = i + colon + 1;
    return result;
}

bool config_file_uri_implicit_kind_is_relative(const ConfigFileUriSchemeScan& scan,
                                               std::u16string_view original, std::int32_t kind,
                                               bool windows) {
    if (scan.error != 0 || (scan.flags & 0x20000000ull) == 0)
        return false;
    if ((scan.flags & 0x8000000ull) == 0 && kind != 1 &&
        (kind == 2 || (original.size() >= 2 && (original[0] != u'\\' || original[1] != u'\\')) ||
         (!windows && (scan.flags & 0x100000000000ull) != 0)))
        return true;
    return kind == 2 && (scan.flags & 0x8000000ull) != 0;
}
}
