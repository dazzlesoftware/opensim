// Shared native text-stream structured failures.
#pragma once
#include "opensim/framework/nini_writer.hpp"
namespace opensim::framework {
class NiniStreamError : public std::runtime_error {
public:
    NiniStreamError(std::string type, std::int32_t hresult, std::u16string message,
                    std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::int32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter;
    std::optional<std::int32_t> character_index;
    std::optional<char16_t> unknown_character;
    std::optional<std::u16string> file_name, object_name;
};
}
