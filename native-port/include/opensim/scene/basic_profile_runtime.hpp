// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/profile_packets.hpp"
namespace opensim::scene {
// Binds the consumed Scene/IClientAPI event surfaces to admitted native sessions.
// Runtime teardown owns collection after clients and their source scene stop.
class BasicProfileRuntime {
  public:
    using Sender = std::function<void(framework::Uuid, framework::Uuid, network::lludp::Packet)>;
    BasicProfileRuntime(std::shared_ptr<BasicProfileModule>, std::shared_ptr<BasicProfileAccounts>,
                        framework::Uuid scope, Sender);
    ~BasicProfileRuntime();
    void new_client(framework::Uuid agent, framework::Uuid session);
    void request(framework::Uuid agent, framework::Uuid session, framework::Uuid avatar);
    void remove_client(framework::Uuid agent);
    void stop();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
