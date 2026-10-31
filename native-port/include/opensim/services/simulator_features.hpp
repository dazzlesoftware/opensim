// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/scene_capability_events.hpp"
#include "opensim/server/seed_caps.hpp"
namespace opensim::services {
struct SimulatorFeatureValue;
using FeatureData=std::shared_ptr<SimulatorFeatureValue>;
struct SimulatorFeatureValue {
    enum class WireKind {Normal,RawXml,Utf8};
    WireKind wire_kind=WireKind::Normal;
    network::Value scalar;
    framework::OrderedDictionary<std::string,FeatureData> map;
    std::vector<FeatureData> array;
    static FeatureData from_value(const network::Value& value);
    static FeatureData object();
    static FeatureData list();
    static FeatureData raw_xml(const std::optional<std::string>& text);
    static FeatureData utf8(server::PollBytes bytes);
};
server::PollBytes simulator_feature_xml(const FeatureData& value);
class SimulatorSyntaxReader {
public:virtual ~SimulatorSyntaxReader()=default;virtual std::optional<std::string> read_line()=0;virtual void dispose()=0;
};
struct SimulatorSyntaxState {
    std::recursive_mutex mutex;framework::Uuid id;server::PollBytes xml;
};
class SimulatorFeaturesModule;
class SimulatorGridInfo {
public:virtual ~SimulatorGridInfo()=default;virtual void set(const std::string& field,const std::optional<std::string>& value)=0;virtual void aliases(std::vector<std::string> values)=0;
};
using SimulatorGridExtras=framework::OrderedDictionary<std::string,std::any>;
class SimulatorFeaturesScene {
public:
    virtual ~SimulatorFeaturesScene()=default;
    virtual SceneCapabilityEvents& events()=0;
    virtual void register_module(SimulatorFeaturesModule* module)=0;
    virtual std::shared_ptr<SimulatorGridExtras> extra_features()=0;
    virtual std::shared_ptr<SimulatorGridInfo> grid_info()=0;
};
class SimulatorFeaturesBackend {
public:
    virtual ~SimulatorFeaturesBackend()=default;
    virtual framework::Uuid random_uuid(){return framework::Uuid::random();}
    virtual bool syntax_exists();
    virtual std::shared_ptr<SimulatorSyntaxReader> open_syntax();
    virtual void error(const std::string& text)=0;
    virtual void warn(const std::string& text)=0;
};
struct SimulatorFeaturesResponse {int status=200;server::PollBytes raw_buffer;};
class SimulatorFeaturesModule:public std::enable_shared_from_this<SimulatorFeaturesModule> {
public:
    using CallbackEvent=SceneCapabilityEvent<framework::Uuid,FeatureData&>;
    explicit SimulatorFeaturesModule(std::shared_ptr<SimulatorFeaturesBackend> backend,std::shared_ptr<SimulatorSyntaxState> state={});
    static constexpr std::string_view name="SimulatorFeaturesModule";
    std::nullptr_t replaceable_interface()const{return nullptr;}
    CallbackEvent on_request;
    void initialise(const server::IniConfig* source);
    void add_region(std::shared_ptr<SimulatorFeaturesScene> scene);
    void remove_region(std::shared_ptr<SimulatorFeaturesScene> scene);
    void region_loaded(std::shared_ptr<SimulatorFeaturesScene> scene);
    void close(){}
    void register_caps(framework::Uuid agent,std::shared_ptr<Caps> caps);
    void add_feature(const std::optional<std::string>& name,FeatureData value);
    void add_extra(const std::optional<std::string>& name,FeatureData value);
    bool remove_feature(const std::optional<std::string>& name);
    bool try_feature(const std::optional<std::string>& name,FeatureData& value);
    bool try_extra(const std::optional<std::string>& name,FeatureData& value);
    bool extra_contains(const std::optional<std::string>& name);
    FeatureData get_features();
    void process(const network::HttpRequest& request,SimulatorFeaturesResponse& response,std::shared_ptr<Caps> caps);
    void process_syntax(const network::HttpRequest& request,SimulatorFeaturesResponse& response);
private:
    friend struct SimulatorFeaturesProbe;
    void defaults();
    void read_syntax();
    FeatureData deep_copy();
    std::shared_ptr<SimulatorFeaturesBackend> backend_;
    std::shared_ptr<SimulatorSyntaxState> syntax_;
    std::shared_ptr<SimulatorFeaturesScene> scene_;
    std::recursive_mutex features_mutex_,scene_mutex_;
    FeatureData features_=SimulatorFeatureValue::object();
    bool export_=false,do_syntax_=true;
};
}
