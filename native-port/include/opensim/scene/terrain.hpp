// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <shared_mutex>
#include <utility>
#include <vector>
namespace opensim::scene {
// TerrainData's height/patch projection. Storage codecs, effects and the full
// terrain module remain separate from this thread-safe heightfield owner.
class Terrain {
public:
 static constexpr std::uint32_t patch_size=16;
 static constexpr float default_height=21.f;
 explicit Terrain(std::uint32_t width=256,std::uint32_t height=256);
 Terrain(const Terrain& other);
 Terrain& operator=(const Terrain&)=delete;
 std::uint32_t width()const noexcept{return width_;}
 std::uint32_t height()const noexcept{return height_;}
 std::uint32_t patches_x()const noexcept{return width_/patch_size;}
 std::uint32_t patches_y()const noexcept{return height_/patch_size;}
 float get(int x,int y)const;
 void set(int x,int y,float value);
 // The source interpolates the two triangles of a cell, not a bilinear plane.
 float height_at(float x,float y)const;
 void clear_land(float value=default_height);
 std::array<float,256> patch(std::uint32_t px,std::uint32_t py,float sub=0.f,float premult=1.f)const;
 std::pair<float,float> patch_min_max(std::uint32_t px,std::uint32_t py)const;
 std::vector<float> serialized_floats()const; // y outer, x inner
 std::vector<float> compressed_map()const; // source's legacy x outer, y inner
 void clear_taint();
 void taint_all();
 bool tainted()const;
 std::int32_t tainted_count()const;
 bool tainted_at(int x,int y,bool clear=false);
 bool tainted_patch(int px,int py,bool clear=false);
 int next_taint(int start_index=0); // inclusive, tests and clears
 Terrain clone()const; // source Clone copies heights, starts with no taints
private:
 std::size_t index(int x,int y)const;
 std::size_t patch_index(std::uint32_t px,std::uint32_t py)const;
 bool taint_get_unlocked(int index,bool clear);
 void taint_set_unlocked(int index,bool value);
 std::uint32_t width_,height_;
 std::vector<float> heights_;
 std::vector<std::uint32_t> taints_;
 std::int32_t tainted_count_=0;
 mutable std::shared_mutex mutex_;
};
}
