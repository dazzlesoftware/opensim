// Port of bundled Nini.Util.ArgvParser and Nini.Config.ArgvConfigSource.
// Nini MIT license: third_party/nini/LICENSE.
#pragma once
#include "opensim/framework/nini_config.hpp"
namespace opensim::framework {
// CLR array: fixed length, mutable element references, retained by source.
class NiniArgvArgumentArray {
    std::vector<NiniWriterText> values_;

public:
    explicit NiniArgvArgumentArray(std::vector<NiniWriterText> values = {}) : values_(std::move(values)) {}
    std::size_t size() const noexcept {
        return values_.size();
    }
    NiniWriterText& operator[](std::size_t index) {
        if (index >= values_.size())
            throw NiniConfigError(NiniConfigError::Kind::IndexOutOfRange,
                                  u"Index was outside the bounds of the array.");
        return values_[index];
    }
    const NiniWriterText& operator[](std::size_t index) const {
        if (index >= values_.size())
            throw NiniConfigError(NiniConfigError::Kind::IndexOutOfRange,
                                  u"Index was outside the bounds of the array.");
        return values_[index];
    }
    auto begin() const {
        return values_.begin();
    }
    auto end() const {
        return values_.end();
    }
};
using NiniArgvArguments = std::shared_ptr<NiniArgvArgumentArray>;
struct NiniArgvBackend {
    // Regex default Unicode character classes operate on UTF-16 code units.
    std::function<bool(char16_t)> regex_word, regex_space;
    // StringDictionary uses ToLowerInvariant then ordinal key equality.
    std::function<NiniWriterText(const NiniWriterText&)> lower_invariant;
};
class NiniArgvParser {
    std::shared_ptr<NiniArgvBackend> backend_;
    std::optional<std::vector<std::pair<NiniWriterText, NiniWriterText>>> parameters_;
    NiniWriterText key(NiniWriterText) const;

public:
    NiniArgvParser(NiniArgvArguments, std::shared_ptr<NiniArgvBackend>);
    // Source defect preserved: tokenizes but never initializes parameters.
    NiniArgvParser(NiniWriterText, std::shared_ptr<NiniArgvBackend>);
    NiniWriterText get(NiniWriterText) const;
};
class NiniArgvConfigSource : public NiniConfigSourceBase {
    NiniArgvParser parser_;
    NiniArgvArguments arguments_;
    NiniArgvConfigSource(NiniConfigArena*, NiniArgvArguments, std::shared_ptr<NiniArgvBackend>);

public:
    static std::shared_ptr<NiniArgvConfigSource> create(std::shared_ptr<NiniConfigArena>, NiniArgvArguments,
                                                        std::shared_ptr<NiniArgvBackend>);
    void save() override;
    void reload() override;
    void add_switch(NiniWriterText config, NiniWriterText long_name, NiniWriterText short_name = {});
    NiniArgvArguments get_arguments() const;
};
// Explicit .NET8 Unicode regex version, selected native ICU invariant casing.
std::shared_ptr<NiniArgvBackend> native_nini_argv_dotnet8_backend();
// Application.cs consumed alias/switch setup, including early logconfig switch.
void setup_opensim_application_argv(const std::shared_ptr<NiniArgvConfigSource>&);
}
