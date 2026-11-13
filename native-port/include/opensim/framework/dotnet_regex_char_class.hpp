// .NET8 RegexCharClass matching kernel. MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace opensim::framework {
class DotnetRegexClassError : public std::runtime_error {
  public:
    DotnetRegexClassError(std::string type, std::uint32_t code, std::string message)
        : runtime_error(message), source_type(std::move(type)), hresult(code) {}
    std::string source_type;
    std::uint32_t hresult;
};
std::uint8_t dotnet_regex_unicode_category(char16_t);
bool dotnet_regex_ecma_word_char(char16_t);
bool dotnet_regex_word_char(char16_t);
bool dotnet_regex_boundary_word_char(char16_t);
// Input is the original serialized RegexCharClass, including optional nested
// subtraction. This is the matching kernel, not the regex/class syntax parser.
bool dotnet_regex_char_in_class(char16_t, std::u16string_view);
bool dotnet_regex_char_in_category(char16_t, std::u16string_view);
using DotnetRegexAsciiCache = std::vector<std::atomic<std::uint32_t>>;
using DotnetRegexAsciiCacheSlot = opensim::framework::AtomicSharedPtr<DotnetRegexAsciiCache>;
bool dotnet_regex_char_in_class(char16_t, std::u16string_view, DotnetRegexAsciiCacheSlot&);
}
