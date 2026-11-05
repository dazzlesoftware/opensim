// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt. Compressor licenses: third_party/zlib/LICENSE
// and third_party/dotnet_zlib_intel/LICENSE.
#include "opensim/network/gzip.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <cstdlib>
#if defined(OPENSIM_GZIP_DOTNET_INTEL)
#include "../third_party/dotnet_zlib_intel/opensim_prefix.h"
extern "C" {
#include "../third_party/dotnet_zlib_intel/zlib.h"
#include "../third_party/dotnet_zlib_intel/x86.h"
}
#elif defined(OPENSIM_GZIP_SYSTEM_ZLIB)
#include <zlib.h>
#else
#define Z_PREFIX
#include "../third_party/zlib/zlib.h"
#endif
namespace opensim::network {
std::string gzip_compress(std::string_view bytes) {
#if defined(OPENSIM_GZIP_DOTNET_INTEL)
    static std::once_flag initialized;
    std::call_once(initialized, [] { x86_check_features(); });
#endif
    z_stream stream{};
    stream.zalloc = [](voidpf, uInt count, uInt size) -> voidpf { return std::calloc(count, size); };
    stream.zfree = [](voidpf, voidpf memory) { std::free(memory); };
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("gzip deflate initialization failed");
    struct End {
        z_stream& stream;
        ~End() {
            deflateEnd(&stream);
        }
    } end{stream};
    std::array<char, 8192> buffer{};
    std::string result;
    std::size_t offset = 0;
    while (true) {
        if (stream.avail_in == 0 && offset < bytes.size()) {
            auto count = std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<uInt>::max());
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data() + offset));
            stream.avail_in = static_cast<uInt>(count);
            offset += count;
        }
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        // Match GZipStream.Write followed by Dispose: consume with no flush, then finish.
        int code = deflate(&stream, offset == bytes.size() && stream.avail_in == 0 ? Z_FINISH : Z_NO_FLUSH);
        result.append(buffer.data(), buffer.size() - stream.avail_out);
        if (code == Z_STREAM_END)
            return result;
        if (code != Z_OK)
            throw std::runtime_error("gzip deflate failed");
    }
}
}
