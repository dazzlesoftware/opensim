// Source .NET8.0.31 Uri.GetCanonicalPath, built-in file syntax. MIT license.
#pragma once
#include "opensim/framework/config_file_uri_state.hpp"
namespace opensim::framework {
std::u16string config_file_uri_canonical_path(ConfigFileUriState&,std::u16string destination,std::int32_t format,bool windows);
}
