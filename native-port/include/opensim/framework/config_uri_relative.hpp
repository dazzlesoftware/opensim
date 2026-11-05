// Creates an already-validated relative System.Uri value. Constructor parsing
// owns the distinction between OriginalString and the IRI parse-string.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
namespace opensim::framework {
ConfigUriObjectPtr config_uri_relative_object(ConfigUriText original,ConfigUriText parse_string);
}
