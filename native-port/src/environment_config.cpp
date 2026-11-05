// Source-derived Util.MergeEnvironmentToConfig / Nini.Env EnvMap/EnvAdapter.
// OpenSim BSD LICENSE.txt; bundled Nini MIT third_party/nini/LICENSE.
#include "opensim/framework/environment_config.hpp"
namespace opensim::framework {
namespace {
using Text = NiniWriterText;
Text text(std::u16string value) {
    return nini_writer_text(std::move(value));
}
Text environment_literal() {
    static const auto value = text(u"Environment");
    return value;
}
Text empty_literal() {
    return runtime_environment_empty_text();
}
[[noreturn]] void nullref() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
}
struct EnvironmentMap::State {
    mutable std::mutex mutex;
    std::vector<std::pair<Text, std::shared_ptr<EnvironmentItem>>> items;
};
EnvironmentMap::EnvironmentMap() : state_(std::make_shared<State>()) {
}
std::int32_t EnvironmentMap::count() const {
    std::lock_guard guard(state_->mutex);
    return static_cast<std::int32_t>(state_->items.size());
}
std::shared_ptr<EnvironmentItem> EnvironmentMap::item(std::int32_t index) const {
    std::lock_guard guard(state_->mutex);
    if (index < 0 || static_cast<std::size_t>(index) >= state_->items.size())
        throw NiniConfigError(
            NiniConfigError::Kind::OutOfRange,
            u"Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')",
            u"index");
    return state_->items[static_cast<std::size_t>(index)].second;
}
void EnvironmentMap::append(Text key, std::shared_ptr<EnvironmentItem> value) {
    if (!key)
        throw NiniConfigError(NiniConfigError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'key')",
                              u"key");
    std::lock_guard guard(state_->mutex);
    for (auto& old : state_->items)
        if (*old.first == *key)
            throw NiniConfigError(NiniConfigError::Kind::Argument,
                                  u"Item has already been added. Key in dictionary: '" + *key +
                                      u"'  Key being added: '" + *key + u"'");
    state_->items.emplace_back(std::move(key), std::move(value));
}
void EnvironmentMap::append(Text key, Text value) {
    append(key, std::make_shared<EnvironmentItem>(EnvironmentItem{key, std::move(value)}));
}
Text native_environment_get(Text variable) {
    return runtime_environment_get(std::move(variable));
}
void native_environment_set(Text variable, Text value) {
    runtime_environment_set(std::move(variable), std::move(value));
}
std::shared_ptr<EnvironmentConfigBackend> native_environment_config_backend() {
    static const auto backend = []() {
        auto result = std::make_shared<EnvironmentConfigBackend>();
        result->get = native_environment_get;
        result->set = native_environment_set;
        result->map = std::make_shared<EnvironmentMap>();
        return result;
    }();
    return backend;
}
EnvironmentConfig::EnvironmentConfig(NiniConfigArena* a, Text name, EnvironmentConfigSource* source)
    : NiniConfig(a, std::move(name), source, false), parent_(source) {
}
Text EnvironmentConfig::managed_type() const {
    static const auto name = text(u"Nini.Config.EnvConfig");
    return name;
}
void EnvironmentConfig::set(Text key, std::shared_ptr<NiniConfigObject> value) {
    if (!parent_)
        nullref();
    if (!parent_->case_sensitive())
        key = insensitive_key(std::move(key));
    NiniConfig::set(std::move(key), std::move(value));
}
EnvironmentConfigSource::EnvironmentConfigSource(NiniConfigArena* a,
                                                 std::shared_ptr<EnvironmentConfigBackend> backend)
    : NiniConfigSourceBase(a, text(u"Nini.Config.EnvConfigSource")), backend_(std::move(backend)) {
}
std::shared_ptr<EnvironmentConfigSource>
EnvironmentConfigSource::create(std::shared_ptr<NiniConfigArena> arena,
                                std::shared_ptr<EnvironmentConfigBackend> backend) {
    if (!arena || !backend || !backend->map || !backend->get || !backend->set)
        throw std::invalid_argument("Environment config requires arena, process map and get/set providers");
    auto value = std::unique_ptr<EnvironmentConfigSource>(
        new EnvironmentConfigSource(arena.get(), std::move(backend)));
    auto pointer = value.get();
    arena->adopt(std::move(value));
    return arena->handle(pointer);
}
void EnvironmentConfigSource::add_env(Text key, Text default_value) {
    auto value = backend_->get(key);
    if (!value) {
        backend_->set(key, default_value);
        value = default_value;
    }
    backend_->map->append(std::move(key), std::move(value));
}
void EnvironmentConfigSource::load_env() {
    configs()->clear();
    auto value =
        std::unique_ptr<EnvironmentConfig>(new EnvironmentConfig(arena_, environment_literal(), this));
    auto pointer = value.get();
    arena_->adopt(std::move(value));
    auto config = arena_->handle(pointer);
    configs()->add(config);
    for (std::int32_t i = 0; i < backend_->map->count(); ++i) {
        auto item = backend_->map->item(i);
        if (!item)
            nullref();
        if (item->name)
            config->add(item->name, item->value);
    }
}
void merge_environment_to_config(std::shared_ptr<NiniConfigSourceBase> source,
                                 std::shared_ptr<EnvironmentConfigBackend> backend) {
    if (!source)
        nullref();
    auto environment = source->configs()->at(environment_literal());
    if (!environment)
        return;
    if (!backend)
        backend = native_environment_config_backend();
    auto env = EnvironmentConfigSource::create(source->arena()->shared_from_this(), std::move(backend));
    auto keys = environment->get_keys();
    for (auto& key : keys)
        env->add_env(key, empty_literal());
    env->load_env();
    source->merge(env);
    source->expand_key_values();
}
}
