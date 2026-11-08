#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace nfsu { namespace eagl {
using Bytes=std::vector<unsigned char>;
using Pose=std::array<float,12>;
inline void need(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
struct View {
    const unsigned char* p;std::size_t size;
    View(const Bytes& b):p(b.data()),size(b.size()){}
    View(const unsigned char* b,std::size_t n):p(b),size(n){}
    void range(std::size_t a,std::size_t n)const {need(a<=size && n<=size-a,"crowd input range");}
    unsigned byte(std::size_t a)const {range(a,1);return p[a];}
    unsigned word(std::size_t a)const {range(a,2);return p[a]|unsigned(p[a+1])<<8;}
    std::uint32_t u32(std::size_t a)const {range(a,4);return p[a]|std::uint32_t(p[a+1])<<8|std::uint32_t(p[a+2])<<16|std::uint32_t(p[a+3])<<24;}
    float f32(std::size_t a)const {auto u=u32(a);float f;std::memcpy(&f,&u,4);need(std::isfinite(f),"crowd nonfinite value");return f;}
    std::string text(std::size_t a)const {range(a,1);auto e=a;while(e<size && p[e])++e;need(e<size,"crowd unterminated name");return {reinterpret_cast<const char*>(p+a),e-a};}
};
struct Elf {
    View data;std::map<std::string,std::size_t> symbols;
    explicit Elf(View e):data(e) {
        need(e.u32(0)==0x464c457f && e.byte(4)==1 && e.byte(5)==1,"crowd ELF format");
        const auto sh=e.u32(32),count=e.word(48),stride=e.word(46);need(stride==40 && count<32,"crowd ELF sections");
        e.range(sh,count*40);std::size_t symbol=0;
        for(unsigned i=0;i<count;++i) {
            auto s=sh+i*40;const auto type=e.u32(s+4);const auto o=e.u32(s+16),n=e.u32(s+20);e.range(o,n);
            if(type==1 && (e.u32(s+8)&2))data=View(e.p+o,n);
            if(type==2)symbol=s;
        }
        need(symbol,"crowd ELF symbols absent");const auto st=e.u32(symbol+24);need(st<count,"crowd ELF strings");
        const auto strings=e.u32(sh+st*40+16),o=e.u32(symbol+16),n=e.u32(symbol+20);
        for(std::size_t i=o;i<o+n;i+=16) {const auto name=e.text(strings+e.u32(i));if(!name.empty())symbols[name]=e.u32(i+4);}
    }
};
// Integer sample of EAGL DeltaQ, DeltaF3 or DeltaF1 into a standard pose.
void channel(View,std::size_t,unsigned,std::vector<Pose>&);
// Original DeltaQ normalized-component interpolation, without sign rewriting.
bool blend_pose(const Pose&,const Pose&,float fraction,Pose&) noexcept;
}} // namespace nfsu::eagl
