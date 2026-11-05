// Invariant-culture behavior consumed by Nini; normal selected system ICU.
// Copyright (c) .NET Foundation and Contributors, MIT source license:
// ../third_party/dotnet_collation/LICENSE.
#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
namespace opensim::framework {
InvariantConfigCultureError::InvariantConfigCultureError(std::string t, std::int32_t h, std::u16string m,
                                                         std::optional<std::u16string> p)
    : runtime_error(utf8(m)), source_type(std::move(t)), hresult(h), message(std::move(m)),
      parameter(std::move(p)) {}
namespace {
[[noreturn]] void null(std::u16string parameter) {
    throw InvariantConfigCultureError("System.ArgumentNullException",
                                      std::bit_cast<std::int32_t>(0x80004003u),
                                      u"Value cannot be null. (Parameter '" + parameter + u"')", parameter);
}
}
std::int32_t invariant_config_compare(const NiniWriterText& a, const NiniWriterText& b) {
    if (a == b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    return invariant_compare_ignore_case_utf16(*a, *b);
}
bool invariant_config_equal(const NiniWriterText& a, const NiniWriterText& b) {
    return invariant_config_compare(a, b) == 0;
}
NiniWriterText invariant_config_lower(const NiniWriterText& input) {
    if (!input)
        null(u"str");
    if (input->empty())
        return nini_writer_text(std::u16string{});
    bool same = true;
    for (auto c : *input)
        if (c > 127 || (c >= u'A' && c <= u'Z')) {
            same = false;
            break;
        }
    if (same)
        return input;
    return std::make_shared<const std::u16string>(invariant_lower_utf16(*input));
}
std::int32_t invariant_config_index_of(const NiniWriterText& source, const NiniWriterText& value,
                                       std::int32_t start) {
    if (!source)
        null(u"source");
    if (!value)
        null(u"value");
    if (start < 0 || static_cast<std::size_t>(start) > source->size())
        throw InvariantConfigCultureError(
            "System.ArgumentOutOfRangeException", std::bit_cast<std::int32_t>(0x80131502u),
            u"Index was out of range. Must be non-negative and less than or equal to the size of the collection. (Parameter 'startIndex')",
            u"startIndex");
    auto result = invariant_index_of_grapheme_utf16(
        std::u16string_view(*source).substr(static_cast<std::size_t>(start)), *value);
    return result < 0 ? result : result + start;
}
InvariantConfigCulture make_invariant_config_culture() {
    return {invariant_config_equal, invariant_config_lower, invariant_config_index_of};
}
}
