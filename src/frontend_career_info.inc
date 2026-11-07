// Original4E9CE0/4EA210 screens. Read once with the resident career atlas;
// navigation does not reload the car, its scene, textures or the music.
namespace {
std::vector<unsigned> career_info_resources(const std::vector<unsigned char>& front,std::size_t offset,unsigned large,unsigned small,std::string& error) {
    std::vector<unsigned char> fng;std::size_t used=0;
    if(!jdlz_decompress(front.data()+offset,dword(front.data()+offset+12),1024*1024,fng,used,error))throw std::runtime_error(error);
    for(std::size_t pos=0;pos+8<=fng.size();++pos)if(!std::memcmp(fng.data()+pos,"RsNm",4)) {
        const auto size=dword(fng.data()+pos+4);if(size>fng.size()-pos-8)throw std::runtime_error("career information resource bounds");
        std::vector<unsigned> out;
        for(auto at=pos+8;at<pos+8+size;) {
            auto name=ascii(fng.data()+at,pos+8+size-at);at+=name.size()+1;
            const auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
            const auto dot=name.find_last_of('.');if(dot!=std::string::npos)name.resize(dot);
            for(auto& ch:name)if(ch>='a'&&ch<='z')ch=char(ch-32);
            out.push_back(name=="CONDUITMDITC_TT21I"?large:name=="CONDUITMDITC_TT14I"?small:bin_hash(name));
        }
        return out;
    }
    throw std::runtime_error("career information resources absent");
}
}
void FrontendArt::draw_career_info(ScreenImage image,const CareerCatalog& catalog,const CareerProfile& profile,const CareerInfoMenu& menu)const {
    impl_->draw_dialog(Canvas{image},menu.screen()==CareerInfoScreen::rankings?4:5,catalog.info_values(profile,menu),0,~0U);
}
void FrontendArt::record_career_info(unsigned width,unsigned height,const CareerCatalog& catalog,const CareerProfile& profile,
    const CareerInfoMenu& menu,FrontendUiFrame& frame)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    if(menu.revision()!=impl_->info_revision){impl_->info_revision=menu.revision();impl_->info_started=impl_->ui_ticks;}
    const auto tick=impl_->temporal?unsigned(std::min<std::uint64_t>(impl_->ui_ticks-impl_->info_started,0xffffff)):~0U;
    impl_->draw_dialog(Canvas{width,height,impl_->atlas,frame},menu.screen()==CareerInfoScreen::rankings?4:5,
        catalog.info_values(profile,menu),0,tick);
}
