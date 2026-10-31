// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt. zlib license: third_party/zlib/LICENSE.
#include "opensim/network/simulation_body.hpp"
#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#define Z_PREFIX
#include "../third_party/zlib/zlib.h"
namespace opensim::network {
std::string inflate_gzip(std::string_view bytes){z_stream stream{};if(inflateInit2(&stream,31)!=Z_OK)throw std::runtime_error("gzip inflate initialization failed");struct End{z_stream& stream;~End(){inflateEnd(&stream);}} end{stream};std::size_t offset=0;std::string result;std::array<char,8192> buffer{};
 while(true){if(stream.avail_in==0&&offset<bytes.size()){auto size=std::min<std::size_t>(bytes.size()-offset,std::numeric_limits<uInt>::max());stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()+offset));stream.avail_in=static_cast<uInt>(size);offset+=size;}stream.next_out=reinterpret_cast<Bytef*>(buffer.data());stream.avail_out=static_cast<uInt>(buffer.size());auto status=inflate(&stream,Z_NO_FLUSH);auto produced=buffer.size()-stream.avail_out;result.append(buffer.data(),produced);if(status==Z_STREAM_END){auto consumed=offset-stream.avail_in;if(bytes.size()-consumed>=2&&static_cast<unsigned char>(bytes[consumed])==0x1f&&static_cast<unsigned char>(bytes[consumed+1])==0x8b){if(inflateReset2(&stream,31)!=Z_OK)throw std::runtime_error("gzip member reset failed");offset=consumed;stream.avail_in=0;continue;}break;}if(status!=Z_OK&&status!=Z_BUF_ERROR)throw std::invalid_argument("Invalid gzip stream");if(!produced&&stream.avail_in==0&&offset==bytes.size())break;if(status==Z_BUF_ERROR&&!produced&&stream.avail_in!=0)throw std::invalid_argument("gzip stream made no progress");}
 return result;
}
}
