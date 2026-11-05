// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/configuration_loader.hpp"
#include "opensim/framework/environment_config.hpp"
namespace opensim::framework {
NiniWriterText native_configuration_exception_message(std::exception_ptr);
// Complete the fixed-startup-invariant URI/prefix/environment/network/expansion
// dependencies of native_local_region_configuration_backend. The host must
// still provide its config directory, log/exit policy and real XML URI source;
// this helper never substitutes a no-op for an unavailable remote source.
void bind_native_startup_configuration_services(RegionConfigurationBackend&,std::shared_ptr<EnvironmentConfigBackend> environment={});
}
