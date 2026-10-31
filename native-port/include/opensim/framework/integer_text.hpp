// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <limits>
#include <string_view>
namespace opensim::framework {
template<class UInt>bool try_parse_unsigned_integer(std::string_view s,UInt& result){
    result=0;auto white=[](char c){return c==' '||(c>='\t'&&c<='\r');};
    while(!s.empty()&&white(s.front()))s.remove_prefix(1);
    bool minus=false;if(!s.empty()&&(s.front()=='+'||s.front()=='-')){minus=s.front()=='-';s.remove_prefix(1);}
    bool digit=false;UInt n=0;
    while(!s.empty()&&s.front()>='0'&&s.front()<='9'){
        auto v=static_cast<UInt>(s.front()-'0');if(n>(std::numeric_limits<UInt>::max()-v)/10)return false;
        digit=true;n=n*10+v;s.remove_prefix(1);
    }
    while(!s.empty()&&white(s.front()))s.remove_prefix(1);while(!s.empty()&&s.front()==0)s.remove_prefix(1);
    if(!digit||!s.empty()||(minus&&n))return false;result=n;return true;
}
}
