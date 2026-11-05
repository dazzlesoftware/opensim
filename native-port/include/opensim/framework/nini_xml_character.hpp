// Derived from .NET runtime v8.0.31 System.Xml XmlCharType/ValidateNames.
// Copyright (c) .NET Foundation and Contributors. Licensed under the MIT license.
// License text: native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace opensim::framework {
// UTF16 code-unit predicates intentionally do not replace malformed surrogates.
std::uint8_t nini_xml_character_properties(char16_t) noexcept;
bool nini_xml_public_id_character(char16_t) noexcept;
std::ptrdiff_t nini_xml_invalid_whitespace(std::u16string_view) noexcept;
std::ptrdiff_t nini_xml_invalid_character_data(std::u16string_view) noexcept;
std::ptrdiff_t nini_xml_invalid_public_id(std::u16string_view) noexcept;
// These parse precisely the original BMP name alphabet. Supplementary scalar
// acceptance must follow the consuming reader's separate surrogate branch.
std::size_t nini_xml_parse_ncname(std::u16string_view,std::size_t offset=0) noexcept;
std::size_t nini_xml_parse_name(std::u16string_view,std::size_t offset=0) noexcept;
std::size_t nini_xml_parse_nmtoken(std::u16string_view,std::size_t offset=0,bool namespaces=true) noexcept;
std::size_t nini_xml_parse_qname(std::u16string_view,std::size_t offset,std::size_t& colon_offset) noexcept;
}
