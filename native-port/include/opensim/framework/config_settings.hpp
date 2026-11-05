// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_writer.hpp"
namespace opensim::framework {
struct ConfigSettings {
 NiniWriterText PhysicsEngine,MeshEngineName,ClientstackDll,LibrariesXMLFile;
 static constexpr std::uint32_t DefaultRegionHttpPort=9000;
};
}
