#include "nfsu/career_garage.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <map>
#include <stdexcept>
namespace nfsu {
std::uint32_t career_decal_brand_icon(std::uint32_t brand) noexcept {
    struct Row {std::array<std::uint32_t,3> aliases;std::uint32_t icon;};
    static constexpr Row rows[]={
#include "career_decal_icons.inc"
    };
    for(const auto& row:rows)for(auto alias:row.aliases)if(alias==brand)return row.icon;
    return 0;
}
// Original4E24A0 and its per-type frontend icon table.
std::uint32_t career_vinyl_icon(unsigned type) noexcept {
    const char* names[]={"VINYL_GROUP_TEAR","VINYL_GROUP_TEAR_HOOD","VINYL_GROUP_STRIPES","VINYL_GROUP_STRIPES_HOOD","VINYL_GROUP_SPLASH","VINYL_GROUP_MODERN","VINYL_GROUP_MODERN_HOOD","VINYL_GROUP_FLAMES","VINYL_GROUP_FLAMES_HOOD","VINYL_GROUP_LIGHTNING","VINYL_GROUP_LIGHTNING_HOOD","VINYL_GROUP_FLAGS","VINYL_GROUP_TRIBAL","VINYL_GROUP_TRIBAL_HOOD","VINYL_GROUP_WILD","VINYL_GROUP_UNIQUES","VINYL_GROUP_AEM","VINYL_GROUP_BODY_VINYL","VINYL_GROUP_MANUFACTURAR_LOGO"};
    return bin_hash(type<std::size(names)?names[type]:"CAR_THUMB");
}
namespace {
std::uint32_t attribute(const CarPartDatabase& db,const CarPartRecord& p,std::uint32_t key) {
    for(int i=0;i<p.attribute_count;++i) {
        const auto& a=db.attributes[p.attribute_index+unsigned(i)];if(a.key==key)return a.value;
    }
    return 0;
}
// Original category icon input to 0x507280 (table 0x706530).
std::uint32_t part_icon(unsigned slot) {
    switch(slot) {
    case 1:return 0x739C4A1BU;case 2:return 0x015C767CU;
    case 10:return 0x4E6C2AF9U;case 11:return 0xBB034EA6U;
    case 8:return 0x028F7092U;case 7:return 0x79165861U;
    case 6:return 0x945E9BC5U;case 13:return 0xF69AC384U;
    case 14:return 0x2CBB2D1CU;case 17:return 0x0294D2A3U;
    case 15:return bin_hash("VISUAL_PART_MUFFLERTIP");
    case 115:return 0x3F23165CU;case 116:return 0x02929058U;
    default:return slot>=31&&slot<=34?bin_hash("VISUAL_PART_VINYL"):slot>=59&&slot<=114?bin_hash("VISUAL_PART_DECALS"):0;
    }
}
constexpr const char* decal_zones[]={"DECAL_ZONE_HOOD","DECAL_ZONE_WINDSHIELD","DECAL_ZONE_REARWINDOW",
    "DECAL_ZONE_LEFTDOOR","DECAL_ZONE_RIGHTDOOR","DECAL_ZONE_LEFTPANEL","DECAL_ZONE_RIGHTPANEL"};
constexpr const char* decal_layout_names[]={"HOOD_RECT","FRONT_WINDOW_WIDE","REAR_WINDOW_WIDE",
    "LEFT_DOOR_RECT","RIGHT_DOOR_RECT","LEFT_QUARTER_RECT","RIGHT_QUARTER_RECT"};
std::string decal_layout_icon(unsigned zone,bool large) {
    const char* prefixes[]={"DECAL_HOOD","DECAL_LREAR_QP","DECAL_RREAR_QP"};
    return std::string(prefixes[zone==0?0:zone==5?1:2])+(large?"_LAYOUT2":"_LAYOUT1");
}
std::string decal_slot_icon(unsigned zone,bool large,bool wide,unsigned slot) {
    if(zone==0)return std::string(large?"DECAL_HOOD_L2_SLOT":"DECAL_HOOD_L1_SLOT")+std::to_string(slot);
    if(zone==3||zone==4)return std::string(zone==3?"DECAL_LEFTDOOR_SLOT":"DECAL_RIGHTDOOR_SLOT")+
        std::to_string(slot)+(wide?"_WIDEBODY":"");
    // The small quarter slot names are referenced in speed.exe but their
    // textures are absent from the PC package. Reuse its matching layout.
    if(!large)return decal_layout_icon(zone,false);
    return std::string(zone==5?"DECAL_LREAR_QP":"DECAL_RREAR_QP")+
        "_L2_SLOT"+std::to_string(slot);
}
std::uint32_t decal_layout(const CareerProfile& p,unsigned zone,bool large) {
    return bin_hash(p.car+"_DECAL_"+decal_layout_names[zone]+(large||(zone<=4&&zone!=0)?"_MEDIUM":"_SMALL"));
}
unsigned decal_widebody(const CareerTuningCatalog& t,const CareerProfile& p) {
    const auto* kit=t.find_part(p.car,6,p.ride.installed[6]);
    if(!kit)return 0;
    return kit->name=="VAPOR"?1U:kit->name=="MANTIS"?2U:0U;
}
bool vinyl_in_family(const CarPartRecord& part,unsigned family) {
    // 4E2680 excludes preset-only designs from the regular shop.
    constexpr const char* presets[]={"SAMANTHA","EDDIE","JUNKMAN","MELISSA","DDAY","CUSTOM33","CUSTOM34",
        "CUSTOM30","CUSTOM31","CUSTOM32","CUSTOM35","CUSTOM11","CUSTOM24","CUSTOM27","CUSTOM28","CUSTOM29","CUSTOM37"};
    for(auto name:presets)if(part.part_name_hash==bin_hash(name))return false;
    return part.byte_12==family;
}
void clear_vinyl_colors(CareerProfile& p,unsigned layer) {
    std::fill_n(p.ride.installed.begin()+42+(layer-31)*4,4,0U);
}
constexpr const char* vinyl_groups[]={"Tear","Hood Tear","Stripes","Hood Stripes","Splash","Modern",
    "Hood Modern","Flames","Hood Flames","Lightning","Hood Lightning","Flags","Tribal","Hood Tribal","Wild","Unique","Manufacturer","Body"};
}
std::vector<std::uint32_t> career_decal_menu_icons() {
    std::vector<std::uint32_t> result;
    for(auto name:decal_zones)result.push_back(bin_hash(name));
    for(unsigned zone:{0U,3U,4U,5U,6U}) {
        if(zone==3||zone==4) {
            for(unsigned slot=1;slot<=6;++slot)
                for(bool wide:{false,true})result.push_back(bin_hash(decal_slot_icon(zone,false,wide,slot)));
        }else for(bool large:{false,true}) {
            result.push_back(bin_hash(decal_layout_icon(zone,large)));
            const auto count=zone==0?(large?8U:4U):(large?2U:1U);
            for(unsigned slot=1;slot<=count;++slot)result.push_back(bin_hash(decal_slot_icon(zone,large,false,slot)));
        }
    }
    return result;
}
void CareerGarage::reset(const CareerCatalog& c,const CareerProfile& p,bool appearance_only){
    dialog_=GarageDialog::none;dialog_selection_=trade_review_=0;
    state_={};parents_.clear();confirm_=false;unique_intro_=false;appearance_only_=appearance_only;build(c,p);
}
void CareerGarage::refresh(const CareerCatalog& c,const CareerProfile& p){confirm_=false;build(c,p);}
void CareerGarage::reset_unique(const CareerCatalog& c,const CareerProfile& p) {
    dialog_=GarageDialog::none;dialog_selection_=trade_review_=0;
    state_={};parents_.clear();confirm_=false;unique_intro_=true;appearance_only_=false;
    const auto* media=c.media(p.pending_unique);
    const bool performance=media&&!media->unique_categories.empty()&&media->unique_categories.front()>=16;
    state_.page=performance?GaragePage::unique_performance:GaragePage::unique_parts;
    const auto category=media&&!media->unique_categories.empty()?media->unique_categories.front():25U;
    state_.slot=category==12?8:category==3?17:category==7?31:category==8?11:category==15?6:0;build(c,p);
}
void CareerGarage::move(int direction) {
    if(dialog_!=GarageDialog::none) {
        if(dialog_!=GarageDialog::already_installed)dialog_selection_=unsigned(std::clamp(int(dialog_selection_)+direction,0,1));
        return;
    }
    if(trade_review_) {
        if(confirm_)dialog_selection_=unsigned(std::clamp(int(dialog_selection_)+direction,0,1));
        return;
    }
    if(unique_intro_)return;
    confirm_=false;if(entries_.empty())return;
    state_.selection=unsigned(std::clamp(int(state_.selection)+direction,0,int(entries_.size()-1)));
    if(state_.page==GaragePage::colors&&entries_[selected()].available)state_.preview=state_.selection;
}
void CareerGarage::move_color(int x,int y) {
    if(state_.page!=GaragePage::colors||entries_.empty())return;
    // Native4DC280/4DC230: clamp each axis independently; retry one missing
    // cell, then retain the old selection. Never wrap across a row.
    const auto cell=entries_[selected()].value;int row=int(cell/10),column=int(cell%10);
    for(unsigned attempt=0;attempt<2;++attempt) {
        row=std::clamp(row+y,0,5);column=std::clamp(column+x,0,9);
        const auto at=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.value==unsigned(row*10+column);});
        if(at!=entries_.end()){state_.selection=unsigned(at-entries_.begin());if(at->available)state_.preview=state_.selection;confirm_=false;return;}
    }
}
void CareerGarage::change_finish(int direction,const CareerCatalog& c,const CareerProfile& p) {
    if(state_.page!=GaragePage::colors||(state_.slot!=30&&state_.slot!=38))return;
    remember_color_cursor();
    constexpr unsigned families[]={0x02DAAB07U,0x03437A52U,0x03797533U};unsigned at=0;
    for(unsigned i=0;i<3;++i)if(state_.group==families[i])at=i;
    // Native4D7B70 lists finishes with an unlocked palette in career mode.
    for(unsigned step=0;step<3;++step) {
        at=(at+(direction<0?2:1))%3;
        if(c.tuning().category_available(0,at+1,p.status))break;
    }
    state_.group=families[at];state_.selection=state_.preview=0;confirm_=false;build(c,p);
    restore_color_cursor();
}
unsigned CareerGarage::color_cursor_index()const {
    const auto family=state_.group==0x03437A52U?1U:state_.group==0x03797533U?2U:0U;
    return state_.channel*3+family;
}
void CareerGarage::remember_color_cursor() {
    if(!entries_.empty())color_cells_[color_cursor_index()]=entries_[selected()].value;
}
void CareerGarage::restore_color_cursor() {
    const auto cell=color_cells_[color_cursor_index()];
    const auto at=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.value==cell;});
    if(at!=entries_.end())state_.selection=unsigned(at-entries_.begin());
    if(!entries_.empty()&&entries_[selected()].available)state_.preview=state_.selection;
    else if(state_.slot>=42&&state_.slot<=57) {
        const auto draft=draft_colors_[state_.channel];
        const auto valid=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.part==draft&&e.available;});
        if(valid!=entries_.end())state_.preview=unsigned(valid-entries_.begin());
    }
}
bool CareerGarage::edit_vinyl_colors(const CareerCatalog& c,const CareerProfile& p,std::string& error) {
    error.clear();if(dialog_!=GarageDialog::none||state_.page!=GaragePage::vinyl_layers||entries_.empty())return false;
    const auto layer=entries_[selected()].value;
    const auto* vinyl=c.tuning().find_part(p.car,layer,p.ride.installed[layer]);
    const auto count=vinyl?std::min(4U,attribute(c.tuning().parts(),*vinyl,bin_hash("NUMREMAPCOLOURS"))):0U;
    if(!count){error="This vinyl has no remappable colors.";return false;}
    // Original4E3320: layer1 is slot34, and its colors are slots54..57.
    State next;next.page=GaragePage::colors;next.slot=42+(layer-31)*4;
    next.group=0x03E871F1U;next.channels=count;
    for(unsigned i=0;i<4;++i)draft_colors_[i]=p.ride.installed[next.slot+i];
    open_page(next,c,p);return true;
}
bool CareerGarage::reorder_vinyl(int direction,const CareerCatalog& c,CareerProfile& p) {
    if(dialog_!=GarageDialog::none||state_.page!=GaragePage::vinyl_layers||entries_.empty())return false;
    const auto index=(selected()+(direction<0?3U:1U))%4;
    const auto from=entries_[selected()].value,to=entries_[index].value;
    if(!p.ride.installed[from]&&!p.ride.installed[to])return false;
    std::swap(p.ride.installed[from],p.ride.installed[to]);
    const auto colors=42+(from-31)*4,other=42+(to-31)*4;
    for(unsigned i=0;i<4;++i)std::swap(p.ride.installed[colors+i],p.ride.installed[other+i]);
    // Native4E3320 swaps RideInfo slots without moving735DEC's layer focus.
    build(c,p);return true;
}
bool CareerGarage::white_decal()const {
    if(entries_.empty())return true;
    const auto& black=state_.black_decals;
    return std::find(black.begin(),black.end(),entries_[selected()].value)==black.end();
}
void CareerGarage::toggle_decal_color(const CareerCatalog& c,const CareerProfile& p) {
    if(state_.page!=GaragePage::decals_brands||confirm_||entries_.empty())return;
    const auto brand=entries_[selected()].value;
    if(!brand)return;
    // Original 511BF0/512690 retain the selected color on each sponsor.
    auto& black=state_.black_decals;
    const auto color=std::find(black.begin(),black.end(),brand);
    if(color==black.end())black.push_back(brand);
    else black.erase(color);
    build(c,p);
    for(unsigned i=0;i<entries_.size();++i)if(entries_[i].value==brand){state_.selection=i;break;}
}
bool CareerGarage::back(const CareerCatalog& c,const CareerProfile& p) {
    if(dialog_!=GarageDialog::none){dialog_=GarageDialog::none;dialog_selection_=0;return false;}
    if(confirm_){confirm_=false;dialog_selection_=0;return false;}
    if(trade_review_){--trade_review_;return false;}
    if(state_.page==GaragePage::colors&&state_.channel) {
        remember_color_cursor();
        if(!entries_.empty())draft_colors_[state_.channel]=entries_[preview_index()].part;
        --state_.channel;--state_.slot;state_.selection=state_.preview=0;build(c,p);
        restore_color_cursor();
        return false;
    }
    if(parents_.empty())return true;
    state_=parents_.back();parents_.pop_back();build(c,p);return false;
}
void CareerGarage::open_page(State next,const CareerCatalog& c,const CareerProfile& p) {
    parents_.push_back(state_);state_=next;confirm_=false;build(c,p);
    if(state_.page==GaragePage::colors)color_cells_.fill(0);
    if(state_.page==GaragePage::decals_brands&&p.ride.installed[state_.slot]&&
       std::none_of(entries_.begin(),entries_.end(),[](const auto& e){return e.installed;})) {
        if(const auto* part=c.tuning().find_part(p.car,state_.slot,p.ride.installed[state_.slot])) {
            state_.black_decals.push_back(part->hash_0c);
            build(c,p);
        }
    }
    if(state_.page==GaragePage::rim_sizes) {
        const auto chosen=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.part==state_.field;});
        if(chosen!=entries_.end())state_.selection=unsigned(chosen-entries_.begin());
    }
    if(previewing()||state_.page==GaragePage::rims||state_.page==GaragePage::packages)
        for(unsigned i=0;i<entries_.size();++i)if(entries_[i].installed){state_.selection=state_.preview=i;break;}
    if(state_.page==GaragePage::colors) {
        // Initialize every vinyl channel from its installed swatch, not only
        // channel0. Single-part finishes retain their native first-page cursor.
        for(unsigned channel=0;channel<state_.channels;++channel) {
            const auto part=state_.channels>1?draft_colors_[channel]:p.ride.installed[state_.slot];
            for(const auto& entry:entries_)if(entry.part==part)color_cells_[channel*3]=entry.value;
        }
        remember_color_cursor();
    }
}
bool CareerGarage::previewing()const {
    if(state_.page==GaragePage::packages)return state_.field==1||state_.field==8;
    return state_.page==GaragePage::trade_in||state_.page==GaragePage::unique_parts||state_.page==GaragePage::decals_brands||state_.page==GaragePage::parts||state_.page==GaragePage::rim_styles||state_.page==GaragePage::rim_sizes||state_.page==GaragePage::colors;
}
std::string CareerGarage::installed_signature(const CareerProfile& p) {
    std::string result=p.car+(p.bonus_preset.empty()?"":":bonus:"+p.bonus_preset);
    for(auto part:p.ride.installed)result+=":"+std::to_string(part);
    for(auto level:p.ride.performance)result+=":p"+std::to_string(level);
    for(auto unique:p.ride.unique)result+=":u"+std::to_string(unique);
    return result;
}
std::string CareerGarage::preview_signature(const CareerProfile& p)const {
    std::string result=p.car+(p.bonus_preset.empty()?"":":bonus:"+p.bonus_preset)+":"+std::to_string(previewing()?slot():120);
    if(state_.page==GaragePage::packages&&previewing())
        result+=":package:"+std::to_string(state_.field);
    if(state_.page==GaragePage::trade_in) {
        // Car selection changes resident draws, not the asset-job identity.
        // Every unlocked candidate in this bank shares the owned ride input.
        result=installed_signature(p)+":trade";
        for(const auto& entry:entries_)result+=":"+entry.name;
        return result;
    }
    for(auto level:p.ride.performance)result+=":p"+std::to_string(level);
    for(auto unique:p.ride.unique)result+=":u"+std::to_string(unique);
    const auto current=preview(p);
    for(unsigned at=0;at<120;++at)if(!previewing()||at!=slot())result+=":"+std::to_string(current.ride.installed[at]);
    const auto first=previewing()&&state_.page!=GaragePage::colors?(preview_index()/12)*12:0;
    const auto last=state_.page==GaragePage::colors?unsigned(entries_.size()):
        previewing()?std::min(first+12,unsigned(entries_.size())):0;
    for(auto i=first;i<last;++i)result+=":"+std::to_string(entries_[i].part)+":"+std::to_string(entries_[i].available);
    return result;
}
bool CareerGarage::prepare_previews(const std::vector<unsigned char>& globals,const CareerCatalog& catalog,
    const CareerProfile& p,std::vector<CareerRideSetup>& variants,std::vector<unsigned>& indices,std::string& error)const {
    std::vector<CareerRideSetup> next(1);
    if(!resolve_career_ride(globals,catalog,p,next.front(),error))return false;
    const auto prepare_paint = [&](CareerRideSetup& ride) {
        if (state_.page != GaragePage::colors || (state_.slot != 37 && state_.slot != 39)) return true;
        auto& selection = ride.request.installed_parts;
        if (state_.slot == 37 && !p.ride.installed[12]) {
            // Native50C5F0 temporarily installs the engine by its package level.
            const auto model = find_car_part(catalog.tuning().parts(),bin_hash(p.car),12,
                kAnyPartName,kNoCarPart,int(p.ride.performance[2]));
            if (model == kNoCarPart) { error = "frontend engine preview model unavailable"; return false; }
            selection.records[12] = model;
        }
        auto paint = selection.records[state_.slot];
        if (!paint || *paint == kNoCarPart) {
            const auto family = bin_hash(state_.slot == 37 ? "HOSE" : "CALIPER");
            for (const auto& record : catalog.tuning().parts().records)
                if (record.slot == 30 && record.hash_0c == family) { paint = record.index; break; }
        }
        if (!paint || *paint == kNoCarPart) { error = "frontend preview paint family unavailable"; return false; }
        selection.preview = *paint;
        return true;
    };
    // Index0 is always the durable normal ride so cancelling a preview
    // can restore hood/rims immediately without another preparation job.
    std::vector<unsigned> mapping(entries_.size(),0);
    const bool whole_palette=state_.page==GaragePage::colors;
    const bool whole_bank=whole_palette||state_.page==GaragePage::trade_in;
    const auto first=!whole_bank&&previewing()?(preview_index()/12)*12:0;
    const auto last=whole_bank?unsigned(entries_.size()):previewing()?std::min(first+12,unsigned(entries_.size())):0;
    for(auto i=first;i<last;++i)if(entries_[i].available) {
        const auto candidate=preview(p,i);
        const bool paint_visibility=state_.page==GaragePage::colors&&(state_.slot==37||state_.slot==39);
        if(!paint_visibility&&candidate.car==p.car&&candidate.ride.installed==p.ride.installed&&
           candidate.ride.performance==p.ride.performance)continue;
        CareerRideSetup ride;
        if(!resolve_career_ride(globals,catalog,candidate,ride,error))return false;
        if (!prepare_paint(ride)) return false;
        if(whole_palette)ride.garage_paint_slot=state_.slot;
        mapping[i]=unsigned(next.size());next.push_back(std::move(ride));
    }
    variants=std::move(next);indices=std::move(mapping);error.clear();return true;
}
float CareerGarage::road_target()const {
    // Native50CD34/50CD39 reads6B6B30=1. Generic505C29/39 reads6B6B2C=20.
    return state_.page==GaragePage::rims||state_.page==GaragePage::rim_styles||state_.page==GaragePage::rim_sizes||(state_.page==GaragePage::colors&&state_.slot==41)?1.F:20.F;
}
unsigned CareerGarage::focus_slot()const {
    if(state_.page==GaragePage::rims||state_.page==GaragePage::rim_styles||state_.page==GaragePage::rim_sizes)return 17;
    if(entries_.empty())return 0;
    // Category hover does not change the native garage camera.
    if(state_.page==GaragePage::body||state_.page==GaragePage::accessories)return 0;
    if(state_.page==GaragePage::parts||state_.page==GaragePage::unique_parts)return state_.slot>=59&&state_.slot<=114?19+(state_.slot-59)/8:state_.slot;
    // 5046D0 routes the brake package through 4B0150 with slot 16.
    if(state_.page==GaragePage::packages&&state_.field==8)return 16;
    if(state_.page==GaragePage::decals_zones)return 0;
    if(state_.page==GaragePage::decals_sizes||state_.page==GaragePage::decals_slots)return 19+state_.group;
    if(state_.page==GaragePage::decals_brands)return 19+(state_.slot-59)/8;
    if(state_.page==GaragePage::vinyl_layers)return 0;
    if(state_.page==GaragePage::vinyl_types||state_.page==GaragePage::vinyl_groups)return 0;
    const auto paint_slot=state_.page==GaragePage::colors?state_.slot:0;
    if(paint_slot>=37&&paint_slot<=41)return paint_slot; // Original4AEA50 uses the paint camera itself.
    return paint_slot==30?30:paint_slot>=42&&paint_slot<=57?31+(paint_slot-42)/4:0;
}
CareerProfile CareerGarage::preview(const CareerProfile& p)const {
    return preview(p,preview_index());
}
CareerProfile CareerGarage::preview(const CareerProfile& p,unsigned index)const {
    auto next=p;
    if(state_.page==GaragePage::trade_in) {
        if(index<trade_previews_.size()&&entries_[index].available)next=trade_previews_[index];
        return next;
    }
    // Native50ADA0 stages the highlighted package in the hypothetical ride.
    // Installed parts and the durable bank stay unchanged until confirmation.
    if(state_.page==GaragePage::packages&&index<entries_.size()&&entries_[index].value<4)
        next.ride.performance[state_.field]=static_cast<unsigned char>(entries_[index].value);
    if(state_.page==GaragePage::colors&&state_.slot>=42&&state_.slot<=57) {
        const auto base=state_.slot-state_.channel;
        for(unsigned i=0;i<state_.channels;++i)if(draft_colors_[i])next.ride.installed[base+i]=draft_colors_[i];
    }
    if(previewing()&&state_.page!=GaragePage::packages&&index<entries_.size()&&entries_[index].available)
        {
        if(state_.slot>=31&&state_.slot<=34&&next.ride.installed[state_.slot]!=entries_[index].part)
            clear_vinyl_colors(next,state_.slot);
        next.ride.installed[state_.slot]=entries_[index].part;
        if(state_.slot>=59&&state_.slot<=114&&state_.layout)next.ride.installed[19+(state_.slot-59)/8]=state_.layout;
    }
    return next;
}
std::size_t CareerGarage::fng()const {
    switch(state_.page) {
    case GaragePage::root:return 0xb7018c;
    case GaragePage::trade_in:return 0xbbe50c;
    case GaragePage::visual:return 0xb7e48c; // MU_GaragePartsChooseVisual
    case GaragePage::body:return 0xb7fc8c;
    case GaragePage::accessories:return 0xc0060c;
    case GaragePage::unique_performance:return 0xbcf70c;
    case GaragePage::unique_parts:return 0xbd0d0c;
    case GaragePage::performance:return 0xb80a8c;
    case GaragePage::packages:return 0xb8180c;
    case GaragePage::rims:return 0xb72f8c;
    case GaragePage::rim_styles:return 0xb73c0c;
    case GaragePage::rim_sizes:return 0xb74c0c;
    case GaragePage::paint:return 0xb75b8c;
    case GaragePage::colors:return 0xbe4c0c;
    case GaragePage::vinyl_layers:return 0xb7b18c;
    case GaragePage::vinyl_groups:return 0xb7750c;
    case GaragePage::vinyl_types:return 0xb7a40c;
    case GaragePage::parts:if(state_.slot>=31&&state_.slot<=34)return 0xb7828c;return state_.slot>=59&&state_.slot<=114?0xbff68c:0xb71e0c;
    case GaragePage::decals_zones:return 0xbfb88c;
    case GaragePage::decals_sizes:return 0xbfc50c;
    case GaragePage::decals_slots:return 0xbfd18c;
    case GaragePage::decals_brands:return 0xbfde0c;
    default:return state_.slot>=59&&state_.slot<=114?0xbff68c:0xb71e0c;
    }
}
void CareerGarage::build(const CareerCatalog& c,const CareerProfile& p) {
    trade_previews_.clear();
    entries_.clear();const auto& t=c.tuning();const auto& db=t.parts();
    const auto add=[&](const char* name,const char* icon,unsigned value) {
        GarageEntry e;e.name=name;e.icon=bin_hash(icon);e.value=value;entries_.push_back(std::move(e));
    };
    switch(state_.page) {
    case GaragePage::unique_performance: {
        title_="CHOOSE ONE UNIQUE UPGRADE";
        const auto* media=c.media(p.pending_unique);
        if(media)for(auto category:media->unique_categories) {
            unsigned field=0;
            while(field<9&&unsigned(CareerTuningCatalog::performance_category(field))!=category)++field;
            //4C6CB0 skips previously installed separate unique flags.
            if(field==9||p.ride.unique[field])continue;
            if(category==18)add("Engine / Exhaust Package","PERFORMANCE_ENGINE",category);
            if(category==19)add("Drivetrain Packages","PERFORMANCE_TRANSMISSION",category);
            if(category==20)add("Tires","PERFORMANCE_TIRES",category);
            if(category==23)add("E.C.U. & Fuel System","PERFORMANCE_ECU",category);
            if(category==22)add("Turbo Package","PERFORMANCE_TURBO",category);
            if(category==17)add("Brake Kits","PERFORMANCE_BRAKES",category);
            if(category==24)add("Nitrous Oxide","PERFORMANCE_NITROUS",category);
            if(category==16)add("Weight Reduction","PERFORMANCE_WEIGHT_REDUCTION",category);
            if(category==21)add("Suspension","PERFORMANCE_SUSPENSION",category);
        }
        break;
    }
    case GaragePage::unique_parts:
        title_=state_.slot==8?"CHOOSE ONE UNIQUE HOOD":state_.slot==17?"CHOOSE ONE UNIQUE RIM":
            state_.slot==31?"CHOOSE ONE UNIQUE VINYL":state_.slot==11?"CHOOSE ONE UNIQUE SPOILER":"CHOOSE ONE UNIQUE WIDE BODY KIT";
        for(const auto& part:db.records)if(part.upgrade_level==4&&t.find_part(p.car,state_.slot,part.part_name_hash)==&part) {
            GarageEntry entry;entry.name=part.name;entry.part=part.part_name_hash;
            entry.value=unsigned(CareerTuningCatalog::slot_category(state_.slot));entry.icon=part_icon(state_.slot);entries_.push_back(std::move(entry));
        }
        break;
    case GaragePage::root:
        title_="CUSTOMIZE";add("Visual","BROWSER_VISUAL",0);
        if(!appearance_only_)add("Performance","BROWSER_PERFORMANCE",1);
        if(!appearance_only_&&!c.free_drive())add("Trade In","BROWSER_TRADE_IN",2);
        break;
    case GaragePage::trade_in:
        title_="TRADE IN";
        for(unsigned index=0;index<c.cars().size();++index) {
            const auto& car=c.cars()[index];if(car.name==p.car||!c.car_available(p,car.name))continue;
            CareerProfile candidate;int cost=0;std::string error;
            if(!c.prepare_trade(p,car.name,candidate,cost,error))throw std::runtime_error(error);
            // Original4C3240 assigns the selected car's manufacturer icon.
            add(car.name.c_str(),"CAR_THUMB",index);auto& entry=entries_.back();
            entry.icon=bin_hash("CARSELECT_MANUFACTURER_"+car.brand);
            entry.trade_cost=cost;entry.price=cost>0?unsigned(cost):0;trade_previews_.push_back(std::move(candidate));
        }break;
    case GaragePage::visual:
        title_="VISUAL";add("Body","VISUAL_PART_BODY",0);add("Rims","VISUAL_PART_RIMS",1);
        add("Accessories","VISUAL_PART_ACCESSORIES",2);add("Paint","VISUAL_PART_PAINT",3);
        add("Decals","VISUAL_PART_DECALS",4);add("Vinyl","VISUAL_PART_VINYL",5);
        entries_.back().available=std::any_of(db.records.begin(),db.records.end(),[&](const auto& part) {
            return part.slot==31&&(t.part_available(part,p.status,p.style_high_water)||c.owns(p,7,t.ownership_key(part)));
        });break;
    case GaragePage::body:
        title_="BODY";add("Front Bumper","PART_FRONT_BUMPER",1);add("Rear Bumper","PART_REAR_BUMPER",2);
        add("Side Skirts","PART_SKIRT",10);add("Spoiler","PART_SPOILER",11);add("Hood","PART_HOOD",8);
        add("Roof Scoop","PART_ROOF",7);add("Wide Body Kit","PART_WIDE_BODY",6);break;
    case GaragePage::accessories:
        title_="ACCESSORIES";add("Neon","",116);add("Headlights","",13);add("Taillights","",14);
        add("Muffler Tips","VISUAL_PART_MUFFLERTIP",15);add("Window Tint","",115);
        {constexpr unsigned icons[]={0x02929058U,0xF69AC384U,0x2CBB2D1CU,0,0x3F23165CU};
            for(unsigned i=0;i<5;++i)if(icons[i])entries_[i].icon=icons[i];}break;
    case GaragePage::performance: {
        title_="PERFORMANCE";
        const char* names[]={"Engine / Exhaust Package","Drivetrain Packages","Tires","E.C.U. & Fuel System","Turbo Package","Brake Kits","Weight Reduction Kits","Suspension Package","Nitrous Oxide"};
        const char* icons[]={"PERFORMANCE_ENGINE","PERFORMANCE_TRANSMISSION","PERFORMANCE_TIRES","PERFORMANCE_ECU","PERFORMANCE_TURBO","PERFORMANCE_BRAKES","PERFORMANCE_WEIGHT_REDUCTION","PERFORMANCE_SUSPENSION","PERFORMANCE_NITROUS"};
        constexpr unsigned fields[]={2,6,7,5,3,8,0,1,4};
        for(unsigned i=0;i<9;++i)add(names[i],icons[i],fields[i]);break;
    }
    case GaragePage::packages:
        title_="PERFORMANCE PACKAGE";
        for(unsigned level=0;level<4;++level) {
            const char* names[]={"Stock","Street Package","Pro Package","Extreme Package"};
            add(names[level],level?("GENERIC_LEVEL"+std::to_string(level)).c_str():"BROWSER_PERFORMANCE",level);
            auto& e=entries_.back();const auto category=unsigned(CareerTuningCatalog::performance_category(state_.field));
            e.owned=level==0||c.owns(p,category,level);e.installed=p.ride.performance[state_.field]==level;
            e.available=e.owned||t.category_available(category,level,p.status);e.price=e.owned?0:t.price(category,level);
            e.upgrade_level=level;
        }
        if(c.free_drive()) {
            add(p.ride.unique[state_.field]?"Unique upgrade: ON":"Unique upgrade: OFF","BROWSER_PERFORMANCE",4);
            entries_.back().owned=true;entries_.back().installed=p.ride.unique[state_.field];
        }break;
    case GaragePage::paint:
        title_="PAINT";add("Base","PAINT_MOD_BASE",30);add("Rims","PAINT_MOD_PART_RIMS",41);add("Spoiler","PAINT_MOD_PART_SPOILER",38);
        add("Engine Accent","PAINT_MOD_PART_ENGINE_ACCENT",37);add("Brake Calipers","PAINT_MOD_PART_BRAKE_CALIPERS",39);
        add("Muffler tip","PAINT_MOD_PART_MUFFLER_TIP",40);break;
    case GaragePage::vinyl_layers:
        title_="VINYL LAYERS";for(unsigned i=0;i<4;++i){add(("Layer "+std::to_string(i+1)).c_str(),"VISUAL_PART_VINYL",34-i);}break;
    case GaragePage::vinyl_groups:case GaragePage::vinyl_types: {
        // Native4E40D0/4E4C40: group, then normal/hood family, then design.
        // Career lists locked families; Customize Ride filters them out.
        const auto available=[&](unsigned type) {
            return std::any_of(db.records.begin(),db.records.end(),[&](const auto& part) {
                return part.slot==31&&vinyl_in_family(part,type)&&t.find_part(p.car,state_.slot,part.part_name_hash)==&part&&
                    (t.part_available(part,p.status,p.style_high_water)||c.owns(p,7,t.ownership_key(part)));
            });
        };
        constexpr unsigned normal[]={0,2,4,5,7,9,11,12,14},hood[]={1,3,6,8,10,13};
        if(state_.page==GaragePage::vinyl_groups) {
            title_="CHOOSE GROUP";
            const char* names[]={"Side","Hood","Manufacturer","Body","Unique"};
            const char* icons[]={"VINYL_GROUP_MANUFACTURAR_NAME","VINYL_GROUP_MANUFACTURAR_HOOD_LOGO","VINYL_GROUP_MANUFACTURAR_LOGO","VINYL_GROUP_BODY_VINYL","VINYL_GROUP_UNIQUES"};
            for(unsigned i=0;i<5;++i) {
                const bool unlocked=i==0?std::any_of(std::begin(normal),std::end(normal),available):
                    i==1?std::any_of(std::begin(hood),std::end(hood),available):available(i==2?16:i==3?17:15);
                if(c.free_drive()&&!unlocked)continue;
                add(names[i],"",i);
                entries_.back().icon=bin_hash(icons[i]);
                entries_.back().available=i<2||unlocked;
            }
        }else {
            title_="VINYL";
            for(auto type:state_.group==0?std::vector<unsigned>(std::begin(normal),std::end(normal)):
                std::vector<unsigned>(std::begin(hood),std::end(hood))) {
                const bool unlocked=available(type);
                if(c.free_drive()&&!unlocked)continue;
                add(vinyl_groups[type],"",type);
                entries_.back().icon=career_vinyl_icon(type);
                entries_.back().available=unlocked;
            }
        }
        break;
    }
    case GaragePage::decals_zones: {
        title_="DECALS";
        const char* names[]={"Hood","Windshield","Rear Window","Left Door","Right Door","Left Quarter","Right Quarter"};
        for(unsigned zone:{1U,2U,3U,4U,5U,6U,0U})add(names[zone],decal_zones[zone],zone);
        break;
    }
    case GaragePage::decals_sizes:
        title_="DECAL LAYOUT";
        for(bool large:{false,true}) {
            const auto part=decal_layout(p,state_.group,large);
            if(!t.find_part(p.car,19+state_.group,part))continue;
            add(large?"Layout 2":"Layout 1",decal_layout_icon(state_.group,large).c_str(),part);
            entries_.back().installed=p.ride.installed[19+state_.group]==part;
        }
        break;
    case GaragePage::decals_brands: {
        title_="DECAL BRAND";
        add("No Decal","GENERICPLACEHOLDER",0);entries_.back().owned=true;
        entries_.back().installed=p.ride.installed[state_.slot]==0;
        for(const auto& part:db.records)if(part.slot==58) {
            // 511B00/511B70 pair each sponsor's black and white records.
            auto key=part.hash_0c;
            const bool extreme=key==bin_hash("EXTREMEDIMENSION");
            // Native sponsor starts at color0, which takes the white record at +18.
            const auto& black=state_.black_decals;
            const bool white=std::find(black.begin(),black.end(),key)==black.end();
            const char* suffix=white?"_WHITE_DECAL":"_BLACK_DECAL";
            if(extreme)key=bin_hash(white?"EXTRDIMENSION_WHITE_DECAL":"EXTRDIMENSION_BLACK_DECAL");
            else for(const char* letter=suffix;*letter;++letter)key=key*33+static_cast<unsigned char>(*letter);
            if(key!=part.part_name_hash)continue;
            const auto icon=career_decal_brand_icon(part.hash_0c);
            const bool unlocked=c.free_drive()||t.part_available(part,p.status,p.style_high_water);
            if(!icon||!unlocked)continue;
            auto name=part.name;
            if(name.size()>=8&&(name.compare(name.size()-8,8," (White)")==0||
                name.compare(name.size()-8,8," (Black)")==0))name.resize(name.size()-8);
            add(name.c_str(),"",part.hash_0c);
            auto& entry=entries_.back();entry.icon=icon;entry.part=part.part_name_hash;
            entry.installed=p.ride.installed[state_.slot]==entry.part;
            entry.price=c.part_price(p,part);
            entry.owned=part.upgrade_level==0||c.owns(p,1,t.ownership_key(part));
        }
        break;
    }
    case GaragePage::decals_slots: {
        title_="DECAL SLOT";
        const auto zone=state_.group;
        const bool large=state_.layout==decal_layout(p,zone,true);
        const auto wide=decal_widebody(t,p);
        const auto* fitment=t.decal_fitment(bin_hash(p.car));
        const bool door=zone==3||zone==4;
        const auto count=door?(wide?(fitment?unsigned((*fitment)[wide-1]):0U):6U):
            zone==0?(large?8U:4U):(large?2U:1U);
        for(unsigned i=0;i<count;++i) {
            const auto image=door&&wide?unsigned((*fitment)[4+6*(wide-1)+i]):i+1;
            if(!image)continue;
            const auto icon=decal_slot_icon(zone,large,wide!=0,image);
            add(("Slot "+std::to_string(i+1)).c_str(),icon.c_str(),59+8*zone+i);
        }
        break;
    }
    case GaragePage::rims:
        title_="RIMS";
        //50CAA3 enumerates the original WHEEL_MANUFACTURERS records.
        for(const auto& part:db.records)if(part.car_type_hash==bin_hash("WHEEL_MANUFACTURERS")) {
            const auto* installed=t.find_part(p.car,17,p.ride.installed[17]);
            GarageEntry e;e.name=part.name;e.value=part.part_name_hash;e.icon=bin_hash("VISUAL_RIMS_BRAND_"+part.name);
            e.installed=installed&&installed->hash_0c==e.value;
            entries_.push_back(std::move(e));
        }
        if(c.free_drive())for(const auto& part:db.records)if(part.slot==17&&part.upgrade_level==4&&t.find_part(p.car,17,part.part_name_hash)==&part&&t.wheel_fits(p.car,part)&&
            std::none_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.value==part.hash_0c;})) {
            GarageEntry e;e.name="Unique rims";e.value=part.hash_0c;e.icon=bin_hash("VISUAL_PART_RIMS");entries_.push_back(std::move(e));
        }break;
    case GaragePage::rim_styles: {
        title_="RIM STYLE";
        std::map<unsigned,std::size_t> families;
        for(const auto& part:db.records)if(t.find_part(p.car,17,part.part_name_hash)==&part&&part.upgrade_level&&part.upgrade_level<=4&&part.hash_0c==state_.group) {
            const bool installed=p.ride.installed[17]==part.part_name_hash;
            if(!t.wheel_fits(p.car,part,!installed))continue;
            const auto key=t.ownership_key(part);
            const auto category=unsigned(CareerTuningCatalog::slot_category(part.slot));
            const bool owned=c.owns(p,category,key);
            if(part.upgrade_level==4&&!owned)continue;
            const bool available=owned||t.part_available(part,p.status,p.style_high_water);
            const auto family=families.emplace(key,entries_.size());
            if(family.second)entries_.emplace_back();
            auto& e=entries_[family.first->second];
            if(!family.second&&!installed&&(e.installed||e.available||!available))continue;
            e.name=part.name.substr(0,part.name.find_last_of(' '));
            e.value=key;e.part=part.part_name_hash;e.upgrade_level=part.upgrade_level;
            e.rim_diameter=attribute(db,part,0xEB0101E2U)&255;
            e.owned=owned;e.available=available;e.installed=installed;e.price=c.part_price(p,part);
            e.icon=bin_hash("VISUAL_PART_RIMS");
        }
        break;
    }
    case GaragePage::parts:case GaragePage::rim_sizes:case GaragePage::colors: {
        const auto* name=car_slot_name(int(state_.slot));title_=name?name:"PARTS";
        std::replace(title_.begin(),title_.end(),'_',' ');
        if(state_.slot>=31&&state_.slot<=34)title_="VINYL";
        if(state_.slot>=59&&state_.slot<=114)title_="DECALS";
        GarageEntry stock;stock.name="Stock";stock.icon=part_icon(state_.slot);stock.owned=true;
        stock.installed=p.ride.installed[state_.slot]==0;stock.upgrade_level=0;
        if(state_.page==GaragePage::parts&&(state_.slot<31||state_.slot>34))entries_.push_back(std::move(stock));
        for(const auto& part:db.records) {
            if(!part.upgrade_level)continue;
            if(t.find_part(p.car,state_.slot,part.part_name_hash)!=&part)continue;
            if(state_.slot>=31&&state_.slot<=34&&!vinyl_in_family(part,state_.group))continue;
            if(state_.page==GaragePage::colors&&part.hash_0c!=state_.group)continue;
            if(state_.page==GaragePage::rim_sizes&&(t.ownership_key(part)!=state_.group||!t.wheel_fits(p.car,part,false)))continue;
            GarageEntry e;e.name=part.name;e.part=part.part_name_hash;e.value=part.index;
            if(state_.page!=GaragePage::colors)e.upgrade_level=part.upgrade_level;
            if(state_.page==GaragePage::rim_sizes)e.rim_diameter=attribute(db,part,0xEB0101E2U)&255;
            const auto category=unsigned(CareerTuningCatalog::slot_category(part.slot));
            e.owned=part.upgrade_level==0||c.owns(p,category,t.ownership_key(part));
            if(part.upgrade_level==4&&!e.owned&&!(part.slot==31&&part.byte_12==15))continue;
            e.installed=p.ride.installed[state_.slot]==e.part;e.available=e.owned||t.part_available(part,p.status,p.style_high_water);
            e.price=c.part_price(p,part);
            e.icon=state_.slot>=31&&state_.slot<=34?career_vinyl_icon(part.byte_12):part_icon(state_.slot);
            if(!e.icon)e.icon=attribute(db,part,bin_hash("NAME"));
            entries_.push_back(std::move(e));
        }
        if(state_.page==GaragePage::rim_sizes)
            std::stable_sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.rim_diameter<b.rim_diameter;});
        if(state_.page==GaragePage::colors) {
            std::stable_sort(entries_.begin(),entries_.end(),[&](const auto& a,const auto& b) {
                return t.find_part(p.car,state_.slot,a.part)->upgrade_level<t.find_part(p.car,state_.slot,b.part)->upgrade_level;
            });
            unsigned counts[4]{};
            for(auto& e:entries_) {
                const auto level=t.find_part(p.car,state_.slot,e.part)->upgrade_level;
                e.value=(level<=1?0:level==2?10:30)+counts[std::min(3U,unsigned(level))]++;
            }
        }
        break;
    }
    }
    if(state_.page==GaragePage::body) {
        for(auto& entry:entries_)entry.icon=part_icon(entry.value);
    }
    // Native5048E0 shows the authored arrow for the highlighted entry.
    // 5A4420/5A4480 use the first matching reward of the current event.
    if(!c.free_drive()&&p.new_parts_event&&p.new_parts_event<p.status.size()&&
       p.status[p.new_parts_event]==kEventStatusWon)if(const auto* rewards=c.media(p.new_parts_event)) {
        const auto level=[&](int category)->unsigned {
            if(category<0||unsigned(category)>=rewards->new_part_levels.size())return 0;
            const auto value=rewards->new_part_levels[unsigned(category)];
            return category>=16&&value==4?0:value;
        };
        const auto slot_level=[&](unsigned slot){return level(CareerTuningCatalog::slot_category(int(slot)));};
        const auto field_level=[&](unsigned field){return level(CareerTuningCatalog::performance_category(field));};
        const auto any_slot=[&](std::initializer_list<unsigned> slots) {
            return std::any_of(slots.begin(),slots.end(),[&](unsigned slot){return slot_level(slot)!=0;});
        };
        const bool body=any_slot({1,2,6,7,8,10,11}),accessories=any_slot({13,14,15,115,116});
        for(auto& entry:entries_)switch(state_.page) {
        case GaragePage::root:
            entry.newly_unlocked=entry.value==0?(body||accessories||any_slot({17,30,31,58})):
                entry.value==1&&std::any_of(rewards->new_part_levels.begin()+16,rewards->new_part_levels.end(),
                    [](unsigned value){return value>0&&value<4;});
            break;
        case GaragePage::visual: {
            const bool flags[]={body,slot_level(17)!=0,accessories,slot_level(30)!=0,slot_level(58)!=0,slot_level(31)!=0};
            entry.newly_unlocked=flags[entry.value];break;
        }
        case GaragePage::body:case GaragePage::accessories:
            entry.newly_unlocked=slot_level(entry.value)!=0;break;
        case GaragePage::performance:
            entry.newly_unlocked=field_level(entry.value)!=0;break;
        case GaragePage::packages:
            entry.newly_unlocked=entry.value!=0&&entry.value==field_level(state_.field);break;
        case GaragePage::parts:case GaragePage::rim_styles:case GaragePage::rim_sizes:
            if(const auto* part=t.find_part(p.car,state_.slot,entry.part)) {
                const auto newly=slot_level(part->slot);
                entry.newly_unlocked=entry.available&&newly&&part->upgrade_level==newly;
            }
            break;
        default:break;
        }
    }
    state_.selection=entries_.empty()?0:std::min(state_.selection,unsigned(entries_.size()-1));
    if(state_.page==GaragePage::colors&&!entries_.empty()) {
        if(state_.preview>=entries_.size())state_.preview=0;
        if(entries_[selected()].available)state_.preview=state_.selection;
    }
}
bool CareerGarage::accept(const CareerCatalog& c,CareerProfile& p,bool& purchased,std::string& error) {
    purchased=false;error.clear();if(entries_.empty())return true;const auto e=entries_[state_.selection];
    if(dialog_!=GarageDialog::none) {
        const auto dialog=dialog_;
        if(dialog==GarageDialog::vinyl_layer) {
            const auto layer=e.value;
            if(dialog_selection_==0) {
                State next;next.page=GaragePage::vinyl_groups;next.slot=layer;
                dialog_=GarageDialog::none;open_page(next,c,p);return true;
            }
            auto replacement=p;clear_vinyl_colors(replacement,layer);
            if(!c.buy_part(replacement,layer,0,error))return false;
            p=std::move(replacement);purchased=true;
        }else if(dialog!=GarageDialog::already_installed&&dialog_selection_==0) {
            auto replacement=p;
            if(dialog==GarageDialog::stock_hood) {
                if(!c.buy_part(replacement,8,0,error))return false;
            }else {
                // Native4E4840 clears designs only; unused remap slots remain.
                for(unsigned layer=31;layer<=34;++layer)
                    if(!c.buy_part(replacement,layer,0,error))return false;
            }
            p=std::move(replacement);purchased=true;
        }
        dialog_=GarageDialog::none;dialog_selection_=0;build(c,p);return true;
    }
    State next;next.slot=state_.slot;next.group=state_.group;next.field=state_.field;next.layout=state_.layout;
    switch(state_.page) {
    case GaragePage::unique_performance:case GaragePage::unique_parts:
        //4C74D0/4C8900 first dismiss the authored reward explanation.
        if(unique_intro_){unique_intro_=false;return true;}
        if(!confirm_){confirm_=true;return true;}
        if(!c.award_unique(p,e.value,e.part,error))return false;
        purchased=true;confirm_=false;return true;
    case GaragePage::root:
        if(e.value==2){next.page=GaragePage::trade_in;break;}
        next.page=e.value==0?GaragePage::visual:GaragePage::performance;break;
    case GaragePage::visual: {
        if(!e.available){error="This category is locked";return false;}
        constexpr GaragePage pages[]={GaragePage::body,GaragePage::rims,GaragePage::accessories,GaragePage::paint,GaragePage::decals_zones,GaragePage::vinyl_layers};next.page=pages[e.value];break;
    }
    case GaragePage::body:case GaragePage::accessories:
        next.page=GaragePage::parts;next.slot=e.value;break;
    case GaragePage::paint:
        if(e.value>=37&&e.value<=41) {
            constexpr unsigned physical[]={12,11,16,15,17};
            const auto slot=physical[e.value-37];const auto* component=c.tuning().find_part(p.car,slot,p.ride.installed[slot]);
            // Native50C5F0 also accepts installed engine/brake performance
            // packages; their stock visual part record can still have level0.
            const bool mechanical=(e.value==37&&p.ride.performance[2]>0)||(e.value==39&&p.ride.performance[8]>0);
            if((!component||!component->upgrade_level)&&!mechanical){error="You cannot paint stock parts.";return false;}
        }
        next.page=GaragePage::colors;next.slot=e.value;
        next.group=e.value==37?0x0016BDCEU:e.value==39?0xD6640DFFU:e.value==40?0x066350F0U:e.value==41?0x0000DA27U:0x02DAAB07U;break;
    case GaragePage::performance:next.page=GaragePage::packages;next.field=e.value;break;
    case GaragePage::rims:next.page=GaragePage::rim_styles;next.slot=17;next.group=e.value;break;
    case GaragePage::rim_styles:
        if(!e.available){error="This upgrade is locked";return false;}
        next.page=GaragePage::rim_sizes;next.slot=17;next.group=e.value;next.field=e.part;break;
    case GaragePage::vinyl_layers:
        if(p.ride.installed[e.value]){dialog_=GarageDialog::vinyl_layer;dialog_selection_=0;return true;}
        next.page=GaragePage::vinyl_groups;next.slot=e.value;break;
    case GaragePage::vinyl_groups: {
        if(!e.available){error="This category is locked";return false;}
        bool any=false,unique=false;
        for(unsigned layer=31;layer<=34;++layer)if(const auto hash=p.ride.installed[layer]) {
            any=true;const auto* part=c.tuning().find_part(p.car,layer,hash);
            unique|=part&&part->byte_12==15;
        }
        const auto* hood=c.tuning().find_part(p.car,8,p.ride.installed[8]);
        // Original4E4610 gates the selected group before opening its browser.
        if(e.value==1&&hood&&hood->name!="STOCK")dialog_=GarageDialog::stock_hood;
        else if(e.value==4&&any)dialog_=GarageDialog::remove_vinyls;
        else if(e.value!=4&&unique)dialog_=GarageDialog::remove_unique;
        if(dialog_!=GarageDialog::none){dialog_selection_=0;return true;}
        next.page=e.value<2?GaragePage::vinyl_types:GaragePage::parts;
        next.group=e.value<2?e.value:e.value==2?16:e.value==3?17:15;break;
    }
    case GaragePage::vinyl_types:
        if(!e.available){error="This category is locked";return false;}
        next.page=GaragePage::parts;next.group=e.value;break;
    case GaragePage::decals_zones: {
        const auto zone=e.value;
        const auto wide=decal_widebody(c.tuning(),p);
        next.group=zone;
        next.layout=decal_layout(p,zone,true);
        next.slot=59+zone*8;
        if(zone==0) {
            const auto* hood=c.tuning().find_part(p.car,8,p.ride.installed[8]);
            if(hood&&hood->upgrade_level) {error="Hood decals require the stock hood.";return false;}
        }
        if(zone==1||zone==2||(wide&&zone>=5))next.page=GaragePage::decals_brands;
        else if(zone==3||zone==4)next.page=GaragePage::decals_slots;
        else next.page=GaragePage::decals_sizes;
        break;
    }
    case GaragePage::decals_sizes:next.page=GaragePage::decals_slots;next.layout=e.value;break;
    case GaragePage::decals_slots:next.page=GaragePage::decals_brands;next.slot=e.value;break;
    case GaragePage::trade_in:
        if(e.trade_cost>0&&p.bank<unsigned(e.trade_cost)){error="Insufficient career funds";return false;}
        // 4C352E opens stats for the current car. 50950F then switches
        // to the prepared replacement, followed by the trade confirmation.
        if(trade_review_<2){++trade_review_;return true;}
        if(!confirm_){confirm_=true;dialog_selection_=0;return true;}
        if(dialog_selection_){confirm_=false;dialog_selection_=0;return true;}
        if(!c.trade(p,e.name,error))return false;
        purchased=true;confirm_=false;trade_review_=0;state_={};parents_.clear();build(c,p);return true;
    case GaragePage::packages:case GaragePage::parts:case GaragePage::rim_sizes:case GaragePage::colors:case GaragePage::decals_brands:
        if(!e.available){error="This upgrade is locked";return false;}
        if(state_.page==GaragePage::parts&&state_.slot>=31&&state_.slot<=34&&e.installed) {
            dialog_=GarageDialog::already_installed;dialog_selection_=0;return true;
        }
        if(state_.page==GaragePage::colors&&state_.channel+1<state_.channels) {
            remember_color_cursor();
            draft_colors_[state_.channel]=e.part;++state_.channel;++state_.slot;state_.selection=state_.preview=0;build(c,p);
            restore_color_cursor();
            return true;
        }
        if(!confirm_){confirm_=true;return true;}
        if(state_.page==GaragePage::packages) {
            if(!c.buy_performance(p,state_.field,e.value,error))return false;
        }else {
            auto next_profile=p;
            if(state_.slot>=31&&state_.slot<=34&&e.part!=p.ride.installed[state_.slot])
                clear_vinyl_colors(next_profile,state_.slot);
            if(state_.slot>=59&&state_.slot<=114&&state_.layout&&
                !c.buy_part(next_profile,19+(state_.slot-59)/8,state_.layout,error))return false;
            if(state_.page==GaragePage::colors&&state_.slot>=42&&state_.slot<=57) {
                draft_colors_[state_.channel]=e.part;
                for(unsigned i=0;i<state_.channels;++i)if(draft_colors_[i]&&
                    !c.buy_part(next_profile,state_.slot-state_.channel+i,draft_colors_[i],error))return false;
            }else if(!c.buy_part(next_profile,state_.slot,e.part,error))return false;
            p=std::move(next_profile);
        }
        purchased=true;confirm_=false;build(c,p);return true;
    }
    open_page(next,c,p);return true;
}
}
