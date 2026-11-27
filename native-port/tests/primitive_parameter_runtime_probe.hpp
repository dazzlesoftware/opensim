// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/region_runtime.hpp"

class PrimitiveErrorViewer {
  public:
    PrimitiveErrorViewer();
    ~PrimitiveErrorViewer();
    opensim::network::UdpEndpoint start();
    bool saw_debug(opensim::framework::Uuid source, opensim::framework::Vector3 position, std::string_view text) const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

void verify_primitive_parameter_runtime(
    opensim::scene::RegionRuntime &runtime,
    const std::function<void(std::shared_ptr<opensim::framework::AssetBase>)> &store,
    const PrimitiveErrorViewer &viewer);
