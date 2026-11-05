#include "opensim/framework/config_uri_relative.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
[[noreturn]] ConfigUriText not_absolute() {
    throw ConfigUriComponentsError("System.InvalidOperationException",
                                   u"This operation is not supported for a relative URI.", 0x80131509);
}
class Relative final : public ConfigUriObject {
    ConfigUriText original_, parse_;

  public:
    Relative(ConfigUriText original, ConfigUriText parse)
        : original_(std::move(original)), parse_(std::move(parse)) {}
    bool is_absolute() const override {
        return false;
    }
    ConfigUriText original_string() const override {
        return original_;
    }
    ConfigUriText scheme() const override {
        return not_absolute();
    }
    ConfigUriText fragment() const override {
        return not_absolute();
    }
    ConfigUriText absolute_uri() const override {
        return not_absolute();
    }
    ConfigUriText to_string() const override {
        return parse_;
    }
    ConfigUriText local_path() const override {
        return not_absolute();
    }
};
}
ConfigUriObjectPtr config_uri_relative_object(ConfigUriText original, ConfigUriText parse) {
    if (!original || !parse)
        throw std::invalid_argument("Validated relative URI requires original and parse strings");
    return std::make_shared<Relative>(std::move(original), std::move(parse));
}
}
