// .NET Runtime v8.0.31 RegexCaseEquivalences; MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
namespace opensim::framework {
enum class DotnetRegexCaseBehavior:std::int32_t {NotSet,Invariant,NonTurkish,Turkish};
// Preserve the virtual CultureInfo.Name getter, including lazy evaluation and
// the two distinct getter calls for a nonempty first result.
using DotnetRegexCultureName=std::function<std::u16string()>;
DotnetRegexCaseBehavior dotnet_regex_case_behavior(const DotnetRegexCultureName&);
// An empty span is the source false/default result. Returned storage is static.
std::span<const char16_t> dotnet_regex_case_equivalences(char16_t,const DotnetRegexCultureName&,DotnetRegexCaseBehavior&);
}
