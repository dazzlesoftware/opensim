#include "opensim/framework/texture_entry.hpp"
#include <bit>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim::framework;
int main(){
    TextureEntry entry(TextureEntry::white_texture);
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream in(line);int index,op;std::uint32_t bits;in>>index>>op>>std::hex>>bits;
        float value=std::bit_cast<float>(bits);
        auto face=index<0?entry.default_texture:entry.get_face(static_cast<unsigned>(index));
        switch(op){
        case 0:face->repeat_u(value);break;case 1:face->repeat_v(value);break;
        case 2:face->offset_u(value);break;case 3:face->offset_v(value);break;
        case 4:face->rotation(value);break;case 5:face->glow(value);break;
        case 6:face->bump(static_cast<std::uint8_t>(bits));break;
        case 7:face->shiny(static_cast<std::uint8_t>(bits));break;
        case 8:face->fullbright(bits!=0);break;case 9:face->media_flags(bits!=0);break;
        case 10:face->mapping(static_cast<std::uint8_t>(bits));break;
        case 11:face->color({value,value,value,value});break;
        case 12:entry.dirty_flags(index,bits);break;
        case 13:entry=TextureEntry(*face);break;
        case 14:face->texture_id(bits?TextureEntry::white_texture:Uuid{});break;
        case 15:face->material_id(bits?TextureEntry::white_texture:Uuid{});break;
        case 16:entry.default_texture.reset();break;
        case 17:entry.default_texture=std::make_shared<TextureFace>();break;
        }
        for(int length:{-1,0,21,22,45,100}){
            for(auto b:entry.bakes_bytes(length))std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
            std::cout<<';';
        }
        for(int length:{-1,0,21,22,45,100}){
            for(auto b:entry.bytes(length))std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
            std::cout<<';';
        }
        for(bool bakes:{false,true}){
            auto bytes=bakes?entry.bakes_bytes():entry.bytes();
            TextureEntry decoded(bytes);
            for(auto b:decoded.bytes())std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
            std::cout<<';';
            for(auto b:decoded.bakes_bytes())std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
            std::cout<<';'<<std::hex<<decoded.dirty_flags(45,false)<<';';
        }
        std::cout<<std::hex<<entry.dirty_flags(index,(bits&1)!=0)<<';';
        for(auto& f:entry.faces)std::cout<<(f?'1':'0');
        std::cout<<';'<<std::hex<<entry.dirty_flags(45,false)<<'\n';
    }
}
