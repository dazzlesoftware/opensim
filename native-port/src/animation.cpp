// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/animation.hpp"
#include <stdexcept>
namespace opensim::framework {
using network::Value;
Value Animation::pack()const{auto uuid=[](const Uuid& id){Value value(id.str());value.kind=Value::Kind::Uuid;return value;};auto data=Value::object({});data.set("animation",uuid(animation_id));data.set("object_id",uuid(object_id));data.set("seq_num",Value(sequence_number));return data;}
void Animation::unpack(const Value* input){if(!input||input->view().kind!=Value::Kind::Map)throw std::invalid_argument("Animation requires an OSD map");const auto& data=input->view();if(data.contains("animation"))animation_id=network::osd_uuid(data.get("animation"));if(data.contains("object_id"))object_id=network::osd_uuid(data.get("object_id"));if(data.contains("seq_num"))sequence_number=network::osd_integer(data.get("seq_num"));}
bool Animation::equals(const Animation* other)const noexcept{return other&&animation_id==other->animation_id&&sequence_number==other->sequence_number&&object_id==other->object_id;}
std::string Animation::str()const{return "AnimID= "+animation_id.str()+"/seq="+std::to_string(sequence_number)+"/objID="+object_id.str();}
}
