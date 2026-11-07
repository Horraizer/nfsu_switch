// Included inside frontend.cpp: uses its bounded FEng decoder and renderer.
namespace {
unsigned career_page_index(CareerPage page) {
    return page==CareerPage::briefing?4:page==CareerPage::choose_car?0:page==CareerPage::map?1:page==CareerPage::menu?2:3;
}
std::string career_language(const std::vector<unsigned char>& bytes,unsigned hash,const std::string& fallback) {
    if(bytes.size()<24)return fallback;
    const auto count=dword(bytes.data()+12),table=8+dword(bytes.data()+16),strings=8+dword(bytes.data()+20);
    if(table>bytes.size() || count>(bytes.size()-table)/8 || strings>bytes.size())return fallback;
    for(unsigned i=0;i<count;++i)if(dword(bytes.data()+table+8*i)==hash) {
        const auto at=std::size_t(strings)+dword(bytes.data()+table+8*i+4);
        if(at>=bytes.size())throw std::runtime_error("career language offset outside file");
        std::string result;
        for(auto i=at;i<bytes.size();++i) {
            const auto c=bytes[i];if(!c)return result;
            // The authenticated event78 English message has a CP1252
            // apostrophe92. The retained font/text path uses ASCII quotes.
            if(c==0x92)result+='\'';
            else if(c>=32&&c<=126)result+=char(c);
            else throw std::runtime_error("invalid original career language byte");
        }
        throw std::runtime_error("unterminated original career language string");
    }
    return fallback;
}
std::vector<Object> career_objects(const std::vector<unsigned char>& front,std::size_t offset,
                                   const std::vector<unsigned char>& language,std::string& error,bool animated=false) {
    std::vector<Object> result;std::vector<unsigned char> fng;std::size_t used=0;
    if(offset+16>front.size() || !jdlz_decompress(front.data()+offset,dword(front.data()+offset+12),
        1024*1024,fng,used,error))throw std::runtime_error("career FNG decompression failed");
    for(std::size_t pos=0;pos+16<=fng.size();++pos) {
        if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
        const auto n=dword(fng.data()+pos+12),outer=dword(fng.data()+pos+4);
        if(n>fng.size()-pos-16 || outer<n+8 || outer>fng.size()-pos-8)throw std::runtime_error("career object bounds");
        std::vector<unsigned char> bytes(fng.begin()+pos,fng.begin()+pos+16+n);
        const unsigned clipped=n+8;std::memcpy(bytes.data()+4,&clipped,4);
        auto o=decode_ui_object(bytes,0);
        // This mouse-arrow sprite is not submitted by the pad browser.
        // Its three-dimensional flip track is outside the retained 2D subset.
        if(o.name!="Select_arrow_right")post_state(o,fng.data()+pos+16+n,outer-n-8,"Init");
        const auto* script=fng.data()+pos+16+n;const auto length=outer-n-8;
        if(animated||o.name=="Unlocked_Icon")load_ui_loops(script,length,o);
        if(o.name=="LAPSNUMBER")post_state(o,script,length,"intro");
        if(o.name=="Description")post_state(o,script,length,"FadeIn");
        const bool selection=o.name.rfind("hightlight0",0)==0 || o.name=="MapScreen"||o.name=="Customize"||o.name=="UGRanking"||
            o.name=="Magazines"||o.name=="STATISTICS"||o.name=="SAVE"||o.name.rfind("HighlightGroup",0)==0;
        if(selection) {
            auto high=o;post_state(high,script,length,"HighLight");
            if(high.type==2 && high.language)high.text=career_language(language,high.language,high.text);
            high.name+="/selected";
            result.push_back(std::move(high));post_state(o,script,length,"UnHighLight");
        }
        if(o.type==2 && o.language)o.text=career_language(language,o.language,o.text);
        if(offset==0x21cd8c) {
            if(o.name=="Highlight_1")post_state(o,script,length,"Highlight");
            if(o.name=="Highlight_2")post_state(o,script,length,"Unhighlight");
            if(o.name=="button1") {post_state(o,script,length,"highlight");o.text=career_language(language,0x639,"OK");}
            if(o.name=="button2") {post_state(o,script,length,"unhighlight");o.text=career_language(language,0x541389e5,"Cancel");}
            if(o.name=="message_blurb")o.text=career_language(language,0x9da49222,"Would you like to choose this part?");
        }
        result.push_back(std::move(o));pos+=7+outer;
    }
    if(result.empty())throw std::runtime_error("career page has no objects");
    return result;
}
}
bool FrontendArt::load_career_pages(const std::filesystem::path& root,const CareerCatalog& catalog,std::string& error) {
    try {
        if(impl_->textures.empty())throw std::runtime_error("career pages require frontend fonts");
        std::vector<unsigned char> front,global,language,dynamic;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues) ||
           !load_race_image(root/"Global/GLOBALB.BUN",global,issues) ||
           !load_race_image(root/"Languages/English.bin",language,issues) ||
           !load_race_image(root/"Global/DYNTEX.BIN",dynamic,issues))throw std::runtime_error("career page input unreadable");
        if(sha256_hex(sha256(front.data(),front.size()))!="94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77")
            throw std::runtime_error("career FNG identity mismatch");
        std::array<std::vector<Object>,5> pages;
        const std::size_t offsets[]={0xbbdb0c,0xbc228c,0xbca10c,0xb7018c,0xbc368c};
        for(unsigned i=0;i<5;++i)pages[i]=career_objects(front,offsets[i],language,error);
        auto common=career_objects(front,0xb6dc0c,language,error,true);
        auto browser=career_objects(front,0xbd388c,language,error);
        auto scroller=career_objects(front,0xbd468c,language,error);
        std::map<std::size_t,Impl::GarageArt> garage_pages;
        const std::size_t garage_offsets[]={0xb7018c,0xb7100c,0xb7fc8c,0xc0060c,0xb80a8c,0xb8180c,0xbbe50c,
            0xb72f8c,0xb73c0c,0xb74c0c,0xb75b8c,0xbe4c0c,0xb7b18c,0xb7750c,0xb7680c,0xb7a40c,0xb7828c,0xbfb88c,0xbfc50c,0xbfd18c,0xbfde0c,0xbff68c,0xb71e0c,0xbcf70c,0xbd0d0c,0x21cd8c};
        auto textures=impl_->textures;
        const auto add=[&](const std::string& name) {
            const auto hash=bin_hash(name);if(textures.count(hash))return;
            Image image;if(!texture(front,hash,image,error) && !texture(global,hash,image,error) && !texture(dynamic,hash,image,error))
                throw std::runtime_error("career sprite absent: "+name);
            textures.emplace(hash,std::move(image));
        };
        const auto add_hash=[&](unsigned hash,bool required,unsigned thumbnail_limit=0) {
            if(!hash||textures.count(hash))return;
            Image image;
            if(texture(front,hash,image,error)||texture(global,hash,image,error)||texture(dynamic,hash,image,error)) {
                // Decal position art is only used in the thumbnail scroller.
                // Filter in premultiplied space to preserve transparent edges.
                while(thumbnail_limit&&std::max(image.width,image.height)>thumbnail_limit) {
                    const auto w=(image.width+1)/2,h=(image.height+1)/2;
                    std::vector<unsigned char> reduced(std::size_t(w)*h*4);
                    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                        unsigned weighted[3]{},alpha=0,samples=0;
                        for(unsigned v=2*y;v<std::min(image.height,2*y+2);++v)
                            for(unsigned u=2*x;u<std::min(image.width,2*x+2);++u) {
                                const auto* pixel=image.rgba.data()+(std::size_t(v)*image.width+u)*4;
                                for(unsigned k=0;k<3;++k)weighted[k]+=unsigned(pixel[k])*pixel[3];
                                alpha+=pixel[3];++samples;
                            }
                        auto* pixel=reduced.data()+(std::size_t(y)*w+x)*4;
                        if(alpha)for(unsigned k=0;k<3;++k)pixel[k]=static_cast<unsigned char>((weighted[k]+alpha/2)/alpha);
                        pixel[3]=static_cast<unsigned char>((alpha+samples/2)/samples);
                    }
                    image.width=w;image.height=h;image.rgba=std::move(reduced);
                }
                textures.emplace(hash,std::move(image));
            }
            else if(required)throw std::runtime_error("garage sprite absent hash="+std::to_string(hash));
            error.clear();
        };
        for(unsigned i=0;i<2;++i) {
            const std::size_t offset=i?0xbc818c:0xbc640c;
            auto art=read_dialog(front,offset,career_info_resources(front,offset,impl_->large_hash,impl_->small_hash,error),
                [&](unsigned hash,const std::string& fallback){return career_language(language,hash,fallback);},error);
            for(const auto& object:art.objects)if((object.type==1||object.type==9)&&object.resource<art.resources.size())
                add_hash(art.resources[object.resource],true);
            impl_->dialogs[4+i]=std::move(art);
        }
        add("GENERICPLACEHOLDER");
        for(auto offset:garage_offsets) {
            const auto& source=offset==0x21cd8c?global:front;
            Impl::GarageArt page;page.objects=career_objects(source,offset,language,error);
            std::vector<unsigned char> fng;std::size_t consumed=0;
            if(!jdlz_decompress(source.data()+offset,dword(source.data()+offset+12),1024*1024,fng,consumed,error))throw std::runtime_error(error);
            for(std::size_t pos=0;pos+8<=fng.size();++pos)if(!std::memcmp(fng.data()+pos,"RsNm",4)) {
                const auto length=dword(fng.data()+pos+4);if(length>fng.size()-pos-8)throw std::runtime_error("garage resource names outside FNG");
                const auto end=pos+8+length;
                for(auto at=pos+8;at<end;) {
                    auto path=ascii(fng.data()+at,end-at);at+=path.size()+1;
                    const auto slash=path.find_last_of("/\\");if(slash!=std::string::npos)path.erase(0,slash+1);
                    const auto dot=path.find_last_of('.');if(dot!=std::string::npos)path.resize(dot);
                    for(auto& ch:path)if(ch>='a'&&ch<='z')ch=char(ch-32);
                    // Authored PS2 help icons are not submitted on Switch;
                    // pad labels are bound by the current platform controls.
                    page.resources.push_back(path.rfind("PS2_",0)==0?0:bin_hash(path));
                }
                break;
            }
            if(page.resources.empty())throw std::runtime_error("garage original resources absent");
            for(const auto& o:page.objects)if((o.type==1||o.type==9)&&o.resource<page.resources.size())add_hash(page.resources[o.resource],true);
            garage_pages.emplace(offset,std::move(page));
        }
        // Category icons are small original sprites, independent of car geometry.
        add_hash(bin_hash("UNLOCKED_ICON"),true);
        {
            const char* icons[]={"BROWSER_VISUAL","BROWSER_PERFORMANCE","BROWSER_TRADE_IN","VISUAL_PART_BODY","VISUAL_PART_RIMS","VISUAL_PART_ACCESSORIES","VISUAL_PART_PAINT","VISUAL_PART_DECALS","VISUAL_PART_VINYL",
                "PERFORMANCE_ENGINE","PERFORMANCE_TRANSMISSION","PERFORMANCE_TIRES","PERFORMANCE_ECU","PERFORMANCE_TURBO","PERFORMANCE_BRAKES","PERFORMANCE_WEIGHT_REDUCTION","PERFORMANCE_SUSPENSION","PERFORMANCE_NITROUS",
                "VISUAL_PART_MUFFLERTIP","PAINT_MOD_BASE","PAINT_MOD_PART_RIMS","PAINT_MOD_PART_SPOILER","PAINT_MOD_PART_ENGINE_ACCENT","PAINT_MOD_PART_BRAKE_CALIPERS","PAINT_MOD_PART_MUFFLER_TIP"};
            for(auto name:icons)add_hash(bin_hash(name),false);
        }
        for(const auto icon:career_decal_menu_icons())add_hash(icon,true,64);
        for(unsigned layer=1;layer<=4;++layer)add_hash(bin_hash("VINYL_LAYER_"+std::to_string(layer)),true,64);
        for(const auto& part:catalog.tuning().parts().records)if(part.slot==58)
            add_hash(career_decal_brand_icon(part.hash_0c),true);
        for(unsigned level=1;level<4;++level) {
            add_hash(bin_hash("GENERIC_LEVEL"+std::to_string(level)),true);
            add_hash(bin_hash("UPGRADE_LEVEL_"+std::to_string(level)),true);
        }
        add_hash(bin_hash("UPGRADE_LEVEL_STOCK"),true);
        add_hash(bin_hash("UPGRADE_LEVEL_UNIQUE"),true);
        for(unsigned diameter=15;diameter<=21;++diameter)
            add_hash(bin_hash("RIM_SIZE_"+std::to_string(diameter)),true);
        for(const auto& part:catalog.tuning().parts().records)if(part.car_type_hash==bin_hash("WHEEL_MANUFACTURERS"))
            add_hash(bin_hash("VISUAL_RIMS_BRAND_"+part.name),false);
        for(unsigned hash:{0x015C767CU,0x4E6C2AF9U,0xBB034EA6U,0x028F7092U,0x79165861U,0x945E9BC5U,0x739C4A1BU,0x02929058U,0xF69AC384U,0x2CBB2D1CU,0x3F23165CU,0x0294D2A3U})add_hash(hash,true);
        for(const char* name:{"VINYL_GROUP_MANUFACTURAR_NAME","VINYL_GROUP_MANUFACTURAR_HOOD_LOGO","VINYL_GROUP_MANUFACTURAR_LOGO","VINYL_GROUP_BODY_VINYL","VINYL_GROUP_UNIQUES","VINYL_GROUP_AEM",
            "VINYL_GROUP_TEAR","VINYL_GROUP_TEAR_HOOD","VINYL_GROUP_STRIPES","VINYL_GROUP_STRIPES_HOOD","VINYL_GROUP_SPLASH","VINYL_GROUP_MODERN","VINYL_GROUP_MODERN_HOOD","VINYL_GROUP_FLAMES","VINYL_GROUP_FLAMES_HOOD","VINYL_GROUP_LIGHTNING","VINYL_GROUP_LIGHTNING_HOOD","VINYL_GROUP_FLAGS","VINYL_GROUP_TRIBAL","VINYL_GROUP_TRIBAL_HOOD","VINYL_GROUP_WILD"})add_hash(bin_hash(name),false);
        for(const char* name:{"HEAD_DIRT","BORDER_1","GRADIENT32X32","CREDZ","CAR_THUMB","WHITE16X16_NOALPHA",
                              "MODE_ICON_CIRCUIT","MODE_ICON_LAP_KNOCKOUT","MODE_ICON_DRIFT","MODE_ICON_DRAG","MODE_ICON_SPRINT",
                              "SCROLLING_ARROW","REP_STAR","WHITE16X16_PURE_WHITE","PERFORMANCE_LEVEL_INDICATOR"})add(name);
        //4C2B00/4C3120 bind native SECONDARY_LOGO, not the larger PC_ alias.
        // Keep the complete career roster within the existing atlas byte cap.
        // Practice uses its independent car-name selector.
        if(!catalog.free_drive()) {
            for(const auto& car:catalog.cars()) {
                add("CARSELECT_MANUFACTURER_"+car.brand);
                add("SECONDARY_LOGO_"+car.name);
            }
        }
        // Original4BBC90/4B92A0 select the dynamic background and independent
        // TRACK_%d_SPLINE. The in-race TRACK%d_MAP is a different resource.
        std::map<unsigned,unsigned> maps,backgrounds;std::map<unsigned,std::string> titles,descriptions,track_names;
        std::map<unsigned,bool> sprint;
        const auto tracks=parse_track_info_table(global.data(),global.size());
        if(!tracks.ok())throw std::runtime_error("career track metadata unavailable");
        for(unsigned event=1;event<=kCareerLastEvent;++event) {
            const auto* record=find_race_event(catalog.events(),event);
            if(!record || record->races.empty())continue;
            if(const auto* media=catalog.media(event);media&&media->portrait&&!textures.count(media->portrait)) {
                Image head;if(!texture(front,media->portrait,head,error)&&!texture(global,media->portrait,head,error))
                    throw std::runtime_error("career sender portrait absent");
                textures.emplace(media->portrait,std::move(head));
            }
            const auto track=record->races[0].track;
            const auto* info=find_track_info(tracks,track);
            if(!info)throw std::runtime_error("career map track metadata absent");
            sprint[event]=(info->mode_mask&4)!=0;
            track_names[event]=career_language(language,bin_hash("TRACK_NAME_"+std::to_string(track)),"");
            if(track_names[event].empty())throw std::runtime_error("career track label absent");
            std::string back;
            if(track<1100 || (track>=1200 && track<1300))back="TRACK_"+std::to_string(track);
            else if(track>=1300)back=track<1305?"DRIFT_TRACKS_BACK":"DRIFT_TRACKS_BACK2";
            else {
                // Jump table4B9344 for1101..1109: second regional background
                // at entries0,1,5,6,7,8; the other entries select the first.
                const auto index=track-1101;
                back=index==0||index==1||(index>=5&&index<=8)?"SPRINT_TRACKS_BACK2":"SPRINT_TRACKS_BACK";
            }
            const auto spline="TRACK_"+std::to_string(track)+"_SPLINE";
            if(event<=15){add(back);add(spline);}maps[event]=bin_hash(spline);backgrounds[event]=bin_hash(back);
            descriptions[event]=career_language(language,bin_hash("EVENT_"+std::to_string(event)+"_DESCRIPTION"),"");
            titles[event]=career_language(language,bin_hash("EVENT_"+std::to_string(event)+"_TITLE"),"Event "+std::to_string(event));
        }
        // Original50ADA0 selects these language keys by performance field.
        std::array<std::array<std::string,4>,9> contents;
        constexpr const char* fields[]={"WEIGHT","SUSP","ENG_EXHAUST","TURBO","NITROUS","ECU_FUEL","TRANS","TIRES","BRAKE"};
        for(unsigned field=0;field<9;++field)for(unsigned level=0;level<4;++level) {
            const auto key=level?"MOD_DESC_"+std::string(fields[field])+"_"+std::to_string(level):std::string("MOD_DESC_STOCK");
            contents[field][level]=career_language(language,bin_hash(key),"");
            if(contents[field][level].empty())throw std::runtime_error("original package contents absent");
        }
        impl_->garage_package_contents=std::move(contents);
        Impl packed;packed.textures=std::move(textures);packed.pack_atlas(true);
        impl_->garage_pages=std::move(garage_pages);
        impl_->career_objects=std::move(pages);impl_->garage_common=std::move(common);
        impl_->career_browser=std::move(browser);impl_->career_scroller=std::move(scroller);
        impl_->career_sprint=std::move(sprint);impl_->career_backgrounds=std::move(backgrounds);impl_->career_titles=std::move(titles);impl_->career_descriptions=std::move(descriptions);impl_->career_track_names=std::move(track_names);
        impl_->textures=std::move(packed.textures);impl_->atlas=std::move(packed.atlas);impl_->career_maps=std::move(maps);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void FrontendArt::Impl::draw_career(Canvas image,const CareerCatalog& catalog,const CareerProfile& profile,
                                   unsigned selection,const std::string& notice,bool controls,const CareerGarage* garage)const {
    ++sample_generation;
    const unsigned page=career_page_index(profile.page);
    if(page==4){auto background=profile;background.page=CareerPage::map;draw_career(image,catalog,background,selection,{},false);}
    const GarageArt* garage_art=page==3&&garage?&garage_pages.at(garage->fng()):nullptr;
    auto objects=garage_art?garage_art->objects:career_objects[page];
    if(page==4)for(auto& o:objects)if(o.name=="hightlight0"+std::to_string(profile.difficulty+1)) {
        const auto high=std::find_if(objects.begin(),objects.end(),[&](const auto& h){return h.name==o.name+"/selected";});
        if(high!=objects.end()){auto name=o.name;o=*high;o.name=std::move(name);}
    }
    // Resolve selection before hierarchical transforms, so child quads inherit
    // the selected original group rather than its invisible UnHighLight state.
    if(page==2) {
        const char* names[]={"MapScreen","Customize","UGRanking","Magazines","STATISTICS","SAVE"};
        const char* groups[]={"HighlightGroup06","HighlightGroup01","HighlightGroup02","HighlightGroup03","HighlightGroup04","HighlightGroup05"};
        for(auto& o:objects)if(o.name==names[selection%6]||o.name==groups[selection%6]) {
            const auto high=std::find_if(objects.begin(),objects.end(),[&](const auto& h){return h.name==o.name+"/selected";});
            if(high!=objects.end()){auto name=o.name;o=*high;o.name=std::move(name);}
        }
    }
    objects.erase(std::remove_if(objects.begin(),objects.end(),[](const auto& o){return o.name.find("/selected")!=std::string::npos;}),objects.end());
    if(garage_art) {
        const auto thumb=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.name=="master_thumb";});
        if(thumb!=objects.end())for(auto& o:objects)
            if(o.name=="GarageMenuHeading"||o.name=="GarageCategoryHeading") {
                o.x=thumb->x;o.y=thumb->y+thumb->h*.5F+2;o.justification=1;
            }
    }
    if(objects.empty())return;
    const auto white=textures.find(0xe2a5c626U);if(white==textures.end())return;
    const auto find_parent=[](const std::vector<Object>& list,unsigned id)->const Object* {
        for(const auto& o:list)if(o.id==id)return &o;
        return nullptr;
    };
    const auto pose=[&](auto&& self,const std::vector<Object>& list,const Object& o,unsigned depth)->Transform {
        if(depth>32)throw std::runtime_error("career parent cycle");
        Transform p;
        if(o.parent) {
            const auto* parent=find_parent(list,o.parent);
            if(!parent)throw std::runtime_error("career parent absent");
            p=self(self,list,*parent,depth+1);
        }
        const auto& sampled=list.data()==garage_common.data()?sample(o):o;
        return combine(p,sampled,o.type==2||o.type==5);
    };
    const auto color=[&](auto&& self,const std::vector<Object>& list,const Object& o,unsigned depth)->std::array<unsigned,4> {
        if(depth>32)throw std::runtime_error("career color cycle");
        // The browser's new-part arrow has its own repeating color/alpha
        // track: visibility pulses with the authored Init loop.
        auto c=(list.data()==garage_common.data()||o.name=="Unlocked_Icon"?sample(o):o).color;
        if(o.parent)if(const auto* p=find_parent(list,o.parent)) {
            const auto pc=self(self,list,*p,depth+1);
            for(unsigned i=0;i<4;++i)c[i]=(c[i]*pc[i]+128)>>8;
        }
        return c;
    };
    const auto label=[&](float x,float y,float scale,const std::string& value,std::array<unsigned,4> c,bool small_font=false) {
        Object o;o.justification=0;Transform m;m.x=x;m.y=y;m.z=-10;m.a=m.d=scale;text(image,o,m,value,c,small_font?8:0);
    };
    const auto available=catalog.available(profile);
    const auto* event=find_race_event(catalog.events(),profile.selected_event);
    const auto* media=catalog.media(profile.selected_event);
    InstalledPartSelection visual;
    const auto display_profile=garage?garage->preview(profile):profile;
    for(unsigned slot=0;slot<120;++slot)if(const auto hash=display_profile.ride.installed[slot])
        if(const auto* part=catalog.tuning().find_part(display_profile.car,slot,hash))visual.records[slot]=part->index;
    const float reputation=catalog.tuning().reputation(visual);
    const auto car=page==0?catalog.starters().at(selection%catalog.starters().size()).name:display_profile.car;
    const auto selected_car=std::find_if(catalog.cars().begin(),catalog.cars().end(),
        [&](const auto& entry){return entry.name==car;});
    if(selected_car==catalog.cars().end())throw std::runtime_error("career display car absent");
    auto displayed_ratings=selected_car->performance;
    if(page==3 && !catalog.performance_ratings(display_profile,displayed_ratings))
        throw std::runtime_error("installed performance ratings unavailable");
    auto installed_ratings=displayed_ratings;
    const bool package_preview=garage&&page==3&&garage->page()==GaragePage::packages;
    if(package_preview&&!catalog.performance_ratings(profile,installed_ratings))
        throw std::runtime_error("installed performance comparison unavailable");
    if(page==1) {
        // Native scroller uses two authored image planes with a separate
        // yellow spline and black/gradient modulation. No route recoloring.
        const bool sprint=career_sprint.at(profile.selected_event);
        for(const char* name:{"Image",sprint?"SprintImage":"TrackImage",sprint?"SprintEffect":"TrackEffect","White16x16","gradient32x32","gradient32x33"})
            for(const auto& o:career_scroller)if(o.name==name) {
                unsigned hash=0;
                if(o.name=="TrackImage"||o.name=="SprintImage")hash=career_backgrounds.at(profile.selected_event);
                else if(o.name=="TrackEffect"||o.name=="SprintEffect")hash=career_maps.at(profile.selected_event);
                else if(o.name.rfind("gradient",0)==0)hash=0x868a1fcfU;
                else hash=0xe2a5c626U;
                const auto& tex=textures.at(hash);const auto m=pose(pose,career_scroller,o,0);
                quad(image,tex,m,-o.w/2,-o.h/2,o.w,o.h,o.uv,o.color);
            }
    }
    const auto draw_page=[&](const std::vector<Object>& list,unsigned layer,const GarageArt* override_art=nullptr) {
        const auto* art=override_art?override_art:garage_art;
        const bool common=layer==1,browser=layer==2;
        std::vector<std::size_t> order(list.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return pose(pose,list,list[a],0).z>pose(pose,list,list[b],0).z;});
        for(auto index:order) {
            const auto& o=list[index];if(o.type==5)continue;
            if(o.name.find("tag")!=std::string::npos || o.name.rfind("mouse_",0)==0 || o.name=="DisplayMenuBar" ||
               o.name.rfind("ConduitMd",0)==0 || o.name.rfind("Help",0)==0 || (o.name.rfind("font_arial",0)==0 && !(page==1 && o.name=="font_arial")) ||
               o.name=="thumb" || o.name=="car_thumb" || o.name.rfind("thumb(",0)==0 || o.name=="master_thumb")continue;
            // Original runtime list controllers replace placeholder thumbs;
            // tutorial boxes and highlight groups are conditional, not all on.
            bool hidden=false;const Object* ancestor=&o;
            for(unsigned depth=0;ancestor && depth<33;++depth) {
                //4C73E0/4C8810 hide these whole subtrees after the first
                //accept; their children must not cover the part picker forever.
                if(profile.page==CareerPage::unique_reward&&garage&&!garage->unique_intro()&&layer==0&&
                   (ancestor->name=="blue_menu_backing"||ancestor->name=="BorderFade"||ancestor->name=="button"))hidden=true;
                if((page==4 && (ancestor->name=="ACCEPT_DECLINE" || ancestor->name=="hightlight04" || ancestor->name=="hightlight05")) || ancestor->name.rfind("tutorial_back",0)==0)hidden=true;
                if(art&&!common&&!browser) {
                    if(garage->page()==GaragePage::colors) {
                        if((ancestor->name=="Level2Locks"&&catalog.tuning().category_available(0,2,profile.status))||
                           (ancestor->name=="Level3Locks"&&catalog.tuning().category_available(0,3,profile.status))||
                           (ancestor->name=="VinylThumbBox"&&!(garage->slot()>=42&&garage->slot()<=57)))hidden=true;
                    }
                    const auto& items=garage->entries();const auto* item=items.empty()?nullptr:&items[garage->selected()];
                    if((ancestor->name=="LockedIcon"&&(!item||item->available))||
                       (ancestor->name=="UnlockedIcon"&&(!item||!item->available))||
                       (ancestor->name=="InstalledIcon"&&(!item||!item->installed)))hidden=true;
                }
                ancestor=ancestor->parent?find_parent(list,ancestor->parent):nullptr;
            }
            if(page==4&&(!media||!media->portrait)&&
               (o.name=="Heads"||o.name=="CharacterName"||o.name=="Messenger"))hidden=true;
            if(art&&!common&&!browser&&(o.name=="LockedIcon"||o.name=="UnlockedIcon"||o.name=="InstalledIcon")) {
                const auto& items=garage->entries();const auto* item=items.empty()?nullptr:&items[garage->selected()];
                if(!item||(o.name=="LockedIcon"&&item->available)||(o.name=="UnlockedIcon"&&!item->available)||
                   (o.name=="InstalledIcon"&&!item->installed))hidden=true;
            }
            if(hidden || (page==4 && (o.name=="ACCEPT"||o.name=="DECLINE")))continue;
            if(page==3 && !common && !browser && (o.name=="TopMenu(116)"||o.name=="TopMenu(112)"||
                o.name=="TopMenu(110)"||o.name=="TopMenu(72)"))continue;
            if(browser && (o.name=="Select_arrow_right" || o.name=="Select_arrow_left"))continue;
            if(browser&&o.name=="Unlocked_Icon"&&(!garage||garage->entries().empty()||
               !garage->entries()[garage->selected()].newly_unlocked))continue;
            if(common && ((o.name.find("Hypo_")!=std::string::npos&&!package_preview) ||
               (o.name=="SecondaryCarLogo"&&(!garage||garage->page()!=GaragePage::trade_in))))continue;
            auto value=o.text;auto c=color(color,list,o,0);auto m=pose(pose,list,o,0);
            if(garage&&garage->page()==GaragePage::vinyl_layers&&!common&&!browser) {
                const auto layer=garage->selected()+1;
                if(o.name=="COLOUR_VINYL_BUTTON")value=profile.ride.installed[35-layer]?"X: Color Vinyl":"";
                if(o.name=="Vinyl_Layer")value="Layer "+std::to_string(layer);
                if(o.name.rfind("mose_over_select_",0)==0&&o.name!="mose_over_select_"+std::to_string(layer))continue;
            }
            if(garage&&garage->page()==GaragePage::colors&&!common&&!browser) {
                const bool vinyl=garage->slot()>=42&&garage->slot()<=57;
                if(o.name=="DebugPaintDisplayName"||o.name=="OptionsIcon(89)"||
                   (!vinyl&&(o.name=="VinylThumb"||o.name=="VinylLayerBox")))continue;
                if(o.name.rfind("Materialpick_Arrow_",0)==0&&
                   ((garage->slot()!=30&&garage->slot()!=38)||
                    (!catalog.tuning().category_available(0,2,profile.status)&&!catalog.tuning().category_available(0,3,profile.status))))continue;
                if(o.name=="VinylLayerString") {
                    if(garage->slot()<42||garage->slot()>57)continue;
                    value="Color "+std::to_string(garage->color_channel()+1)+" / "+std::to_string(garage->color_channels());
                }
                const auto& entries=garage->entries();const auto index=garage->selected();
                if(o.name.rfind("ColorElement_",0)==0||o.name=="CurrentColor") {
                    auto i=index;
                    if(o.name!="CurrentColor") {
                        const auto cell=unsigned(std::stoul(o.name.substr(13)))-1;
                        const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.value==cell;});
                        if(found==entries.end())continue;
                        i=unsigned(found-entries.begin());
                    }
                    if(i>=entries.size())continue;
                    const auto* part=catalog.tuning().find_part(profile.car,garage->slot(),entries[i].part);
                    if(!part)continue;
                    const auto& db=catalog.tuning().parts();unsigned rgb[3]{};const char* channels[]={"BLUE","GREEN","RED"};
                    for(unsigned k=0;k<3;++k)for(int a=0;a<part->attribute_count;++a) {
                        const auto& v=db.attributes[part->attribute_index+unsigned(a)];if(v.key==bin_hash(channels[k]))rgb[k]=v.value;
                    }
                    for(unsigned k=0;k<3;++k)c[k]=entries[i].available?rgb[k]:rgb[k]/3;
                }
                if(o.name=="Cursor"&&index<entries.size()) {
                    const auto a=std::find_if(list.begin(),list.end(),[](const auto& v){return v.name=="ColorElement_1";});
                    const auto b=std::find_if(list.begin(),list.end(),[&](const auto& v){return v.name=="ColorElement_"+std::to_string(entries[index].value+1);});
                    if(a!=list.end()&&b!=list.end()){const auto first=pose(pose,list,*a,0),selected=pose(pose,list,*b,0);m.x+=selected.x-first.x;m.y+=selected.y-first.y;}
                }
                if(o.name=="ColorCategoryHeader") {
                    const auto family=garage->color_family();value=family==0x02DAAB07U?"Gloss":family==0x03437A52U?"Metallic":family==0x03797533U?"Pearlescent":"Color";
                }
                if(o.name.rfind("Price_",0)==0) {
                    const auto level=unsigned(o.name.back()-'0');value=std::to_string(catalog.tuning().price(0,level));
                }
                if(o.name=="BaseColour")value=garage->slot()==30?"BASE COLOUR":"PART COLOUR";
            }
            //4B0FD0: only the current full-star tier is gold/scale.9.
            // The fractional meter has ten segments with native.15 threshold.
            if(common&&(o.name.rfind("rep_star_",0)==0||o.name.rfind("Rep_X",0)==0||o.name.rfind("RepFillMeter_bar",0)==0)) {
                unsigned number=0;for(char digit:o.name)if(digit>='0'&&digit<='9')number=number*10+unsigned(digit-'0');
                const float remainder=reputation-float(number?number-1:0);
                const bool meter=o.name.rfind("RepFillMeter_bar",0)==0;
                const bool active=meter?reputation-std::floor(reputation)-float(number)*.1F>=.15000000596046448F:
                    remainder>=1&&remainder<2;
                c=active?std::array<unsigned,4>{{0,191,183,c[3]}}:std::array<unsigned,4>{{192,128,0,c[3]}};
            }
            if(page==4) {
                if(o.name=="TrackName")value=career_track_names.at(profile.selected_event);
                if(o.name=="EventDescBlurb")value=career_descriptions.at(profile.selected_event);
                if(o.name=="CharacterName")value=media?media->sender:"";
                if(o.name=="Messenger")value="From:";
                if(o.name=="message_blurb")value="The Low-down:";
                const char* purses[]={"easy_purse","medium_purse","hard_purse"};
                for(unsigned d=0;d<3;++d)if(o.name==purses[d])value=std::to_string(catalog.media(profile.selected_event)->reward[d]);
                if(o.type==2 || o.name=="Heads")c[3]=255;
            }
            if(o.name=="cashola")value="Bank: "+std::to_string(profile.bank);
            if(o.name=="CarCost")value="Cost: "+std::to_string(catalog.starters().at(selection%catalog.starters().size()).cost);
            if(o.name=="hEADING")value=garage&&garage->page()==GaragePage::trade_in?"CHOOSE TRADE CAR":"CHOOSE A CAR";
            if(o.name=="UNDERGROUND_PERCENTAGE_COMPLETE")value="Progress: "+std::to_string(std::count(profile.status.begin(),profile.status.end(),kEventStatusWon))+" / 111";
            if(o.name=="LAPSNUMBER")value=event&&!event->races.empty()&&(event->type==0||event->type==3||event->type==5)?"Laps: "+std::to_string(event->races[0].laps):"";
            // 4C49F0 selects Tournament for kind 1 before consulting race type.
            if(o.name=="EventName")value=event?(event->kind==1?"Tournament":
                event->type==0?"Circuit":event->type==1?"Drag":event->type==3?"Drift":event->type==5?"Lap Knock Out":race_type_name(event->type)):"";
            if(o.name=="Message_Blurb")value=career_titles.at(profile.selected_event);
            if((o.name=="INCOMING_TEXT"&&!common) || o.name=="ACCEPT_MESSAGE_TEXT" || o.name=="button_icon")value="";
            if(common&&o.name=="Style_P1")value="Style: "+std::to_string(profile.style_points);
            if(o.name=="Description")value=selection==0?"Choose your next race":selection==1?"Customize your ride":"";
            if(garage&&page==3) {
                const auto& entries=garage->entries();const auto* item=entries.empty()?nullptr:&entries[garage->selected()];
                if(o.name=="GarageCategoryHeading"||o.name=="GarageMenuHeading"||o.name=="PartsCategoryHeading"||
                   o.name=="PerfCategoryHeading"||o.name=="PartsHeading"||o.name=="PerformanceHeading"||
                   o.name=="BrandHeading"||o.name=="StyleHeading"||o.name=="RimHeading"||
                   o.name=="GaragePaintModMenuHeading"||o.name=="GarageVinylMenuHeading"||
                   o.name=="GarageDecalsHeading"||o.name=="AccessoryCategoryHeading")value=item?item->name:"";
                if(o.name=="DecalColorText"&&garage->page()==GaragePage::decals_brands)
                    value=garage->white_decal()?"X: White":"X: Black";
                if(o.name.rfind("ModeTitle",0)==0)value=garage->title();
                if(o.name=="JunkmanUpgradeName"||o.name=="UniqueType")value=item?item->name:"";
                if(o.name=="PartsPrice")value=item?(item->installed?"INSTALLED":item->owned?"OWNED":!item->available?"LOCKED":"Cost: "+std::to_string(item->price)):"";
                if(o.name=="Cash")value="Bank: "+std::to_string(profile.bank);
                if(o.name=="CarName")value=display_profile.car;
                if(o.name=="TradeInCost")value=item?std::string(item->trade_cost<0?"Trade In Credit: ":"Trade In Cost: ")+std::to_string(std::abs(item->trade_cost)):"";
                if(o.name=="PlayerName")value=profile.name;
                if(o.name=="UpgradeBlurb")value=package_preview&&item&&item->value<4?
                    garage_package_contents.at(garage->performance_field()).at(item->value):"";
                if(o.name=="KIT_CONTENT_HEADER"&&!package_preview)value="";
                if(o.name=="Vinyl_layer_heading")value="CHOOSE LAYER";
                // FEng editor/platform tokens need a runtime binding, not
                // literal dollar-delimited identifiers on a controller UI.
                if(value.size()>1&&value.front()=='$'&&value.back()=='$')value="";
            }
            if(o.name=="Customize" && page==3)value="";
            if(o.name=="CustomizeButton" || o.name=="LoadingVinyl" || o.name=="PlayerName_P1")value="";
            if(common&&o.name.rfind("Rep_X",0)==0) {
                unsigned number=0;for(char digit:o.name)if(digit>='0'&&digit<='9')number=number*10+unsigned(digit-'0');
                const float remainder=reputation-float(number?number-1:0),scale=remainder>=1&&remainder<2?.9F:.75F;
                if(o.w){m.a*=scale/o.w;m.b*=scale/o.w;}if(o.h){m.c*=scale/o.h;m.d*=scale/o.h;}
            }
            if(o.type==2) {
                const unsigned small=common?3:page==0?3:page==1?0:page==2?1:page==4?0:2;
                // Original language strings use ^ to mark explicit line breaks.
                std::size_t begin=0;unsigned line=0;
                do {const auto end=value.find('^',begin);auto row=m;row.y+=float(line++)*22;
                    text(image,o,row,value.substr(begin,end==std::string::npos?end:end-begin),c,(art&&!common&&!browser&&o.resource<art->resources.size()?art->resources[o.resource]==small_hash:o.resource==small)?8:0);
                    if(end==std::string::npos)break;
                    begin=end+1;
                }while(begin<value.size());
                continue;
            }
            unsigned hash=0;
            if(browser) {
                if(o.name=="UpgradeLevelIcon") {
                    const auto level=garage&&!garage->entries().empty()?garage->entries()[garage->selected()].upgrade_level:~0U;
                    if(level>4)continue;
                    // 4F9BE1..4F9BFD: independent tier art on the browser chrome.
                    hash=level==0?bin_hash("UPGRADE_LEVEL_STOCK"):level==4?bin_hash("UPGRADE_LEVEL_UNIQUE"):
                        bin_hash("UPGRADE_LEVEL_"+std::to_string(level));
                }else if(o.name=="Unlocked_Icon")hash=bin_hash("UNLOCKED_ICON");
                else if(o.resource==1)hash=0xe2a5c626U;else if(o.resource==2)hash=0x1abedeb1U;
            }else if(common) {
                const unsigned hashes[]={0x868a1fcfU,0,0xe2a5c626U,0,bin_hash("REP_STAR"),bin_hash("WHITE16X16_PURE_WHITE"),0x1abedeb1U,bin_hash("CREDZ"),bin_hash("WHITE16X16_NOALPHA"),bin_hash("PERFORMANCE_LEVEL_INDICATOR"),0};
                if(o.resource<std::size(hashes))hash=hashes[o.resource];
            }else if(page==4) {
                const unsigned hashes[]={0,0xe2a5c626U,media?media->portrait:0U,bin_hash("BORDER_1"),bin_hash("WHITE16X16_NOALPHA"),bin_hash("SCROLLING_ARROW"),0x1abedeb1U,bin_hash("GRADIENT32X32")};
                if(o.resource<std::size(hashes))hash=hashes[o.resource];
            }else if(page==0||page==3) {if(o.resource==1)hash=0xe2a5c626U;}
            else {
                const unsigned hashes[]={0,0,0xe2a5c626U,0x1abedeb1U,page==1?0x868a1fcfU:bin_hash("CREDZ"),0,page==1?bin_hash("BORDER_1"):bin_hash("ARROW3"),bin_hash("NFSU_LOGO")};
                if(o.resource<std::size(hashes))hash=hashes[o.resource];
            }
            if(art&&!common&&!browser&&o.resource<art->resources.size())hash=art->resources[o.resource];
            if(o.name=="RimsSizeIcon"&&garage&&!garage->entries().empty()) {
                const auto diameter=garage->entries()[garage->selected()].rim_diameter;
                if(diameter<15||diameter>21)continue;
                hash=bin_hash("RIM_SIZE_"+std::to_string(diameter));
            }
            // Runtime4C3120/4AD7B0 replaces the FNG's authored sample logo.
            // Apply after static resource resolution so it cannot restore Supra.
            if(o.name=="SecondaryCarLogo")hash=bin_hash("SECONDARY_LOGO_"+car);
            if(art&&!common&&!browser&&garage) {
                unsigned layer=0;
                if(garage->page()==GaragePage::vinyl_layers&&
                    o.name.rfind("Vinyl_Thumb_",0)==0)
                    layer=35-unsigned(std::stoul(o.name.substr(12)));
                if(garage->page()==GaragePage::colors&&o.name=="VinylThumb"&&garage->slot()>=42&&garage->slot()<=57)
                    layer=31+(garage->slot()-42)/4;
                if(layer>=31&&layer<=34) {
                    const auto* part=catalog.tuning().find_part(profile.car,layer,profile.ride.installed[layer]);
                    hash=part?career_vinyl_icon(part->byte_12):bin_hash("VINYL_LAYER_"+std::to_string(35-layer));
                }
            }
            const auto tex=textures.find(hash);if(tex!=textures.end()) {
                float width=o.w,height=o.h,left=-o.w/2,top=-o.h/2;
                if(common && o.name.find("_BAR_P1")!=std::string::npos) {
                    const unsigned metric=o.name=="ACCEL_BAR_P1"?0:o.name=="TOP_SPEED_BAR_P1"?1:2;
                    //4AEEE0 changes width then restores the original left edge.
                    width*=installed_ratings[metric];
                }
                if(common&&package_preview&&o.name.find("Hypo_")!=std::string::npos) {
                    const unsigned metric=o.name=="AccelHypo_P1"?0:o.name=="TopSpeedHypo_P1"?1:2;
                    const char* bars[]={"ACCEL_BAR_P1","TOP_SPEED_BAR_P1","HANDLING_BAR_P1"};
                    const auto bar=std::find_if(list.begin(),list.end(),[&](const auto& v){return v.name==bars[metric];});
                    if(bar==list.end())throw std::runtime_error("hypothetical performance anchor absent");
                    // Native4AEF50 puts the indicator at the predicted bar endpoint.
                    const auto anchor=pose(pose,list,*bar,0);
                    m.x=anchor.x+anchor.a*bar->w*(displayed_ratings[metric]-.5F);
                    m.y=anchor.y;m.z=anchor.z-1;
                }
                if(common&&o.name.rfind("rep_star_",0)==0) {
                    unsigned number=0;for(char digit:o.name)if(digit>='0'&&digit<='9')number=number*10+unsigned(digit-'0');
                    const float remainder=reputation-float(number?number-1:0),scale=remainder>=1&&remainder<2?.9F:.75F;
                    //4F7430/4F74F0 use texture dimensions, not authored18x18.
                    width=float(tex->second.width)*scale;height=float(tex->second.height)*scale;
                    left=-width/2;top=-height/2;
                }
                quad(image,tex->second,m,left,top,width,height,o.uv,c,o.type==9?&o.corner_colors:nullptr);
            }
        }
    };
    if(page==0 || page==3)draw_page(garage_common,1);
    if(page!=2 && page!=4)draw_page(career_browser,2);
    draw_page(objects,0);
    if(page==0) {
        //4C2ED0/4C3240 clone master_thumb and bind each manufacturer's
        //original small logo. Placeholder thumbs are suppressed above.
        const auto master=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.name=="master_thumb";});
        if(master==objects.end())throw std::runtime_error("starter browser anchor absent");
        for(unsigned i=0;i<catalog.starters().size();++i) {
            const float x=float(int(i)-int(selection))*master->w;if(x< -200||x>200)continue;
            const auto& entry=catalog.starters()[i];
            const auto tex=textures.find(bin_hash("CARSELECT_MANUFACTURER_"+entry.brand));
            if(tex==textures.end())throw std::runtime_error("starter manufacturer icon absent");
            auto m=pose(pose,objects,*master,0);m.x+=x;
            const float factor=1.F-std::abs(x)*.0031250000465661287F;
            auto tint=color(color,objects,*master,0);tint[3]=unsigned(float(tint[3])*factor*factor*factor);
            const float w=master->w*factor,h=master->h*factor;
            quad(image,tex->second,m,-w/2,-h/2,w,h,master->uv,tint);
        }
    }
    if(page==3&&garage) {
        const auto master=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.name=="master_thumb";});
        if(master!=objects.end())for(unsigned i=0;i<garage->entries().size();++i) {
            const float x=float(int(i)-int(garage->selected()))*master->w;if(x< -200||x>200)continue;
            const auto& e=garage->entries()[i];auto tex=textures.find(e.icon);
            if(tex==textures.end())tex=textures.find(bin_hash("GENERICPLACEHOLDER"));
            if(tex==textures.end())continue;
            auto m=pose(pose,objects,*master,0);m.x+=x;
            const float factor=1.F-std::abs(x)*.0031250000465661287F;
            auto tint=color(color,objects,*master,0);tint[3]=unsigned(float(tint[3])*factor*factor*factor);
            if(!e.available){tint[0]/=2;tint[1]/=2;tint[2]/=2;}
            const float w=master->w*factor,h=master->h*factor;
            quad(image,tex->second,m,-w/2,-h/2,w,h,master->uv,tint);
        }
        if(garage->dialog()!=GarageDialog::none||garage->confirming()) {
            auto dialog=garage_pages.at(0x21cd8c);
            const auto kind=garage->dialog();
            const bool replace=kind==GarageDialog::vinyl_layer;
            const bool information=kind==GarageDialog::already_installed;
            const char* message=replace?"Replace This Vinyl or Remove Completely ?":
                kind==GarageDialog::remove_vinyls?"Remove all vinyls before ^installing a Unique vinyl.":
                kind==GarageDialog::remove_unique?"Remove Unique vinyl before ^installing any other vinyls.":
                kind==GarageDialog::stock_hood?"Hood vinyls can only be installed on^stock hoods. Continue and remove^your custom hood?":
                information?"That part is already installed.":
                profile.page==CareerPage::unique_reward?"Would you like to choose this part?":
                garage->page()==GaragePage::trade_in?"Trade this car?":"Install this part now?";
            const auto selected=garage->dialog_selected();
            const auto first=std::find_if(dialog.objects.begin(),dialog.objects.end(),[](const auto& o){return o.name=="button1";});
            const auto second=std::find_if(dialog.objects.begin(),dialog.objects.end(),[](const auto& o){return o.name=="button2";});
            if(selected&&first!=dialog.objects.end()&&second!=dialog.objects.end())std::swap(first->color,second->color);
            for(auto& o:dialog.objects) {
                if(o.name=="message_blurb")o.text=message;
                if(o.name=="button1")o.text=replace?"REPLACE":kind==GarageDialog::stock_hood?"Yes":"OK";
                if(o.name=="button2"){o.text=replace?"REMOVE":kind==GarageDialog::stock_hood?"No":"Cancel";if(information)o.color[3]=0;}
                if(o.name=="Highlight_1")o.color[3]=selected==0?255:0;
                if(o.name=="Highlight_2")o.color[3]=!information&&selected==1?255:0;
            }
            draw_page(dialog.objects,3,&dialog);
        }
    }
    if(page==1 && !available.empty()) {
        const auto master=std::find_if(objects.begin(),objects.end(),[](const Object& o){return o.name=="master_thumb";});
        const auto selected=std::find(available.begin(),available.end(),profile.selected_event);
        if(master==objects.end()||selected==available.end())throw std::runtime_error("career scroller selection/template absent");
        //4F8730 advances by each original thumb's unscaled width.4F8960
        //centers the selected offset;4F8B80 keeps centers within[-200,200],
        //scales by1-abs(x)/320 and cubes that factor for opacity.
        for(std::size_t i=0;i<available.size();++i) {
            const float x=float(int(i)-int(selected-available.begin()))*master->w;
            if(x< -200.F||x>200.F)continue;
            const auto* item=find_race_event(catalog.events(),available[i]);if(!item)continue;
            const char* icon=item->type==2?"MODE_ICON_SPRINT":item->type==1?"MODE_ICON_DRAG":item->type==3?"MODE_ICON_DRIFT":item->type==5?"MODE_ICON_LAP_KNOCKOUT":"MODE_ICON_CIRCUIT";
            const auto& tex=textures.at(bin_hash(icon));auto m=pose(pose,objects,*master,0);m.x+=x;
            const float factor=1.F-std::abs(x)*.0031250000465661287F;
            auto tint=color(color,objects,*master,0);tint[3]=unsigned(float(tint[3])*factor*factor*factor);
            const float width=master->w*factor,height=master->h*factor;
            quad(image,tex,m,-width/2,-height/2,width,height,master->uv,tint);
        }
    }
    if(controls)label(-290,216,.65F,garage&&garage->dialog()!=GarageDialog::none?"Left / Right: select    A: accept    B: cancel":page==4?"Left / Right: difficulty     A: start     B: race map":page==0?"Left / Right: car     A: buy     B: back":page==1?"Left / Right: event     A: briefing     B: Underground menu":page==2?"Up / Down: select     A: open     B: main menu":(garage&&garage->page()==GaragePage::colors?"D-pad: preview color   L / R: finish   A: select   B: back":garage&&garage->page()==GaragePage::vinyl_layers?"Left / Right: layer   ZL / ZR: reorder   X: color   A: select   B: back":"L/R: browse   Right stick: view   A: select   B: back"),{{205,235,240,255}},true);
    if(!notice.empty())label(-290,192,.62F,notice,{{255,224,140,255}},true);
}
void FrontendArt::draw_career(ScreenImage image,const CareerCatalog& catalog,const CareerProfile& profile,
                              unsigned selection,const std::string& notice,const CareerGarage* garage)const {
    impl_->draw_career(Canvas{image},catalog,profile,selection,notice,true,garage);
}
void FrontendArt::record_career(unsigned width,unsigned height,const CareerCatalog& catalog,const CareerProfile& profile,
                                unsigned selection,const std::string& notice,FrontendUiFrame& frame,const CareerGarage* garage)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;impl_->draw_career(Canvas{width,height,impl_->atlas,frame},catalog,profile,selection,notice,true,garage);
}

