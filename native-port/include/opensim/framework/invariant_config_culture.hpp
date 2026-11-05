// Invariant-culture string providers consumed by bundled Nini configuration.
// .NET source-derived behavior, MIT: third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_writer.hpp"
namespace opensim::framework {
class InvariantConfigCultureError : public std::runtime_error {
  public:
    InvariantConfigCultureError(std::string source_type, std::int32_t hresult, std::u16string message,
                                std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::int32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter;
};
// Captures invariant comparison independently from subsequent current culture.
// This is the consumed nullable-string comparer/equality surface, not Hashtable
// hashing/enumeration, arbitrary object comparison, NLS or invariant-mode casing.
std::int32_t invariant_config_compare(const NiniWriterText&, const NiniWriterText&);
bool invariant_config_equal(const NiniWriterText&, const NiniWriterText&);
NiniWriterText invariant_config_lower(const NiniWriterText&);
std::int32_t invariant_config_index_of(const NiniWriterText& source, const NiniWriterText& value,
                                       std::int32_t start_index);
struct InvariantConfigCulture {
    std::function<bool(const NiniWriterText&, const NiniWriterText&)> insensitive_equal;
    std::function<NiniWriterText(const NiniWriterText&)> lower;
    std::function<std::int32_t(const NiniWriterText&, const NiniWriterText&, std::int32_t)> index_of;
};
InvariantConfigCulture make_invariant_config_culture();
}
