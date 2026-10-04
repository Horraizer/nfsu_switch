#include "nfsu/frontend.h"
#include "nfsu/race_events.h"
#include "nfsu/sha256.h"
#include "nfsu/texture.h"
#include <algorithm>
namespace nfsu {
FrontendAction FrontendMenu::update(FrontendInput input) {
    if(input.back) return FrontendAction::exit;
    if(input.up != input.down) {
        selected_=1-selected_;
        notice_.clear();
    }
    if(!input.accept) return FrontendAction::none;
    return selected_==0 ? FrontendAction::underground : FrontendAction::free_drive;
}
bool FrontendArt::load(const std::filesystem::path& root,std::string& error) {
    background_={}; selection_={}; logo_={};
    std::vector<unsigned char> bytes; std::vector<RaceTableIssue> issues;
    RaceTableLimits limits; limits.max_file_size=8*1024*1024; limits.max_image_size=16*1024*1024;
    if(!load_race_image(root/"FrontEnd/FrontB.lzc",bytes,issues,limits)) {
        error=issues.empty()?"frontend pack unreadable":issues.front().message; return false;
    }
    if(sha256_hex(sha256(bytes.data(),bytes.size())) !=
        "94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77") {
        error="frontend pack identity mismatch"; return false;
    }
    auto packs=parse_texture_pack_set(bytes.data(),bytes.size());
    if(!packs.ok()) { error="frontend texture tables invalid"; return false; }
    for(const auto& pack:packs.packs) for(const auto& record:pack.textures) {
        Image* image=nullptr;
        if(record.name=="PC_GRIT_BACK_01") image=&background_;
        else if(record.name=="MENUSELECT") image=&selection_;

        if(!image) continue;
        if(!record.valid || record.width>1024 || record.height>1024) {
            error="frontend texture record invalid"; return false;
        }
        std::vector<unsigned char> payload; std::size_t unused=0; P8Palette palette;
        if(!texture_payload(bytes.data(),bytes.size(),record,payload,unused,error,&palette)) return false;
        std::uint64_t top=0;
        if(!level_size(record.format,record.width,record.height,top) || top>payload.size() ||
           !decode_level(record.format,record.width,record.height,payload.data(),top,image->rgba,error,&palette)) return false;
        image->width=record.width; image->height=record.height;
    }
    if(!load_race_image(root/"Global/GLOBALB.BUN",bytes,issues,limits)) {
        error="global frontend texture pack unreadable"; return false;
    }
    if(sha256_hex(sha256(bytes.data(),bytes.size())) !=
        "39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9") {
        error="global frontend pack identity mismatch"; return false;
    }
    packs=parse_texture_pack_set(bytes.data(),bytes.size());
    if(!packs.ok()) { error="global frontend texture tables invalid"; return false; }
    for(const auto& pack:packs.packs) for(const auto& record:pack.textures) {
        if(record.name!="NFSU_LOGO") continue;
        if(!record.valid || record.width>1024 || record.height>1024) {
            error="frontend logo record invalid"; return false;
        }
        std::vector<unsigned char> payload; std::size_t unused=0; P8Palette palette;
        std::uint64_t top=0;
        if(!texture_payload(bytes.data(),bytes.size(),record,payload,unused,error,&palette) ||
           !level_size(record.format,record.width,record.height,top) || top>payload.size() ||
           !decode_level(record.format,record.width,record.height,payload.data(),top,logo_.rgba,error,&palette)) return false;
        logo_.width=record.width; logo_.height=record.height;
    }
    if(background_.rgba.empty() || selection_.rgba.empty() || logo_.rgba.empty()) {
        error="original frontend textures missing"; return false;
    }
    error.clear(); return true;
}
void FrontendArt::blit(ScreenImage target,const Image& source,unsigned x,unsigned y,unsigned w,unsigned h) {
    if(!target.pixels || !w || !h || source.rgba.empty()) return;
    for(unsigned dy=0;dy<h && y+dy<target.height;++dy) for(unsigned dx=0;dx<w && x+dx<target.width;++dx) {
        const auto* s=source.rgba.data()+((std::size_t(dy)*source.height/h)*source.width+std::size_t(dx)*source.width/w)*4;
        auto* d=target.pixels+(y+dy)*target.stride+(x+dx)*4;
        for(unsigned c=0;c<3;++c) d[c]=static_cast<unsigned char>((unsigned(s[c])*s[3]+unsigned(d[c])*(255-s[3])+127)/255);
        d[3]=255;
    }
}
void FrontendArt::draw(ScreenImage image,const FrontendMenu& menu) const {
    fill_screen_rect(image,0,0,image.width,image.height,{0,0,0,255});
    blit(image,background_,0,0,image.width,image.height);
    blit(image,logo_,60,42,640,80);
    const ScreenColor white{240,240,240,255}, black{0,0,0,255};
    draw_screen_text(image,66,210,"NEED FOR SPEED UNDERGROUND",3,white,black);
    for(unsigned i=0;i<2;++i) {
        const unsigned y=300+i*110;
        if(menu.selected()==i) blit(image,selection_,48,y-25,590,96);
        draw_screen_text(image,84,y,i==0?"UNDERGROUND":"FREE DRIVING",5,
            menu.selected()==i?ScreenColor{255,230,80,255}:white,black);
    }
    draw_screen_text(image,76,550,"OTHER MODES LOCKED",2,{155,155,155,255},black);
    draw_screen_text(image,76,608,"UP/DOWN: SELECT    A: ENTER    B: EXIT",2,white,black);
    if(!menu.notice().empty()) draw_screen_text(image,76,664,menu.notice().substr(0,92),2,{255,180,80,255},black);
}
}
