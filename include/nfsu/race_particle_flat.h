#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nfsu {
inline bool race_particle_flat_material(float mode,float alpha,float lamp_mode) noexcept {
    return mode==9.F && alpha==1.F && lamp_mode==0.F;
}
inline bool race_particle_flat_frame(unsigned flip,unsigned options) noexcept {
    return flip==0 && (options&1024U)==0;
}
// Only live, bounded MeshVertex-style streams may use a flat sprite tint.
// Different quads may have different colors; every corner of one quad must
// retain the same four authored bytes. No source vertex or color is changed.
inline bool race_particle_flat_colors(const unsigned char* data,std::size_t bytes,
                                      std::size_t stride,std::size_t color_offset) noexcept {
    if(!data || !bytes || bytes>2*1024*1024 || stride<4 || color_offset>stride-4 ||
       bytes%stride || (bytes/stride)%4)return false;
    for(std::size_t quad=0;quad<bytes/stride;quad+=4)
        for(unsigned corner=1;corner<4;++corner)
            if(std::memcmp(data+quad*stride+color_offset,data+(quad+corner)*stride+color_offset,4))return false;
    return true;
}

// Validate immutable triangle topology once. Every triangle must belong to
// one four-vertex color group; winding and the quad diagonal are immaterial.
inline bool race_particle_flat_indices(const unsigned char* data,std::size_t bytes) noexcept {
    if(!data || !bytes || bytes>2*1024*1024 || bytes%(3*sizeof(std::uint16_t)))return false;
    for(std::size_t at=0;at<bytes;at+=3*sizeof(std::uint16_t)){
        std::uint16_t values[3];std::memcpy(values,data+at,sizeof values);
        if(values[0]/4!=values[1]/4 || values[0]/4!=values[2]/4)return false;
    }
    return true;
}

inline bool race_particle_stage_changed(bool particle,bool flat,bool previous_particle,bool previous_flat) noexcept {
    return particle!=previous_particle || flat!=previous_flat;
}
} // namespace nfsu
