// Source-derived .NET8 SafeFileHandle/AppContextConfigHelper, MIT.
// See third_party/dotnet_collation/LICENSE.
#include "opensim/framework/file_stream_policy.hpp"
#include <mutex>
namespace opensim::framework {
namespace {
struct Policy {
    std::mutex mutex;
    std::optional<bool> value, switch_value;
};
Policy& policy() {
    static Policy value;
    return value;
}
}
bool file_stream_boolean_config_value(const RuntimeEnvironmentText& text) noexcept {
    if (!text)
        return false;
    if (*text == u"1")
        return true;
    if (text->size() != 4)
        return false;
    constexpr char16_t expected[] = u"true";
    for (std::size_t i = 0; i < 4; ++i) {
        auto c = (*text)[i];
        if (c >= u'A' && c <= u'Z')
            c += u'a' - u'A';
        if (c != expected[i])
            return false;
    }
    return true;
}
void set_file_stream_disable_locking_switch(bool value) {
    auto& state = policy();
    std::lock_guard lock(state.mutex);
    state.switch_value = value;
}
bool file_stream_disable_locking(std::optional<bool> value) {
    if (value)
        return *value;
    auto& state = policy();
    std::lock_guard lock(state.mutex);
    if (!state.value) {
        if (state.switch_value)
            state.value = *state.switch_value;
        else {
            static const auto name =
                std::make_shared<const std::u16string>(u"DOTNET_SYSTEM_IO_DISABLEFILELOCKING");
            state.value = file_stream_boolean_config_value(runtime_environment_get(name));
        }
    }
    return *state.value;
}
}
