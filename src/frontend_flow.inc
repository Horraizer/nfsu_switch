// Original MU_Pause_PC, MU_PauseConfirm and PC_LoadSave object packages.
namespace {
DialogArt read_dialog(const std::vector<unsigned char>& image,std::size_t at,std::vector<unsigned> resources,
    const std::function<std::string(unsigned,const std::string&)>& label,std::string& error) {
    if(at+16>image.size())throw std::runtime_error("dialog package absent");
    std::vector<unsigned char> fng;std::size_t used=0;
    if(!jdlz_decompress(image.data()+at,dword(image.data()+at+12),1024*1024,fng,used,error))throw std::runtime_error("dialog package decode");
    DialogArt out;out.resources=std::move(resources);
    for(std::size_t pos=0;pos+16<=fng.size();++pos) {
        if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
        const auto n=dword(fng.data()+pos+12),outer=dword(fng.data()+pos+4);
        if(n>fng.size()-pos-16||outer<n+8||outer>fng.size()-pos-8)throw std::runtime_error("dialog object bounds");
        std::vector<unsigned char> bytes(fng.begin()+pos,fng.begin()+pos+16+n);
        const unsigned clipped=n+8;std::memcpy(bytes.data()+4,&clipped,4);
        auto o=decode_ui_object(bytes,0);
        auto& script=out.scripts[o.id];script.assign(fng.begin()+pos+16+n,fng.begin()+pos+8+outer);
        if(o.type==2&&o.language!=0xffffffffU)o.text=label(o.language,o.text);
        out.objects.push_back(std::move(o));pos+=7+outer;
    }
    if(out.objects.empty())throw std::runtime_error("dialog objects absent");
    return out;
}
}
void FrontendArt::decode_startup_splash(const std::vector<unsigned char>& front,std::string& error) {
    // LS_Splash_PC.fng in the authenticated FrontB image.
    const auto art=read_dialog(front,0xbb5d8c,{},
        [](unsigned,const std::string& fallback){return fallback;},error);
    auto& p=*impl_;
    for(auto object:art.objects) {
        const auto& script=art.scripts.at(object.id);
        post_state(object,script.data(),script.size(),"Init");
        if(object.name=="LS_Splash_PC")p.splash_picture=std::move(object);
        else if(object.name=="mouse_click")p.splash_prompt=std::move(object);
    }
    if(p.splash_picture.type!=1||p.splash_prompt.type!=2||
       !texture(front,bin_hash("LS_SPLASH_PC"),p.startup_splash,error))
        throw std::runtime_error("original startup splash artwork absent");
}
void FrontendArt::draw_startup_splash(ScreenImage image)const {
    const auto& p=*impl_;
    fill_screen_rect(image,0,0,image.width,image.height,{0,0,0,255});
    const auto& picture=p.splash_picture;
    quad(Canvas{image},p.startup_splash,combine({},picture,false),
        -picture.w/2,-picture.h/2,picture.w,picture.h,picture.uv,picture.color);
    const auto& prompt=p.splash_prompt;
    p.text(Canvas{image},prompt,combine({},prompt,true),"Press A or + to continue",prompt.color,0);
}
void FrontendArt::release_startup_splash() {impl_->startup_splash={};}

