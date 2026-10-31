// SDK compatibility subset; see protocol/LIBOPENMETAVERSE-LICENSE.txt.
#pragma once
#include <string>
#include <string_view>
namespace opensim::framework {
struct Quaternion {float x=0,y=0,z=0,w=0;bool operator==(const Quaternion&)const=default;};
std::string quaternion_string(const Quaternion& value);
bool parse_quaternion(std::string_view text,Quaternion& value);
}
