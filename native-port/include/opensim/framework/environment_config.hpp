// Util.MergeEnvironmentToConfig and its consumed bundled Nini.Env closure.
// Nini MIT license: third_party/nini/LICENSE; OpenSim BSD: LICENSE.txt.
#pragma once
#include "opensim/framework/nini_config.hpp"
#include <mutex>
#include "opensim/framework/runtime_environment.hpp"
namespace opensim::framework {
using EnvironmentConfigError = RuntimeEnvironmentError;
struct EnvironmentItem {
    const NiniWriterText name;
    NiniWriterText value;
};
class EnvironmentMap {
    struct State;
    std::shared_ptr<State> state_;

public:
    EnvironmentMap();
    std::int32_t count() const;
    std::shared_ptr<EnvironmentItem> item(std::int32_t) const;
    void append(NiniWriterText key, std::shared_ptr<EnvironmentItem>);
    void append(NiniWriterText key, NiniWriterText value);
};
struct EnvironmentConfigBackend {
    std::function<NiniWriterText(NiniWriterText)> get;
    std::function<void(NiniWriterText, NiniWriterText)> set;
    std::shared_ptr<EnvironmentMap> map;
};
NiniWriterText native_environment_get(NiniWriterText variable);
void native_environment_set(NiniWriterText variable, NiniWriterText value);
// Default map is process-persistent, just like EnvMap.Instance. Explicit maps
// provide isolated hosts/tests; no sorting or synthetic OS dictionary order.
std::shared_ptr<EnvironmentConfigBackend> native_environment_config_backend();
class EnvironmentConfigSource;
class EnvironmentConfig final : public NiniConfig {
    EnvironmentConfigSource* parent_;
    friend class EnvironmentConfigSource;
    EnvironmentConfig(NiniConfigArena*, NiniWriterText, EnvironmentConfigSource*);

public:
    NiniWriterText managed_type() const override;
    void set(NiniWriterText, std::shared_ptr<NiniConfigObject>) override;
};
class EnvironmentConfigSource final : public NiniConfigSourceBase {
    std::shared_ptr<EnvironmentConfigBackend> backend_;
    bool case_sensitive_ = true;
    EnvironmentConfigSource(NiniConfigArena*, std::shared_ptr<EnvironmentConfigBackend>);

public:
    static std::shared_ptr<EnvironmentConfigSource> create(std::shared_ptr<NiniConfigArena>,
                                                           std::shared_ptr<EnvironmentConfigBackend>);
    bool case_sensitive() const {
        return case_sensitive_;
    }
    void case_sensitive(bool value) {
        case_sensitive_ = value;
    }
    void add_env(NiniWriterText key, NiniWriterText default_value);
    void load_env();
};
void merge_environment_to_config(std::shared_ptr<NiniConfigSourceBase>,
                                 std::shared_ptr<EnvironmentConfigBackend> backend = {});
}