bool FrontendArt::load_underground_welcome(const std::filesystem::path& root,std::string& error) {
    try {
        if(impl_->textures.empty())throw std::runtime_error("welcome requires frontend fonts");
        std::vector<unsigned char> front,language,global;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues) ||
           !load_race_image(root/"Languages/English.bin",language,issues) ||
           !load_race_image(root/"Global/GLOBALB.BUN",global,issues))throw std::runtime_error("welcome input unreadable");
        if(sha256_hex(sha256(front.data(),front.size()))!="94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77")
            throw std::runtime_error("welcome FNG identity mismatch");
        decode_welcome(front,language,global,error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void FrontendArt::decode_welcome(const std::vector<unsigned char>& front,const std::vector<unsigned char>& language,
                                 const std::vector<unsigned char>& global,std::string& error) {
    auto objects=career_objects(front,0xbc528c,language,error);
    std::vector<unsigned char> fng;std::size_t consumed=0;
    if(!jdlz_decompress(front.data()+0xbc528c,dword(front.data()+0xbc528c+12),
        1024*1024,fng,consumed,error))throw std::runtime_error(error);
    std::map<unsigned,std::vector<unsigned char>> scripts;
    for(std::size_t pos=0;pos+16<=fng.size();++pos) {
        if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
        const auto n=dword(fng.data()+pos+12),outer=dword(fng.data()+pos+4);
        if(outer<n+8 || outer>fng.size()-pos-8)throw std::runtime_error("welcome script bounds");
        auto object=decode_ui_object(fng,pos);
        scripts[object.id]={fng.begin()+pos+16+n,fng.begin()+pos+8+outer};
        pos+=7+outer;
    }
    auto textures=impl_->textures;
    for(const char* name:{"WHITE16X16_NOALPHA","BORDER_1"}) {
        const auto hash=bin_hash(name);if(textures.count(hash))continue;
        Image image;if(!texture(front,hash,image,error) && !texture(global,hash,image,error))throw std::runtime_error("welcome sprite absent");
        textures.emplace(hash,std::move(image));
    }
    if(objects.size()!=35)throw std::runtime_error("welcome object count changed");
    Impl packed;packed.textures=std::move(textures);packed.pack_atlas(true);
    impl_->welcome_scripts=std::move(scripts);impl_->welcome_objects=std::move(objects);impl_->textures=std::move(packed.textures);impl_->atlas=std::move(packed.atlas);
}
void FrontendArt::Impl::draw_welcome(Canvas image,bool exit_selected,unsigned tick)const {
    auto list=welcome_objects;
    for(auto& o:list) {
        const auto script=welcome_scripts.find(o.id);
        if(script!=welcome_scripts.end())post_state(o,script->second.data(),script->second.size(),"Init",tick);
        if(o.name=="Highlight_1" || o.name=="Highlight_2")o.color[3]=((o.name=="Highlight_2")==exit_selected)?255:0;
        if(o.name=="Accept_button" || o.name=="Backbutton")o.color=((o.name=="Backbutton")==exit_selected)?
            std::array<unsigned,4>{{242,232,207,255}}:std::array<unsigned,4>{{180,154,84,255}};
        if(o.name=="button_icon")o.text="A";
        if(o.name=="button_icon(98)")o.text="B";
    }
    const auto pose=[&](auto&& self,const Object& o,unsigned depth)->Transform {
        if(depth>32)throw std::runtime_error("welcome parent cycle");
        Transform p;
        if(o.parent) {
            const auto parent=std::find_if(list.begin(),list.end(),[&](const auto& a){return a.id==o.parent;});
            if(parent==list.end())throw std::runtime_error("welcome parent absent");
            p=self(self,*parent,depth+1);
        }
        return combine(p,o,o.type==2||o.type==5);
    };
    const auto color=[&](auto&& self,const Object& o,unsigned depth)->std::array<unsigned,4> {
        if(depth>32)throw std::runtime_error("welcome color cycle");
        auto c=o.color;
        if(o.parent)for(const auto& p:list)if(p.id==o.parent) {
            const auto pc=self(self,p,depth+1);for(unsigned k=0;k<4;++k)c[k]=(c[k]*pc[k]+128)>>8;break;
        }
        return c;
    };
    std::vector<std::size_t> order(list.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return pose(pose,list[a],0).z>pose(pose,list[b],0).z;});
    const char* resources[]={"WHITE16X16","WHITE16X16_NOALPHA","","GRADIENT32X32","BORDER_1","NFSU_LOGO"};
    for(auto i:order) {
        const auto& o=list[i];if(o.type==5)continue;
        const auto c=color(color,o,0);auto m=pose(pose,o,0);
        // Keep this original 4:3 dialog centered without stretching its font
        // and logo across a widescreen viewport. The dimmer covers all pixels.
        if(o.name!="makesbackdarker") {
            const float fit=std::min(1.F,float(image.height)*4/(3*float(image.width)));
            m.x*=fit;m.a*=fit;m.c*=fit;
        }
        if(o.type==2) {
            std::size_t start=0;unsigned line=0;
            do {
                const auto end=o.text.find('^',start);auto row=m;
                row.y+=float(line++)*(large.line_height+(o.name=="TheText"?5:0))*m.d;
                text(image,o,row,o.text.substr(start,end==std::string::npos?end:end-start),c,0);
                if(end==std::string::npos)break;
                start=end+1;
            }while(start<o.text.size());
        }else if(o.resource<6 && o.resource!=2) {
            const auto found=textures.find(bin_hash(resources[o.resource]));
            if(found==textures.end())throw std::runtime_error("welcome texture binding absent");
            quad(image,found->second,m,-o.w/2,-o.h/2,o.w,o.h,o.uv,c,o.type==9?&o.corner_colors:nullptr);
        }
    }
}
void FrontendArt::draw_underground_welcome(ScreenImage image,bool exit_selected,unsigned tick)const {
    impl_->draw_welcome(Canvas{image},exit_selected,tick);
}
void FrontendArt::record_underground_welcome(unsigned width,unsigned height,bool exit_selected,FrontendUiFrame& frame,unsigned tick)const {
    frame.vertices.clear();frame.additive.clear();impl_->draw_welcome(Canvas{width,height,impl_->atlas,frame},exit_selected,tick);
}

