// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/rest_forms.hpp"
#include <iostream>
int main(int argc,char** argv){try{std::optional<std::pair<std::string,std::string>> auth;if(argc>3)auth={{"service-user","service-password"}};auto result=opensim::network::rest_forms_transport(auth)(argv[1],argc>2?argv[2]:"METHOD=probe");std::string hex;for(unsigned char c:result){hex+="0123456789abcdef"[c>>4];hex+="0123456789abcdef"[c&15];}std::cout<<"OK|"<<hex<<std::endl;}catch(...){std::cout<<"EX"<<std::endl;}return 0;}
