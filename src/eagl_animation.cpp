#include "nfsu/eagl_animation.h"
#include <algorithm>
namespace nfsu { namespace eagl {
namespace {
std::size_t align(std::size_t n,std::size_t a){return (n+a-1)&~(a-1);}
}
// EAGL DeltaQ, DeltaF3 and DeltaF1. Original functions 6315F0, 62DF90,
// 62CB50. Random access begins at a block's absolute sample, then applies
// its byte deltas; the stored indices address bone quaternions or pose floats.
void channel(View b,std::size_t at,unsigned frame,std::vector<Pose>& pose) {
    const unsigned type=b.word(at);
    // EAGL type11: four scalar delta channels, 629F40 -> 634180.
    // The camera quaternion uses float offsets, unlike DeltaQ bone indices.
    if(type==11 || type==10) {
        const auto data=b.u32(at+4),frames=b.word(at+8),count=b.word(data),bits=b.byte(data+2);
        need(frame<frames && count>0 && count<=pose.size()*12 && (type!=11 || count%4==0) && (bits==8 || bits==16),"start scalar delta domain");
        for(unsigned k=0;k<count;++k) {
            const auto offset=type==11?b.word(at+10+2*(k/4))+k%4:b.word(at+10+2*k);
            need(offset/12<pose.size(),"start scalar delta output");
            const auto h=data+4+12*k;float value=b.f32(h+8);
            for(unsigned f=0;f<frame;++f) {
                const auto at=data+4+12*count+(f*count+k)*(bits/8);
                const double delta=double(bits==8?b.byte(at):b.word(at))*b.f32(h+4);
                value=float(k%4 && k<count/4*4?delta+b.f32(h)+value:delta+value+b.f32(h));
            }
            pose[offset/12][offset%12]=value;
        }
        return;
    }
    need(type==17 || type==20 || type==21,"crowd animation channel unsupported");
    const bool q=type==17;const unsigned dims=type==21?1:3;
    const unsigned count=q?b.byte(at+6):b.word(at+14),frames=b.word(at+(q?4:12)),shift=b.byte(at+16);
    need(frames && frame<frames && count<=192 && shift<=8 && b.u32(at+8)==0,"crowd animation sample domain");
    const auto index=b.u32(at+(q?12:4));const unsigned block=1U<<shift,local=frame&(block-1);
    const auto header=at+(q?18:20);
    const auto step=q?12:12*dims;
    const auto first=header+step*count+std::size_t(frame>>shift)*align((block+1)*count*dims,2);
    b.range(first,count*dims*2+local*count*dims);
    for(unsigned i=0;i<count;++i) {
        const auto h=header+i*step;std::array<float,4> v{};unsigned sign=0;
        for(unsigned k=0;k<dims;++k) {
            const auto raw=b.word(first+2*(i*dims+k));
            if(q) {v[k]=float(k?raw:raw>>1)*(k?1.5259021893143654e-5F:3.0518509447574615e-5F)*2-1;if(!k)sign=raw&1;}
            else v[k]=float(raw)*(b.f32(h+4*dims+4*k)*1.5259021893143654e-5F)+b.f32(h+4*k);
        }
        for(unsigned f=1;f<=local;++f)for(unsigned k=0;k<dims;++k) {
            const auto raw=b.byte(first+count*dims*2+(f-1)*count*dims+i*dims+k);
            float bias,scale;
            if(q) {
                bias=float(b.word(h+2*k))*1.5259021893143654e-5F*2-1;
                scale=float(b.word(h+6+2*k))*1.5259021893143654e-5F*2;
                v[k]+=float(k?raw:raw>>1)*scale*(k?0.003921568859368563F:0.007874015718698502F)+bias;
                if(!k)sign=raw&1;
            } else {
                const float extent=b.f32(h+4*dims+4*k);
                bias=float(b.word(h+8*dims+2*k))*extent*1.5259021893143654e-5F*2-extent;
                scale=(float(b.word(h+10*dims+2*k))*extent*1.5259021893143654e-5F*2)*0.003921568859368563F;
                v[k]+=float(raw)*scale+bias;
            }
        }
        if(q) {
            const auto bone=b.byte(index+i);need(bone<pose.size(),"crowd quaternion bone index");
            const float sum=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
            if(sum>1) {const float inv=1/std::sqrt(sum);for(int k=0;k<3;++k)v[k]*=inv;v[3]=0;}
            else v[3]=std::sqrt(1-sum)*(sign?-1:1);
            std::copy(v.begin(),v.end(),pose[bone].begin()+4);
        } else {
            const auto offset=b.word(index+2*i);need(offset/12<pose.size() && offset%12+dims<=12,"crowd float pose index");
            for(unsigned k=0;k<dims;++k)pose[offset/12][offset%12+k]=v[k];
        }
    }
    const auto fixed=q?b.byte(at+7):b.byte(at+17);
    const auto blocks=frames/block,remainder=frames%block;
    auto tail=header+step*count+blocks*align((block+1)*count*dims,2);
    if(remainder)tail+=(remainder+1)*count*dims;
    if(!q)tail=align(tail,2);
    const auto values=align(tail+fixed*(q?1:2),q?2:4);
    for(unsigned i=0;i<fixed;++i) {
        if(q) {
            const auto bone=b.byte(tail+i);need(bone<pose.size(),"crowd constant quaternion index");
            float sum=0;const auto raw=b.word(values+6*i);
            for(unsigned k=0;k<3;++k) {const auto word=b.word(values+6*i+2*k);const float v=float(k?word:word>>1)*(k?1.5259021893143654e-5F:3.0518509447574615e-5F)*2-1;pose[bone][4+k]=v;sum+=v*v;}
            if(sum>1) {const float inv=1/std::sqrt(sum);for(unsigned k=0;k<3;++k)pose[bone][4+k]*=inv;pose[bone][7]=0;}
            else pose[bone][7]=std::sqrt(1-sum)*((raw&1)?-1:1);
        }else {
            const auto offset=b.word(tail+2*i);need(offset/12<pose.size() && offset%12+dims<=12,"crowd constant float index");
            for(unsigned k=0;k<dims;++k)pose[offset/12][offset%12+k]=b.f32(values+4*(i*dims+k));
        }
    }
}
}} // namespace nfsu::eagl