bool FrontendArt::load_magazine(const std::filesystem::path& root,const CareerMagazineRecipe& recipe,std::string& error){
    try{
        std::vector<unsigned char> front,language,magazine;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues)||!load_race_image(root/"Languages/English.bin",language,issues)||
           !load_race_image(root/"FrontEnd/MAGAZINE_TEXTURES.BIN",magazine,issues))throw std::runtime_error("magazine presentation input unavailable");
        auto textures=impl_->textures;for(auto hash:impl_->magazine_textures)textures.erase(hash);
        // A cover and every campaign map need not be resident together.
        //Keep their metadata; reload the next selected map at the transition.
        for(const auto& entry:impl_->career_maps)textures.erase(entry.second);
        for(const auto& entry:impl_->career_backgrounds)textures.erase(entry.second);
        std::vector<unsigned> added;
        for(auto hash:{recipe.background,recipe.front,recipe.chick})if(hash&&!textures.count(hash)){
            Image image;if(!texture(magazine,hash,image,error))throw std::runtime_error("magazine authored texture absent");
            textures.emplace(hash,std::move(image));added.push_back(hash);
        }
        auto background=career_objects(front,0xc3680c,language,error);
        auto overlay=career_objects(front,0xc35b8c,language,error);
        //513C90 binds the dynamic front/model after the package is ready.
        for(auto& o:overlay)if(o.name=="MagazineFront"||o.name=="MagazineChick")o.color={{255,255,255,255}};
        for(auto& o:background)if(o.name=="MagazineBack")o.color={{255,255,255,255}};
        std::vector<unsigned char> package,global,dynamic;std::size_t consumed=0;
        if(!jdlz_decompress(front.data()+0xc35b8c,dword(front.data()+0xc35b8c+12),1024*1024,package,consumed,error)||
           !load_race_image(root/"Global/GLOBALB.BUN",global,issues)||!load_race_image(root/"Global/DYNTEX.BIN",dynamic,issues))throw std::runtime_error("magazine mask input unavailable");
        std::vector<unsigned> resources;
        for(std::size_t pos=0;pos+8<=package.size();++pos)if(!std::memcmp(package.data()+pos,"RsNm",4)){
            const auto n=dword(package.data()+pos+4);if(n>package.size()-pos-8)throw std::runtime_error("magazine resources truncated");
            for(auto at=pos+8;at<pos+8+n;){auto name=ascii(package.data()+at,pos+8+n-at);at+=name.size()+1;
                const auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name.erase(0,slash+1);
                const auto dot=name.find_last_of('.');if(dot!=std::string::npos)name.resize(dot);
                for(auto& c:name)if(c>='a'&&c<='z')c=char(c-32);resources.push_back(bin_hash(name));
            }break;
        }
        std::map<unsigned,unsigned> opaque_masks;
        for(const auto& o:overlay)if(o.type==1&&(o.name.rfind("White16x16_noalpha",0)==0||o.name.rfind("gradient32x3",0)==0)){
            if(o.resource>=resources.size())throw std::runtime_error("magazine mask resource absent");const auto hash=resources[o.resource];
            if(!textures.count(hash)){Image image;if(!texture(front,hash,image,error)&&!texture(global,hash,image,error)&&!texture(dynamic,hash,image,error))throw std::runtime_error("magazine mask sprite absent");textures.emplace(hash,std::move(image));added.push_back(hash);}
            //These black paper-border objects are opaque masks. The portable
            //overlay has one source-over state; normalize their white carrier
            //alpha instead of leaking the 3D car outside the authored border.
            if(o.name.rfind("White16x16_noalpha",0)==0&&!opaque_masks.count(hash)){
                // Keep the shared frontend carrier unchanged for every other
                //page; this temporary atlas entry retires with the cover.
                auto mask=textures.at(hash);for(std::size_t at=3;at<mask.rgba.size();at+=4)mask.rgba[at]=255;
                const auto private_hash=bin_hash("NFSU_PORT_MAGAZINE_MASK_"+std::to_string(hash));
                if(textures.count(private_hash))throw std::runtime_error("magazine mask atlas key collision");
                textures.emplace(private_hash,std::move(mask));opaque_masks.emplace(hash,private_hash);added.push_back(private_hash);
            }
        }
        for(auto& hash:resources)if(const auto at=opaque_masks.find(hash);at!=opaque_masks.end())hash=at->second;
        Impl packed;packed.textures=std::move(textures);packed.pack_atlas(true);
        impl_->textures=std::move(packed.textures);impl_->atlas=std::move(packed.atlas);
        impl_->magazine_textures=std::move(added);impl_->magazine_front=std::move(overlay);impl_->magazine_back=std::move(background);impl_->magazine_mask_resources=std::move(resources);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void FrontendArt::Impl::draw_magazine(Canvas image,const CareerMagazineRecipe& recipe,const std::string& car,bool background)const{
    const auto sprite=[&](const std::vector<Object>& objects,const char* name,unsigned hash){
        if(!hash)return;
        const auto at=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.name==name;});
        if(at==objects.end())throw std::runtime_error("magazine original image object absent");
        Transform transform;const auto pose=combine(transform,*at,false);
        quad(image,textures.at(hash),pose,-at->w/2,-at->h/2,at->w,at->h,at->uv,at->color);
    };
    if(background){sprite(magazine_back,"MagazineBack",recipe.background);return;}
    sprite(magazine_front,"MagazineChick",recipe.chick);
    sprite(magazine_front,"MagazineFront",recipe.front);
    // Native513C90 uses the selected caption strings/position/scale/color.
    //Caption word32 is reserved(-1), not a font selector.
    const auto* bytes=reinterpret_cast<const unsigned char*>(recipe.caption.data());
    std::string value;
    if(recipe.caption[33]){value=ascii(bytes,64)+car;if(recipe.caption[34])value+=ascii(bytes+64,64);}
    else value=ascii(bytes,64);
    Object label;label.justification=0;Transform pose;
    const auto real_word=[](std::uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;};
    pose.x=real_word(recipe.caption[35]);pose.y=real_word(recipe.caption[36]);pose.a=real_word(recipe.caption[37]);pose.d=real_word(recipe.caption[38]);
    const auto color=recipe.caption[39];
    text(image,label,pose,value,{{color&255,(color>>8)&255,(color>>16)&255,color>>24}},0);
    const auto transform=[&](auto&& self,const Object& o,unsigned depth)->Transform{
        if(depth>32)throw std::runtime_error("magazine mask parent cycle");Transform parent;
        if(o.parent){const auto p=std::find_if(magazine_front.begin(),magazine_front.end(),[&](const auto& p){return p.id==o.parent;});if(p==magazine_front.end())throw std::runtime_error("magazine mask parent absent");parent=self(self,*p,depth+1);}
        return combine(parent,o,o.type==2||o.type==5);
    };
    for(const auto& o:magazine_front)if(o.type==1&&(o.name.find("White16x16_noalpha")==0||o.name.rfind("gradient32x3",0)==0)){
        const auto hash=magazine_mask_resources.at(o.resource);const auto m=transform(transform,o,0);
        quad(image,textures.at(hash),m,-o.w/2,-o.h/2,o.w,o.h,o.uv,o.color);
    }
}

