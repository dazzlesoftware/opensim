// Copyright (c) .NET Foundation and Contributors.
// Port of .NET Runtime v8.0.31 RegexCaseEquivalences; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_case.hpp"
namespace opensim::framework {
namespace {
#include "dotnet_regex_case_data.inc"
constexpr char16_t invariant_i[] = {u'I', u'i'};
constexpr char16_t non_turkish_i[] = {u'I', u'i', u'\u0130'};
constexpr char16_t turkish_upper_i[] = {u'I', u'\u0131'};
constexpr char16_t turkish_lower_i[] = {u'i', u'\u0130'};
}
DotnetRegexCaseBehavior dotnet_regex_case_behavior(const DotnetRegexCultureName& get_name) {
    if (get_name().empty())
        return DotnetRegexCaseBehavior::Invariant;
    const auto name = get_name();
    if (name.size() >= 2 && ((name[0] == u't' && name[1] == u'r') || (name[0] == u'a' && name[1] == u'z')) &&
        (name.size() == 2 || name[2] == u'-'))
        return DotnetRegexCaseBehavior::Turkish;
    return DotnetRegexCaseBehavior::NonTurkish;
}
std::span<const char16_t> dotnet_regex_case_equivalences(char16_t c, const DotnetRegexCultureName& name,
                                                         DotnetRegexCaseBehavior& behavior) {
    if (c == u'I' || c == u'i' || c == u'\u0130' || c == u'\u0131') {
        if (behavior == DotnetRegexCaseBehavior::NotSet)
            behavior = dotnet_regex_case_behavior(name);
        if (behavior == DotnetRegexCaseBehavior::Invariant)
            return c == u'I' || c == u'i' ? std::span<const char16_t>(invariant_i)
                                          : std::span<const char16_t>{};
        if (behavior == DotnetRegexCaseBehavior::NonTurkish)
            return c == u'\u0131' ? std::span<const char16_t>{} : std::span<const char16_t>(non_turkish_i);
        if (behavior == DotnetRegexCaseBehavior::Turkish)
            return c == u'I' || c == u'\u0131' ? std::span<const char16_t>(turkish_upper_i)
                                               : std::span<const char16_t>(turkish_lower_i);
        return {};
    }
    const auto first = EquivalenceFirstLevelLookup[static_cast<std::uint16_t>(c) / 1024];
    if (first == 0xffff)
        return {};
    const auto entry = EquivalenceCasingMap[static_cast<std::uint16_t>(c) % 1024 + first];
    if (entry == 0xffff)
        return {};
    return {EquivalenceCasingValues + (entry & 0x1fff), static_cast<std::size_t>((entry >> 13) & 7)};
}
}