bool FrontendArt::load_profile_pages(const std::filesystem::path& root,std::string& error) {
    try {
        std::vector<unsigned char> image,language;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"FrontEnd/FrontB.lzc",image,issues)||
           sha256_hex(sha256(image.data(),image.size()))!="94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77")throw std::runtime_error("profile art identity mismatch");
        if(!load_race_image(root/"Languages/English.bin",language,issues))throw std::runtime_error("profile language unreadable");
        decode_profiles(image,language,error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void FrontendArt::decode_profiles(const std::vector<unsigned char>& image,const std::vector<unsigned char>& language,std::string& error) {
    if(sha256_hex(sha256(language.data(),language.size()))!="6CBAB64235523149B95966B1844CDCA9FB858744126F8D9AD10E72C921AB8790")throw std::runtime_error("profile language identity mismatch");
    const auto label=[&](unsigned hash,const std::string& fallback) {
        for(unsigned i=0;i<dword(language.data()+12);++i) {
            const auto at=8+dword(language.data()+16)+8*i;
            if(dword(language.data()+at)==hash){const auto p=8+dword(language.data()+20)+dword(language.data()+at+4);return ascii(language.data()+p,language.size()-p);}
        }return fallback;
    };
    impl_->dialogs[2]=read_dialog(image,0xbdf40c,{impl_->large_hash,0xe2a5c626U,0x1abedeb1U},label,error);
}
void FrontendArt::Impl::draw_dialog(Canvas canvas,unsigned page,const PostRaceValues& values,unsigned selected,unsigned tick,unsigned fade_out)const {
    const auto& art=dialogs.at(page);auto objects=art.objects;
    const bool extra_options=page==0&&values.count("OptionName_6");
    if(extra_options) {
        for(const std::string prefix:{"OptionName_","Button_"}) {
            const auto last=std::find_if(objects.begin(),objects.end(),[&](const auto& object){return object.name==prefix+"4";});
            if(last==objects.end())continue;
            const auto prototype=*last;
            for(unsigned row=5;row<=6;++row) {
                auto option=prototype;
                option.name=prefix+std::to_string(row);
                objects.push_back(std::move(option));
            }
        }
    }
    for(auto& o:objects) {
        const auto& script=art.scripts.at(o.id);
        post_state(o,script.data(),script.size(),"Init",tick);
        if(page==5&&o.name=="TYPE GROUP")post_state(o,script.data(),script.size(),"FadeIn",tick);
        if(fade_out!=~0U)post_state(o,script.data(),script.size(),"FadeOut",fade_out);
        bool button=false,high=false;
        for(unsigned n=1;n<=(page==2?8:page==1||page==3?2:extra_options?6:4);++n) {
            const auto number=std::to_string(n),zero=std::string(n<10?"0":"")+number;
            if(o.name=="Button_"+number||o.name=="OptionName_"+number||o.name=="button"+number||o.name=="Savehilight_"+zero) {button=true;high=selected==n-1;}
        }
        if(button) {post_state(o,script.data(),script.size(),"UnHighlight");post_state(o,script.data(),script.size(),"Unhighlight");
            if(high){post_state(o,script.data(),script.size(),"Highlight");post_state(o,script.data(),script.size(),"HighLight");}}
    }
    if(extra_options) {
        constexpr float scale=3.F/5.F;
        for(const std::string prefix:{"OptionName_","Button_"}) {
            const auto first=std::find_if(objects.begin(),objects.end(),[&](const auto& object){return object.name==prefix+"1";});
            const auto last=std::find_if(objects.begin(),objects.end(),[&](const auto& object){return object.name==prefix+"4";});
            if(first==objects.end()||last==objects.end())continue;
            const float top=first->y,bottom=last->y;
            for(auto& object:objects)if(object.name.rfind(prefix,0)==0) {
                if(object.name==prefix+"5")object.y=top+(bottom-top)*4.F/5.F;
                else if(object.name==prefix+"6")object.y=bottom;
                else object.y=top+(object.y-top)*scale;
                if(prefix=="Button_")object.h*=scale;
            }
        }
    }
    const auto transform=[&](auto&& self,const Object& o,unsigned depth)->Transform {
        if(depth>32)throw std::runtime_error("dialog parent cycle");
        Transform parent;
        if(o.parent){const auto it=std::find_if(objects.begin(),objects.end(),[&](const auto& v){return v.id==o.parent;});if(it==objects.end())throw std::runtime_error("dialog parent missing");parent=self(self,*it,depth+1);}
        if(page<4)return combine(parent,o,o.type==2||o.type==5);
        const auto r=frontend_info_rotation(o.qx,o.qy,o.qz,o.qw);
        const float sx=o.type==2||o.type==5?o.w:1,sy=o.type==2||o.type==5?o.h:1;
        Transform t;
        t.a=(parent.a*r[0]+parent.c*r[2])*sx;t.b=(parent.b*r[0]+parent.d*r[2])*sx;
        t.c=(parent.a*r[1]+parent.c*r[3])*sy;t.d=(parent.b*r[1]+parent.d*r[3])*sy;
        t.x=parent.x+parent.a*o.x+parent.c*o.y;t.y=parent.y+parent.b*o.x+parent.d*o.y;t.z=parent.z+o.z;
        return t;
    };
    std::vector<std::size_t> order(objects.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){
        const auto za=transform(transform,objects[a],0).z,zb=transform(transform,objects[b],0).z;
        return za==zb&&page>=4?a>b:za>zb;
    });
    for(auto i:order) {
        const auto& o=objects[i];if(o.type==5||o.name=="quit_tag"||o.name=="back_tag"||o.name=="next_tag"||o.name=="DisplayMenuBar"||o.name=="OptionsIcon"||o.name=="AcceptText"||o.text=="quit tag"||o.text=="Next"||o.text=="Back")continue;
        if(page==2) {
            bool hidden=false;for(unsigned n=1;n<=8;++n)if(o.name=="Savehilight_0"+std::to_string(n)&&selected!=n-1)hidden=true;
            if(hidden)continue;
        }
        if(page>=4) {
            const auto hidden=[&](auto&& self,const Object& value,unsigned depth)->bool {
                if(depth>32)throw std::runtime_error("career information hidden parent cycle");
                if(value.name=="help"||value.name=="BottomMenu"||value.name=="HelpIcon"||value.name.rfind("ConduitMdITC_",0)==0)return true;
                if(value.name=="UNRANKED_GROUP"&&values.at("Unranked")=="0")return true;
                if(value.name=="mouse_over_L"&&values.at("LeftEnabled")=="0")return true;
                if(value.name=="mouse_over_R"&&values.at("RightEnabled")=="0")return true;
                if(value.parent)for(const auto& parent:objects)if(parent.id==value.parent)return self(self,parent,depth+1);
                return false;
            };
            if(hidden(hidden,o,0))continue;
        }
        const auto inherited=[&](auto&& self,const Object& value,unsigned depth)->std::array<unsigned,4> {
            if(depth>32)throw std::runtime_error("dialog color parent cycle");
            auto color=value.color;
            if(value.parent)for(const auto& parent:objects)if(parent.id==value.parent) {
                const auto p=self(self,parent,depth+1);for(unsigned c=0;c<4;++c)color[c]=(color[c]*p[c]+128)>>8;break;
            }return color;
        };
        auto color=inherited(inherited,o,0);const auto m=transform(transform,o,0);
        if(page>=4&&!frontend_info_depth_visible(m.z))continue;
        if(o.type==2) {
            const auto it=values.find(o.name);auto value=it==values.end()?o.text:it->second;
            auto label=o;
            if(value.find('^')!=std::string::npos){std::replace(value.begin(),value.end(),'^','\n');label.wrap_width=600;}
            text(canvas,label,m,value,color,o.resource<art.resources.size()&&art.resources[o.resource]==small_hash?8:0);
        }else if(o.resource<art.resources.size()) {
            const auto tex=textures.find(art.resources[o.resource]);if(tex!=textures.end())quad(canvas,tex->second,m,-o.w/2,-o.h/2,o.w,o.h,o.uv,color,o.type==9?&o.corner_colors:nullptr);
        }
    }
    Object label;Transform footer;label.justification=0;footer.x=-280;footer.y=218;
    const auto hint=values.find("Controls");if(hint!=values.end())text(canvas,label,footer,hint->second,{{242,233,207,255}},8);
    const auto notice=values.find("Notice");if(notice!=values.end()&&!notice->second.empty()){footer.y=190;text(canvas,label,footer,notice->second,{{255,210,120,255}},8);}
}
namespace {
PostRaceValues pause_values(const RacePauseMenu& menu,unsigned& page,unsigned& selected) {
    PostRaceValues values;page=0;selected=menu.selected();
    const char* names[]={"Resume Race","Restart Race","Options","Leave Race"};
    for(unsigned n=0;n<4;++n)values["OptionName_"+std::to_string(n+1)]=names[n];
    values["Controls"]="A: Accept   B / +: Back";
    if(menu.page()==RacePausePage::restart||menu.page()==RacePausePage::leave) {
        page=1;selected=menu.confirmed()?0:1;
        values["message_blurb"]=menu.page()==RacePausePage::restart?
            "Restart Race? ^Restarting causes you to lose any style ^points earned during this race. Continue?":
            menu.practice()?"Quit Race?":menu.tournament()?
            "Quit Tournament? ^Quitting a tournament causes you to lose ^your tournament progress. Continue?":
            "Quitting out of a race counts as a^loss in your stats. Would you^like to continue?";
        values["button1"]="OK";values["button2"]="Cancel";
    }else if(menu.page()==RacePausePage::options) {
        selected=menu.option();const char* options[]={"Music Volume","Effects Volume","HUD",""};
        for(unsigned n=0;n<4;++n)values["OptionName_"+std::to_string(n+1)]=options[n];
        values["OptionName_1"]+="  "+std::to_string(menu.music())+"%";
        values["OptionName_2"]+="  "+std::to_string(menu.effects())+"%";
        values["OptionName_3"]+=menu.hud()?"  ON":"  OFF";
        values["Controls"]="Left / Right: Adjust   B: Back";
    }
    return values;
}
}
void FrontendArt::record_options(unsigned width,unsigned height,const RacePresentationOptions& options,
    unsigned selected,const std::string& notice,FrontendUiFrame& frame)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    const PostRaceValues values{
        {"OptionName_1",options.motion_blur?"Motion Blur   ON":"Motion Blur   OFF"},
        {"OptionName_2",options.low_car_reflections?"Car Reflections   LOW":"Car Reflections   HIGH"},
        {"OptionName_3",options.road_reflections?"Road Reflections   ON":"Road Reflections   OFF"},
        {"OptionName_4",options.metric_speed?"Speed Units   KM/H":"Speed Units   MPH"},
        {"OptionName_5",options.race_height==540?"Race Resolution   540p":
                        options.race_height==720?"Race Resolution   720p":"Race Resolution   AUTO"},
        {"OptionName_6",options.diagnostic_logs?"Diagnostic Logs   ON":"Diagnostic Logs   OFF"},
        {"Controls","Left / Right: Adjust   B: Back"},{"Notice",notice}};
    impl_->draw_dialog(Canvas{width,height,impl_->atlas,frame},0,values,selected,600);
}
void FrontendArt::draw_pause(ScreenImage image,const RacePauseMenu& menu)const {
    unsigned page=0,selected=0;const auto values=pause_values(menu,page,selected);
    impl_->draw_dialog(Canvas{image},page,values,selected,600);
}
void FrontendArt::record_pause(unsigned width,unsigned height,const RacePauseMenu& menu,FrontendUiFrame& frame,unsigned fade_out)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    unsigned page=0,selected=0;const auto values=pause_values(menu,page,selected);
    const unsigned key=page+8*unsigned(menu.page())+64*menu.revision();
    if(key!=impl_->dialog_key){impl_->dialog_key=key;impl_->dialog_started=impl_->ui_ticks;}
    const unsigned tick=impl_->temporal?unsigned(std::min<std::uint64_t>(impl_->ui_ticks-impl_->dialog_started,0xffffff)):600;
    impl_->draw_dialog(Canvas{width,height,impl_->atlas,frame},page,values,selected,tick,fade_out);
}
namespace {
PostRaceValues profile_values(const std::vector<CareerProfileEntry>& entries,unsigned selected,const std::string& notice) {
    PostRaceValues values;
    const auto first=selected/8*8;
    values["title"]="Select Profile";
    values["Controls"]=entries.empty()?"A / X: Create Profile   B: Back":"A: Load Profile   X: Create Profile   B: Back";
    values["Notice"]=notice;
    for(unsigned n=0;n<8;++n) {
        const auto key=std::string("0")+std::to_string(n+1);
        values["GameID_"+key]=first+n<entries.size()?entries[first+n].name:"";
        values["GameDate_"+key]=first+n<entries.size()?entries[first+n].saved_at:"";
    }
    return values;
}
}
void FrontendArt::draw_profiles(ScreenImage image,const std::vector<CareerProfileEntry>& entries,unsigned selected,const std::string& notice)const {
    impl_->draw_dialog(Canvas{image},2,profile_values(entries,selected,notice),selected%8,600);
}
void FrontendArt::record_profiles(unsigned width,unsigned height,const std::vector<CareerProfileEntry>& entries,unsigned selected,
    const std::string& notice,FrontendUiFrame& frame)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    impl_->draw_dialog(Canvas{width,height,impl_->atlas,frame},2,profile_values(entries,selected,notice),selected%8,600);
}