void FrontendArt::record_magazine(unsigned width,unsigned height,const CareerMagazineRecipe& recipe,
                                 const std::string& car,FrontendUiFrame& frame)const{
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    Canvas image{width,height,impl_->atlas,frame};
    impl_->draw_magazine(image,recipe,car,true);frame.background_quads=frame.additive.size();
    impl_->draw_magazine(image,recipe,car,false);
}
void FrontendArt::draw_magazine(ScreenImage image,const CareerMagazineRecipe& recipe,const std::string& car,bool background)const{
    impl_->draw_magazine(Canvas{image},recipe,car,background);
}

bool FrontendArt::load_career_event(const std::filesystem::path& root,unsigned event,std::string& error){
    try{
        const auto route=impl_->career_maps.at(event),background=impl_->career_backgrounds.at(event);
        if(impl_->textures.count(route)&&impl_->textures.count(background)){error.clear();return true;}
        auto textures=impl_->textures;for(auto hash:impl_->magazine_textures)textures.erase(hash);
        for(const auto& map:impl_->career_maps)if(map.second!=route&&map.second!=background)textures.erase(map.second);
        for(const auto& back:impl_->career_backgrounds)if(back.second!=route&&back.second!=background)textures.erase(back.second);
        std::vector<unsigned char> dynamic;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"Global/DYNTEX.BIN",dynamic,issues))throw std::runtime_error("career map reload input unavailable");
        for(auto hash:{route,background})if(!textures.count(hash)){
            Image image;if(!texture(dynamic,hash,image,error))throw std::runtime_error("selected career map sprite absent");textures.emplace(hash,std::move(image));
        }
        Impl packed;packed.textures=std::move(textures);packed.pack_atlas(true);
        impl_->textures=std::move(packed.textures);impl_->atlas=std::move(packed.atlas);impl_->magazine_textures.clear();error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
