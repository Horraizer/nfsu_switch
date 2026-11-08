#include "nfsu/world_motion.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace nfsu {
float world_train_arrival(float distance,float speed,float acceleration) noexcept {
    if(std::abs(acceleration)>.001F) {
        const float square=(acceleration*distance+acceleration*distance)+speed*speed;
        return ((square>0?std::sqrt(square):0.F)-speed)/acceleration;
    }
    return std::abs(speed)>.001F?distance/speed:99999.F;
}
float world_train_lead(int lane) noexcept {
    return lane>=11&&lane<=16?((1.8F-1.2F)*.25F)*float(lane-11)+1.2F:-1.F;
}
std::uint32_t advance_world_motion_clock(WorldMotionClock& c,float seconds) noexcept {
    const auto flags=c.flags;
    const float amount=(30.F*seconds)*c.rate;
    if(flags&0x80U)return 0x80;
    if(flags&0x10U) {
        c.phase=amount+c.phase;
        if(c.phase>c.end)c.phase=0;
        else if(c.phase<0)c.phase=c.end;
    }
    if(flags&0x20U) {
        const float next=amount+c.phase;
        if(c.end<next)return 0x20;
        c.phase=next;
        if(c.phase>c.end)c.phase=c.end;
        else if(c.phase<0)c.phase=0;
    }
    if(flags&0x40U) {
        c.phase=amount+c.phase;
        if(c.phase<=c.end) {
            if(c.phase<0){c.phase=0;c.rate*= -1.F;}
        } else if(c.hold<=0) {
            c.phase=c.end;c.rate*= -1.F;return 0;
        } else {
            const float next=amount+c.hold_elapsed;
            c.phase=c.end;c.hold_elapsed=next;
            if(c.hold<next){c.hold_elapsed=0;c.rate*= -1.F;return 0;}
        }
    }
    return 0;
}

