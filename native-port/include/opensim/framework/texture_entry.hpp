// SDK compatibility model. See protocol/LIBOPENMETAVERSE-LICENSE.txt.
#pragma once
#include "opensim/framework/texture_face.hpp"
#include <vector>
namespace opensim::framework {
class TextureEntry {
public:
    static constexpr unsigned max_faces=45;
    inline static const Uuid white_texture{"5748decc-f629-461c-9a36-a35a221fe21f"};
    explicit TextureEntry(Uuid texture);
    explicit TextureEntry(const TextureFace& face);
    explicit TextureEntry(std::span<const std::uint8_t> data);
    TextureEntry(std::span<const std::uint8_t> data,std::size_t position,std::size_t length);
    std::shared_ptr<TextureFace> default_texture;
    std::array<std::shared_ptr<TextureFace>,max_faces> faces{};
    std::shared_ptr<TextureFace> get_face(unsigned index);
    std::shared_ptr<TextureFace> create_face(unsigned index){return get_face(index);}
    std::uint32_t dirty_flags(int length,bool clear);
    void dirty_flags(int length,std::uint32_t flags);
    std::vector<std::uint8_t> bakes_bytes(int maxfaces=45)const;
    std::vector<std::uint8_t> bytes(int maxfaces=45)const;
private:
    std::vector<std::uint8_t> serialize(int maxfaces,bool bakes)const;
};
}
