#include "nfsu/career_magazine_menu.h"
#include "nfsu/career_ride.h"
#include <algorithm>

namespace nfsu {
void CareerMagazineMenu::reset(const CareerProfile& p) {
    entries_.clear();selected_=first_visible_=0;album_=p.page==CareerPage::magazine_album;
    const auto add=[&](unsigned index) {
        const auto at=std::find_if(p.magazine_covers.begin(),p.magazine_covers.end(),[&](const auto& c){return c.index==index;});
        if(album_|| (at!=p.magazine_covers.end()&&!at->seen))
            entries_.push_back({index,at==p.magazine_covers.end()?~0U:unsigned(at-p.magazine_covers.begin())});
    };
    if(album_) {
        for(unsigned i=0;i<27;++i)add(i);
    }else {
        // 4C8B70 appends unread task covers first, then the authored race cover.
        for(unsigned i=11;i<27;++i)add(i);
        for(unsigned i=0;i<11;++i)add(i);
    }
    stage_=album_?CareerMagazineStage::list:CareerMagazineStage::notice;
    active_=career_magazine_page(p.page)&&!entries_.empty();++revision_;
}
bool CareerMagazineMenu::move(int direction) {
    if(!active_||stage_!=CareerMagazineStage::list||!direction||entries_.size()<2)return false;
    const int step=direction<0?-1:1;
    const auto next=unsigned(std::clamp(int(selected_)+step,0,int(count()-1)));
    // PC4E9730 keeps left/right inside the same two-column album row.
    if(album_&&next/2!=selected_/2)return false;
    if(next==selected_)return false;
    selected_=next;++revision_;return true;
}
bool CareerMagazineMenu::move_vertical(int direction) {
    if(!active_||!album_||stage_!=CareerMagazineStage::list||!direction)return false;
    const int requested=int(selected_)+(direction<0?-2:2);
    if(requested<0)return false;
    unsigned next=unsigned(requested);
    if(next>=count()) {
        if(direction<0||selected_-first_visible_<6||first_visible_+8>=count())return false;
        // Native4E9940 scroll plus4E9A9E UI update corrects the temporary
        // empty last-column cursor to the last valid cover in this window.
        next=count()-1;
    }
    selected_=next;
    // Four visible rows scroll one row at a time, as4E9730 updates +40/+44.
    if(selected_<first_visible_)first_visible_-=2;
    else if(selected_>=first_visible_+8)first_visible_+=2;
    ++revision_;return true;
}
bool CareerMagazineMenu::accept(const CareerCatalog& catalog,CareerProfile& p,std::string& error) {
    if(!active_||!career_magazine_page(p.page)||!catalog.valid(p,error)||!catalog.pending_movie(p).empty()){if(error.empty())error="magazine presentation inactive";return false;}
    if(stage_==CareerMagazineStage::notice)stage_=CareerMagazineStage::list;
    else if(stage_==CareerMagazineStage::photo)stage_=CareerMagazineStage::list;
    else {
        if(!earned(selected_)){error.clear();return true;}
        p.magazine_covers.at(cover_index()).seen=true;stage_=CareerMagazineStage::photo;
    }
    ++revision_;error.clear();return true;
}
bool CareerMagazineMenu::back(const CareerCatalog& catalog,CareerProfile& p,std::string& error) {
    if(!active_||!career_magazine_page(p.page)||!catalog.valid(p,error)||!catalog.pending_movie(p).empty()){if(error.empty())error="magazine presentation inactive";return false;}
    if(stage_==CareerMagazineStage::photo){stage_=CareerMagazineStage::list;++revision_;error.clear();return true;}
    if(album_)p.page=CareerPage::menu;
    else if(!catalog.dismiss_magazines(p,error))return false;
    active_=false;++revision_;error.clear();return true;
}
bool CareerMagazineMenu::prepare_photo(const std::vector<unsigned char>& globals,const CareerCatalog& catalog,
    const CareerProfile& p,CareerMagazineRecipe& recipe,CareerRideSetup& ride,std::string& error)const {
    if(!active_||!viewing()||!career_magazine_page(p.page)||!catalog.valid(p,error)||!catalog.pending_movie(p).empty()) {
        if(error.empty())error="magazine photo has not been selected";
        return false;
    }
    const auto& cover=p.magazine_covers.at(cover_index());
    auto seed=cover.seed;CareerMagazineRecipe next;
    if(!catalog.magazines().make(CareerMagazineCatalog::key(cover.index),seed,next,error))return false;
    auto photo=p;photo.car=cover.car;photo.ride.installed=cover.installed;
    photo.ride.performance=cover.performance;photo.ride.unique=cover.unique;
    CareerRideSetup prepared;if(!resolve_career_ride(globals,catalog,photo,prepared,error))return false;
    recipe=std::move(next);ride=std::move(prepared);error.clear();return true;
}
}

namespace nfsu { namespace {
// PC1.4 6FA0B0/6FA120, consumed by4C90C0.
constexpr std::uint32_t magazine_text_keys[][2]={
    {0x38208033U,0x8ECB3336U},
    {0x38208034U,0x8ECB3337U},
    {0x38208035U,0x034AE601U},
    {0x38208036U,0x8ECB3339U},
    {0x38208037U,0x8ECB333AU},
    {0x38208038U,0x8ECB333BU},
    {0x38208039U,0x8ECB333CU},
    {0x3820803AU,0x8ECB333DU},
    {0x3820803BU,0x8ECB333EU},
    {0x3C3086C3U,0x68319A26U},
    {0x3C3086C4U,0x68319A27U},
    {0x3CD3B10BU,0x937E640EU},
    {0x3CD3B10CU,0x937E640FU},
    {0x3CD3B10DU,0x937E6410U},
    {0x3CD3B10EU,0x937E6411U},
    {0x3CD3B10FU,0x937E6412U},
    {0x3CD3B110U,0x937E6413U},
    {0x3CD3B111U,0x937E6414U},
    {0x3CD3B112U,0x937E6415U},
    {0x3CD3B113U,0x937E6416U},
    {0xD749D29BU,0x034AE5FEU},
    {0xD749D29CU,0x034AE5FFU},
    {0xD749D29DU,0x034AE600U},
    {0xD749D29EU,0x8ECB3338U},
    {0xD749D29FU,0x034AE602U},
    {0xD749D2A0U,0x034AE603U},
    {0xD749D2A1U,0x034AE604U},
};
}
std::uint32_t career_magazine_title_key(unsigned index)noexcept{return index<27?magazine_text_keys[index][1]:0;}
std::uint32_t career_magazine_description_key(unsigned index)noexcept{return index<27?magazine_text_keys[index][0]:0;}
}
