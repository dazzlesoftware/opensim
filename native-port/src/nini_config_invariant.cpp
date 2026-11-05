// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
namespace opensim::framework {
namespace {
template <class T, class F> std::function<T(const NiniWriterText&)> number(F conversion) {
    return [conversion](const NiniWriterText& value) {
        try {
            return conversion(value ? std::optional<std::u16string>(*value) : std::nullopt);
        } catch (const InvariantNumberError& e) {
            throw NiniConfigError(e.kind == InvariantNumberError::Kind::Format
                                      ? NiniConfigError::Kind::Format
                                      : NiniConfigError::Kind::Overflow,
                                  e.message);
        }
    };
}
[[noreturn]] void culture_error(const InvariantConfigCultureError& e) {
    auto kind = NiniConfigError::Kind::Exception;
    if (e.source_type == "System.ArgumentNullException")
        kind = NiniConfigError::Kind::ArgumentNull;
    else if (e.source_type == "System.NullReferenceException")
        kind = NiniConfigError::Kind::NullReference;
    else if (e.source_type == "System.ArgumentOutOfRangeException")
        kind = NiniConfigError::Kind::OutOfRange;
    else if (e.source_type == "System.ArgumentException")
        kind = NiniConfigError::Kind::Argument;
    else
        throw e;
    throw NiniConfigError(kind, e.message, e.parameter);
}
}
std::shared_ptr<NiniConfigBackend> native_nini_invariant_config_backend(NiniFileStreamProvider file_stream) {
    if (!file_stream)
        file_stream = native_nini_file_stream_provider();
    auto backend = std::make_shared<NiniConfigBackend>();
    auto format = std::make_shared<NiniNumberFormat>();
    format->to_int32 = number<std::int32_t>(invariant_convert_to_int32);
    format->to_int64 = number<std::int64_t>(invariant_convert_to_int64);
    format->to_single = number<float>(invariant_convert_to_single);
    format->to_double = number<double>(invariant_convert_to_double);
    backend->capture_number_format = [format] { return format; };
    backend->insensitive_table = [] {
        auto table = std::make_shared<NiniInsensitiveTable>();
        table->equal = [](const auto& first, const auto& second) {
            try {
                return invariant_config_equal(first, second);
            } catch (const InvariantConfigCultureError& e) {
                culture_error(e);
            }
        };
        return table;
    };
    backend->lower_current = [](const auto& value) {
        try {
            return invariant_config_lower(value);
        } catch (const InvariantConfigCultureError& e) {
            culture_error(e);
        }
    };
    backend->index_of_current = [](const auto& text, const auto& value, std::int32_t start) {
        try {
            return invariant_config_index_of(text, value, start);
        } catch (const InvariantConfigCultureError& e) {
            culture_error(e);
        }
    };
    backend->document = nini_byte_document_factory(std::move(file_stream));
    backend->file_reader = backend->document.file_reader;
    backend->stream_reader = backend->document.stream_reader;
    return backend;
}
}
