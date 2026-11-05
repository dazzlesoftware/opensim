// Copyright (c) Contributors, http://opensimulator.org/
// Source-derived .NET Runtime v8.0.31 FileSystemName closure, MIT license.
// See third_party/dotnet_collation/LICENSE.
#pragma once
#include <memory>
#include <functional>
#include <string>
#include <string_view>
namespace opensim::framework::file_system_name {
enum class CasingMode { Normal, GlobalizationInvariant };
// Bind a different selected ICU/NLS runtime through raw UTF16 callbacks when
// its casing differs from the pinned profiles. No UTF8 replacement is allowed.
struct CasingBackend {
 std::function<char16_t(char16_t)> upper_invariant;
 std::function<bool(std::u16string_view,std::u16string_view)> ordinal_ignore_case_ends_with;
};
const CasingBackend& pinned_case_backend(CasingMode mode=CasingMode::Normal);
using String=std::shared_ptr<const std::u16string>;
// Represents the source interned "*" literal, including reference identity.
String star_literal();
// Null input is allowed. Unmodified ordinary expressions retain their identity;
// null/empty/*/*.* return star_literal(), as does the source string API.
String translate_win32_expression(const String& expression);
// Empty spans also represent nullable .NET strings converted to ReadOnlySpan.
// Both methods operate on raw UTF16, retaining NUL and invalid surrogates.
bool matches_win32_expression(std::u16string_view expression,std::u16string_view name,bool ignore_case,const CasingBackend&);
bool matches_simple_expression(std::u16string_view expression,std::u16string_view name,bool ignore_case,const CasingBackend&);
bool matches_win32_expression(std::u16string_view expression,std::u16string_view name,bool ignore_case=true,CasingMode mode=CasingMode::Normal);
bool matches_simple_expression(std::u16string_view expression,std::u16string_view name,bool ignore_case=true,CasingMode mode=CasingMode::Normal);
}
