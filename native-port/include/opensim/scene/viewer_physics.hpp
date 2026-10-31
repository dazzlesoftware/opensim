// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/default_animations.hpp"
namespace opensim::scene {
// Host-thread composition of source controls, animator and actual ubODE.
// Packet callbacks enqueue commands; no native engine calls run on UDP threads.
class ViewerPhysics {
public:
 using Publish=std::function<void(const ViewerAgent&)>;
 using Log=std::function<void(const std::string&)>;
 ViewerPhysics(World&,ViewerRegion&,const std::filesystem::path& library,
  std::shared_ptr<const DefaultAnimations>,Publish,Log={});
 ~ViewerPhysics();
 void entered(const ViewerAgent&);
 void input(const ViewerAgent&);
 void running(const ViewerAgent&);
 void left(const ViewerAgent&);
 void poll();
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
