// Native XML URL source composition used by the startup configuration loader.
#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/nini_xml_resolver_policy.hpp"
namespace opensim::server {
std::function<std::shared_ptr<framework::NiniConfigSourceBase>(framework::NiniWriterText)>
    native_startup_xml_source(std::shared_ptr<framework::NiniConfigArena>, framework::ConfigXmlNativeHttpServices,
                              std::shared_ptr<framework::NiniXmlResolverPolicy> = {});
}
