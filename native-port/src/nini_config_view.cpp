// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/nini_config_view.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::server {
namespace {
framework::NiniWriterText text(const std::string& value) {
    return framework::nini_writer_text(framework::utf16_replacing(value));
}
std::shared_ptr<framework::NiniConfigSourceBase> empty_source() {
    auto arena =
        std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    return arena->create_ini_source();
}
}
NiniConfigView::NiniConfigView() : source_(empty_source()) {
}
NiniConfigView::NiniConfigView(std::shared_ptr<framework::NiniConfigSourceBase> source)
    : source_(std::move(source)) {
    if (!source_)
        throw std::invalid_argument("Null retained Nini configuration source");
}
NiniConfigView NiniConfigView::load_raw(const std::filesystem::path& path) {
    NiniConfigView result;
    std::static_pointer_cast<framework::NiniIniConfigSource>(result.source_)
        ->load(framework::nini_writer_text(path.u16string()));
    return result;
}
std::shared_ptr<framework::NiniConfig> NiniConfigView::native_section(const std::string& name) const {
    return source_->configs()->at(text(name));
}
bool NiniConfigView::has_section(const std::string& name) const {
    return bool(native_section(name));
}
bool NiniConfigView::contains(const std::string& name, const std::string& key) const {
    auto config = native_section(name);
    return config && config->contains(text(key));
}
std::string NiniConfigView::get(const std::string& name, const std::string& key,
                                const std::string& fallback) const {
    auto config = native_section(name);
    if (!config)
        return fallback;
    auto value = config->get(text(key));
    return value ? framework::utf8(*value) : fallback;
}
std::int32_t NiniConfigView::integer(const std::string& name, const std::string& key,
                                     std::int32_t fallback) const {
    auto config = native_section(name);
    return config ? config->get_int(text(key), fallback) : fallback;
}
bool NiniConfigView::boolean(const std::string& name, const std::string& key, bool fallback) const {
    auto config = native_section(name);
    return config ? config->get_boolean(text(key), fallback) : fallback;
}
void NiniConfigView::set(const std::string& name, const std::string& key, const std::string& value) {
    auto config = native_section(name);
    if (!config)
        config = source_->add_config(text(name));
    config->set_text(text(key), text(value));
}
std::vector<std::pair<std::string, std::string>>
NiniConfigView::ordered_section(const std::string& name) const {
    std::vector<std::pair<std::string, std::string>> result;
    auto config = native_section(name);
    if (config)
        for (const auto& key : config->get_keys()) {
            auto value = config->get(key);
            result.emplace_back(framework::utf8(*key), value ? framework::utf8(*value) : std::string{});
        }
    return result;
}
std::map<std::string, std::string> NiniConfigView::section(const std::string& name) const {
    auto ordered = ordered_section(name);
    return {ordered.begin(), ordered.end()};
}
void NiniConfigView::replace_key_values() {
    source_->replace_key_values();
}
}