namespace {
std::uint32_t hash_name(const std::string& value) {
    std::uint32_t hash=0xffffffffU;for(unsigned char c:value)hash=hash*33+c;return hash;
}
eagl::Pose identity_pose(){return {{1,1,1,1,0,0,0,1,0,0,0,0}};}
}
bool decode_world_motion_clips(const eagl::Bytes& bytes,const std::vector<std::uint32_t>& wanted,
    std::vector<WorldMotionClip>& out,std::string& error) {
    try {
        out.clear();if(wanted.empty()){error.clear();return true;}
        eagl::View file(bytes);std::set<std::uint32_t> found;
        bool root_found=false;
        std::vector<std::pair<std::size_t,std::size_t>> work{{0,bytes.size()}};
        while(!work.empty()) {
            auto range=work.back();work.pop_back();
            for(auto at=range.first;at<range.second;) {
                file.range(at,8);const auto tag=file.u32(at),count=file.u32(at+4);
                eagl::need(count<=range.second-at-8,"world motion chunk bounds");
                const auto end=at+8+count;
                if(tag&0x80000000U)work.push_back({at+8,end});
                if(tag==0xe34010 || tag==0xe34009) {
                    const auto start=(at+23)&~std::size_t(15);
                    eagl::need(start<=end,"world motion ELF alignment");
                    eagl::Elf elf(eagl::View(bytes.data()+start,end-start));
                    const auto root=elf.symbols.find("__Skeleton:::ROOT");
                    if(root!=elf.symbols.end()) {
                        eagl::need(!root_found,"duplicate world ROOT skeleton");
                        root_found=true;const auto at=root->second;
                        eagl::need(elf.data.u32(at+8)==2,"world ROOT bone count");
                        for(unsigned bone=0;bone<2;++bone) {
                            const auto record=at+16+bone*112;
                            eagl::need(elf.data.u32(record+12)==(bone?0U:0xffffffffU),"world ROOT parent");
                            const auto identity=identity_pose();
                            eagl::need(elf.data.u32(record+44)==bone,"world ROOT bone index");
                            for(unsigned k=0;k<11;++k)if(k!=3)
                                eagl::need(elf.data.f32(record+4*k)==identity[k],"world ROOT bind pose");
                        }
                    }
                    const auto bank=elf.symbols.find("__AnimationBank:::bank");
                    if(bank!=elf.symbols.end()) {
                        const auto first=bank->second;
                        const auto n=elf.data.u32(first+4);
                        const auto pointers=elf.data.u32(first+12),names=elf.data.u32(first+16);
                        eagl::need(n<=1024,"world motion clip count");
                        for(unsigned i=0;i<n;++i) {
                            const auto hash=hash_name(elf.data.text(elf.data.u32(names+4*i)));
                            if(std::find(wanted.begin(),wanted.end(),hash)==wanted.end())continue;
                            eagl::need(found.insert(hash).second,"duplicate world motion clip");
                            const auto clip=elf.data.u32(pointers+4*i);
                            eagl::need(elf.data.word(clip+8)==1,"world motion scalar channel count");
                            const auto frames=elf.data.word(clip+10),channel=elf.data.u32(clip+12);
                            const auto type=elf.data.word(channel);
                            eagl::need(frames>0&&frames<=8192&&(type==17||type==20),"world motion channel domain");
                            WorldMotionClip decoded;decoded.hash=hash;decoded.frames.reserve(frames);
                            for(unsigned frame=0;frame<frames;++frame) {
                                std::vector<eagl::Pose> pose(2,identity_pose());
                                eagl::channel(elf.data,channel,frame,pose);
                                decoded.frames.push_back(pose[1]);
                            }
                            out.push_back(std::move(decoded));
                        }
                    }
                }
                at=end;
            }
        }
        eagl::need(root_found,"world ROOT skeleton absent");
        for(auto hash:wanted)eagl::need(found.count(hash),"world motion clip absent");
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool load_world_motion_clips(FileSystemService& fs,const std::filesystem::path& root,
    const std::vector<std::uint32_t>& wanted,std::vector<WorldMotionClip>& out,std::string& error) {
    if(wanted.empty()){out.clear();error.clear();return true;}
    const auto path=root/"Global/INGAMEB.BUN";std::uint64_t size=0;
    if(!fs.file_size(path,size,error))return false;
    if(!size||size>32U*1024*1024){error="world motion bank size";return false;}
    eagl::Bytes bytes(static_cast<std::size_t>(size));std::size_t got=0;
    if(!fs.read_file(path,0,bytes.data(),bytes.size(),got,error))return false;
    if(got!=bytes.size()){error="world motion bank short read";return false;}
    return decode_world_motion_clips(bytes,wanted,out,error);
}

bool sample_world_motion_clip(const WorldMotionClip& clip,float frame,eagl::Pose& out) noexcept {
    if(clip.frames.empty()||!std::isfinite(frame))return false;
    const auto last=clip.frames.size()-1;
    const float position=std::max(0.F,std::min(float(last),frame));
    const auto first=std::size_t(position),second=std::min(first+1,last);
    return eagl::blend_pose(clip.frames[first],clip.frames[second],position-float(first),out);
}
std::array<float,16> world_motion_pose_matrix(const eagl::Pose& p) noexcept {
    const float x=p[4],y=p[5],z=p[6],w=p[7];
    const float yy=y+y,zz=z+z,xw=(x+x)*w,xx=(x+x)*x;
    std::array<float,16> m{{(1.F-(zz*z+yy*y))*p[0],(x*yy+zz*w)*p[0],(zz*x-w*yy)*p[0],0,
        (x*yy-zz*w)*p[1],(1.F-(zz*z+xx))*p[1],(zz*y+xw)*p[1],0,
        (zz*x+w*yy)*p[2],(zz*y-xw)*p[2],(1.F-(yy*y+xx))*p[2],0,p[8],p[9],p[10],1}};
    m[0]*=p[3];m[4]*=p[3];m[8]*=p[3];return m;
}
} // namespace nfsu
