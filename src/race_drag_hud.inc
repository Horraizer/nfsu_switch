// Runtime drag_HUD_test geometry and retail texture bindings.
// Included in race_hud.cpp; no retail texels are stored in the source tree.
namespace {
std::uint32_t drag_u32(const unsigned char* p) {
    return p[0]|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;
}
float drag_float(const unsigned char* p) {float v;std::memcpy(&v,p,4);return v;}
std::string drag_string(const unsigned char* p,std::size_t n) {
    const auto* e=static_cast<const unsigned char*>(std::memchr(p,0,n));
    if(!e)throw std::runtime_error("drag HUD string bounds");
    return {reinterpret_cast<const char*>(p),std::size_t(e-p)};
}
bool drag_visible_object(const std::string& n) {
    for(const char* name:{"321_Go","ShiftMessage","konopaoverheat(119)","PositionBacking","speed_backing","RaceTime","NumberRacers",
        "RacePositionNth","RacePositionNumber","3rdPersonSpeed","3rdPersonSpeedUnits",
        "3rdPersonGear","3rdPersonNeedle","Shift_light","Shift_light_back","Redline","rpm_fill","3rdPersonTach",
        "heat_fill","heat_lines","nos_fill","nos_lines","gear_fill",
        "3rdperson_Turbolines","3rdperson_TurboRed_1","3rdperson_TurboRed_2",
        "3rdperson_TurboDial","3rdperson_Turbofill","TurboPSI","TurboPlus","TurboMinus"})
        if(n==name)return true;
    return n.rfind("NOS_bar_6_",0)==0 || (n.size()==6&&n.rfind("heat_",0)==0&&n[5]>='1'&&n[5]<='8');
}
}
bool load_race_drag_hud(const std::filesystem::path& root,const std::string& car,const VehicleHandling& handling,
                        RaceHudAtlas& atlas,std::string& error,const std::string* second_car,
                        const VehicleHandling* second_handling) {
    try {
        if(bool(second_car)!=bool(second_handling))throw std::runtime_error("incomplete second drag HUD car");
        std::vector<unsigned char> bytes,dynt,language;
        std::vector<RaceTableIssue> language_issues;
        if(!load_race_image(root/"Languages/English.bin",language,language_issues)||language.size()<24)
            throw std::runtime_error("drag HUD language absent");
        const auto localized=[&](unsigned hash) {
            const std::size_t count=drag_u32(language.data()+12),table=8+drag_u32(language.data()+16),strings=8+drag_u32(language.data()+20);
            if(table>language.size()||count>(language.size()-table)/8||strings>=language.size())
                throw std::runtime_error("drag HUD language bounds");
            for(std::size_t k=0;k<count;++k)if(drag_u32(language.data()+table+k*8)==hash) {
                const auto at=strings+drag_u32(language.data()+table+k*8+4);
                if(at>=language.size())throw std::runtime_error("drag HUD label offset");
                return drag_string(language.data()+at,language.size()-at);
            }
            throw std::runtime_error("drag HUD label absent");
        };
        const auto packs=load_texture_pack_set(root/"Global/INGAMEB.BUN",bytes);
        const auto dynpacks=load_texture_pack_set(root/"Global/DYNTEX.BIN",dynt);
        if(!packs.ok()||!packs.complete||!dynpacks.ok()||!dynpacks.complete)
            throw std::runtime_error("drag HUD texture pack invalid");
        constexpr std::size_t offset=0x16d50c;
        if(bytes.size()<offset+16||std::memcmp(bytes.data()+offset,"JDLZ",4))
            throw std::runtime_error("drag HUD package absent");
        const auto compressed=drag_u32(bytes.data()+offset+12);
        if(compressed>bytes.size()-offset)throw std::runtime_error("drag HUD package bounds");
        std::vector<unsigned char> fng;std::size_t used=0;
        if(!jdlz_decompress(bytes.data()+offset,compressed,1024*1024,fng,used,error))return false;
        struct Raw {RaceDragHudObject object;unsigned id=0,parent=0,resource=65535;float x=0,y=0,z=0,w=1,h=1,qz=0,qw=1,px=0,py=0;};
        std::vector<Raw> objects;std::vector<std::string> resources;
        const unsigned char* names=nullptr;std::size_t name_size=0;
        for(std::size_t at=0;at+8<=fng.size();++at) {
            const auto* p=fng.data()+at;const auto n=drag_u32(p+4);
            if(!std::memcmp(p,"RsNm",4)) {
                if(n>fng.size()-at-8)throw std::runtime_error("drag resource names bounds");
                names=p+8;name_size=n;
            } else if(!std::memcmp(p,"RsRq",4)) {
                if(!names||n<4||n>fng.size()-at-8||n!=4+24*drag_u32(p+8))
                    throw std::runtime_error("drag resource table bounds");
                for(unsigned k=0;k<drag_u32(p+8);++k) {
                    const auto index=drag_u32(p+16+k*24);
                    if(index>=name_size)throw std::runtime_error("drag resource name offset");
                    resources.push_back(drag_string(names+index,name_size-index));
                }
            } else if(!std::memcmp(p,"FOb\xEA",4)) {
                if(n>fng.size()-at-8||n<8||std::memcmp(p+8,"ObjD",4))throw std::runtime_error("drag object bounds");
                const auto size=drag_u32(p+12);
                if(size>n-8)throw std::runtime_error("drag object fields bounds");
                Raw raw;
                for(std::size_t a=16;a<16+size;) {
                    if(16+size-a<4)throw std::runtime_error("drag object field header");
                    const unsigned len=p[a+2]|unsigned(p[a+3])<<8;const auto* v=p+a+4;
                    if(len>16+size-a-4)throw std::runtime_error("drag object field bounds");
                    const auto tag=[&](const char* t){return !std::memcmp(p+a,t,2);};
                    if(tag("On"))raw.object.name=drag_string(v,len);
                    else if(tag("Ot")&&len==4)raw.object.type=drag_u32(v);
                    else if(tag("OP")&&len==16){raw.id=drag_u32(v);raw.resource=drag_u32(v+12);}
                    else if(tag("PA")&&len==4)raw.parent=drag_u32(v);
                    else if(tag("Sj")&&len==4)raw.object.justification=drag_u32(v);
                    else if(tag("SH")&&len==4&&raw.object.name=="konopaoverheat(119)")
                        raw.object.text=localized(drag_u32(v));
                    else if(tag("SA")&&len>=68) {
                        for(unsigned k=0;k<4;++k)raw.object.color[k]=static_cast<unsigned char>(std::clamp<int>(int(drag_u32(v+(k<3?2-k:3)*4)),0,255));
                        raw.px=drag_float(v+16);raw.py=drag_float(v+20);
                        raw.x=drag_float(v+28);raw.y=drag_float(v+32);raw.z=drag_float(v+36);
                        raw.qz=drag_float(v+48);raw.qw=drag_float(v+52);raw.w=drag_float(v+56);raw.h=drag_float(v+60);
                        if(len>=84)for(unsigned k=0;k<4;++k)raw.object.uv[k]=drag_float(v+68+k*4);
                    }
                    a+=4+len;
                }
                objects.push_back(std::move(raw));at+=7+n;
            }
        }
        if(resources.size()!=43||objects.empty())throw std::runtime_error("drag HUD resource set changed");
        auto next=atlas;
        if(!load_drag_hud_animations(bytes,next.drag_animations,error))return false;
        next.drag_shift_labels={localized(0x27d2dd45),localized(0x0598b065),
                                localized(0x2303d76c),localized(0x27d75cd8)};
        //4A7D60 selects these messages for terminal drag reasons6 and7.
        next.drag_failure_labels={localized(0xa4c878f6),localized(0xe1279c38)};
        // 4A7EA3 selects the label from the native one-based place table.
        next.drag_finish_places={localized(0x6d8c9ed2),localized(0x63e9977b),
                                 localized(0x86fbe3da),localized(0x329e4953)};
        next.drag_finish_time=localized(0xb713f571);
        std::vector<unsigned char> global;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"Global/GLOBALB.BUN",global,issues))throw std::runtime_error("drag car table unavailable");
        const auto cars=parse_car_type_table(global.data(),global.size());
        const auto rpm_angles=[&](const std::string& model) {
            const auto selected=std::find_if(cars.records.begin(),cars.records.end(),[&](const auto& v){return v.name==model;});
            if(!cars.ok()||selected==cars.records.end()||selected->offset+0xc14>global.size())
                throw std::runtime_error("drag car angles absent");
            // 4A57D0 uses CarType+C0C/C10 rather than the ordinary RPM bands.
            std::array<float,2> angles{drag_float(global.data()+selected->offset+0xc0c),
                                       drag_float(global.data()+selected->offset+0xc10)};
            for(float v:angles)if(!std::isfinite(v))throw std::runtime_error("drag car angle invalid");
            return angles;
        };
        next.drag_rpm_angles=rpm_angles(car);
        if(second_car)next.second_drag_rpm_angles=rpm_angles(*second_car);
        next.height=1024;next.rgba.resize(kRaceHudAtlasWidth*next.height*4);
        next.drag_objects.clear();next.second_drag_objects.clear();
        // ShiftMessage uses Conduit38, including the early/late label alphabet.
        // Repack this font before reserving shared glyphs for drag textures.
        if(!load_lap_fonts(root,next,error,true))return false;
        struct Image {std::uint32_t hash=0;unsigned w=0,h=0;std::vector<unsigned char> rgba;std::array<int,4> rect{};};
        std::vector<Image> images;
        const auto texture=[&](std::uint32_t hash) {
            if(std::any_of(images.begin(),images.end(),[&](const Image& v){return v.hash==hash;}))return;
            for(unsigned source=0;source<2;++source)for(const auto& pack:(source?dynpacks:packs).packs)
                for(const auto& t:pack.textures)if(t.hash==hash) {
                    Image image;image.hash=hash;image.w=t.width;image.h=t.height;
                    std::vector<unsigned char> payload;std::size_t unused=0;std::uint64_t top=0;
                    const auto& data=source?dynt:bytes;
                    if(!t.valid||!texture_payload(data.data(),data.size(),t,payload,unused,error)||
                       !level_size(t.format,t.width,t.height,top)||top>payload.size()||
                       !decode_level(t.format,t.width,t.height,payload.data(),std::size_t(top),image.rgba,error))
                        throw std::runtime_error("drag HUD texture decode: "+error);
                    images.push_back(std::move(image));return;
                }
            throw std::runtime_error("drag HUD texture absent: "+std::to_string(hash));
        };
        std::array<std::vector<std::uint32_t>,2> hashes;
        const std::array<std::vector<RaceDragHudObject>*,2> dials{{&next.drag_objects,&next.second_drag_objects}};
        const auto build_dial=[&](unsigned dial,const VehicleHandling& tune) {
        for(const auto& raw:objects)if(drag_visible_object(raw.object.name)) {
            auto object=raw.object;object.width=raw.w;object.height=raw.h;
            object.position={raw.x,raw.y};object.pivot={raw.px,raw.py};
            std::array<const Raw*,32> ancestry{};unsigned count=0;const Raw* node=&raw;
            while(node) {
                if(count==ancestry.size())throw std::runtime_error("drag HUD parent cycle");
                ancestry[count++]=node;if(!node->parent)break;
                const auto parent=std::find_if(objects.begin(),objects.end(),[&](const Raw& v){return v.id==node->parent;});
                if(parent==objects.end())throw std::runtime_error("drag HUD parent absent");
                node=&*parent;
            }
            auto& m=object.transform;
            while(count) {
                const auto& v=*ancestry[--count];
                if(&v==&raw)object.parent_transform=m;
                const float c=1-2*v.qz*v.qz,s=2*v.qz*v.qw;
                const bool scale=v.object.type==5||v.object.type==2;
                const float sx=scale?v.w:1,sy=scale?v.h:1;
                //4F1810 composes position+pivot-rotation*pivot, then size.
                const float x=v.x+v.px-c*v.px+s*v.py,y=v.y+v.py-s*v.px-c*v.py;
                m={ (m[0]*c+m[2]*s)*sx,(m[1]*c+m[3]*s)*sx,
                    (m[2]*c-m[0]*s)*sy,(m[3]*c-m[1]*s)*sy,
                    m[4]+m[0]*x+m[2]*y,m[5]+m[1]*x+m[3]*y};
                object.depth+=v.z;
            }
            if(raw.resource>=resources.size())throw std::runtime_error("drag HUD resource absent");
            auto name=resources[raw.resource];name=name.substr(name.find_last_of('\\')+1);name=name.substr(0,name.find_last_of('.'));
            for(auto& c:name)if(c>='a'&&c<='z')c=char(c-'a'+'A');
            std::uint32_t hash=0;
            if(object.type==1) {
                //432A30 selects the same RPM bands, with the drag prefix.
                if(object.name=="3rdPersonTach")name="3RDPERSONDRAG_"+std::string(tune.max_rpm>=9500?"10500LINES":tune.max_rpm>=8500?"9500LINES":tune.max_rpm>=7500?"8500LINES":"7500LINES");
                if(object.name=="Redline") {
                    const char* suffix=tune.max_rpm>=9500?(tune.redline_rpm>=9000?"9000RED":"8500RED"):
                        tune.max_rpm>=8500?(tune.redline_rpm>=8000?"8000RED":"7500RED"):
                        tune.max_rpm>=7500?(tune.redline_rpm>=7000?"7000RED":tune.redline_rpm>=6500?"6500RED2":nullptr):
                        tune.redline_rpm>=6500?"6500RED":"6000RED";
                    if(!suffix)continue;
                    name="3RDPERSONDRAG_"+std::string(suffix);
                }
                if(object.name=="rpm_fill")name="3RDPERSONDRAG_RPMFILL";
                hash=bin_hash(name);texture(hash);
            }else object.font=name=="LCD_LET48"?2:name=="FONT_CONDUITMDITCTT38BI"?1:0;
            hashes[dial].push_back(hash);dials[dial]->push_back(std::move(object));
        }
        };
        build_dial(0,handling);
        if(second_handling)build_dial(1,*second_handling);
        std::stable_sort(images.begin(),images.end(),[](const Image& a,const Image& b){return a.h>b.h;});
        // Drag replaces the ordinary dial, so reclaim its texel rectangles.
        // Retain shared glyphs and the white finish backing; no extra image heap.
        std::array<unsigned,kRaceHudAtlasWidth> skyline{};
        const auto reserve=[&](unsigned x,unsigned y,unsigned w,unsigned h) {
            if(x+w>kRaceHudAtlasWidth||y+h>next.height)throw std::runtime_error("drag shared atlas bounds");
            for(unsigned k=x;k<x+w;++k)skyline[k]=std::max(skyline[k],y+h);
        };
        reserve(kLapWhite.x,kLapWhite.y,kLapWhite.w,kLapWhite.h);
        for(const auto& font:next.lap_fonts)for(const auto& g:font)if(g.width&&g.height)
            reserve(g.x,g.y,g.width+2,g.height+2);
        const unsigned height_limit=second_car?2048:1024;
        unsigned used_height=next.height;
        for(auto& image:images) {
            unsigned bestx=0,besty=height_limit+1;
            for(unsigned x=0;x+image.w<=kRaceHudAtlasWidth;++x) {
                unsigned y=0;for(unsigned k=0;k<image.w;++k)y=std::max(y,skyline[x+k]);
                if(y<besty){besty=y;bestx=x;}
            }
            if(besty+image.h>height_limit) {
                std::string detail;
                for(const auto& v:images)detail+=" "+std::to_string(v.hash)+":"+std::to_string(v.w)+"x"+std::to_string(v.h);
                throw std::runtime_error("drag HUD atlas capacity:"+detail);
            }
            image.rect={int(bestx),int(besty),int(image.w),int(image.h)};
            for(unsigned x=0;x<image.w;++x)skyline[bestx+x]=besty+image.h;
            used_height=std::max(used_height,besty+image.h);
        }
        next.height=(used_height+31)&~31U;
        next.rgba.resize(kRaceHudAtlasWidth*next.height*4);
        for(const auto& image:images) {
            const auto x=unsigned(image.rect[0]),y=unsigned(image.rect[1]);
            for(unsigned row=0;row<image.h;++row)
                std::memcpy(next.rgba.data()+((y+row)*kRaceHudAtlasWidth+x)*4,
                            image.rgba.data()+row*image.w*4,image.w*4);
        }
        for(unsigned dial=0;dial<dials.size();++dial) {
            auto& entries=*dials[dial];
            for(unsigned i=0;i<entries.size();++i)if(hashes[dial][i]) {
                const auto image=std::find_if(images.begin(),images.end(),[&](const Image& v){return v.hash==hashes[dial][i];});
                entries[i].texture=image->rect;
            }
            std::stable_sort(entries.begin(),entries.end(),[](const RaceDragHudObject& a,const RaceDragHudObject& b){return a.depth>b.depth;});
        }
        atlas=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void append_race_drag_hud(const RaceSession& world,const RaceHudAtlas& atlas,unsigned width,unsigned height,
                          RaceHudFrame& frame,unsigned player) noexcept {
    const auto& session=player?world.rival(player-1):world;
    const auto& objects=player?atlas.second_drag_objects:atlas.drag_objects;
    const auto& rpm_angles=player?atlas.second_drag_rpm_angles:atlas.drag_rpm_angles;
    const unsigned place=player?world.standings().position(player):world.player_position();
    const float scale=std::min(width/1280.F,height/720.F)*5.F/3.F;
    const float ox=float(width)*.5F,oy=float(height)*.5F;
    const bool turbo=has_turbo(session.spec());
    const auto nitrous=session.nitrous();
    unsigned full_nitrous_bars=0;
    unsigned char partial_nitrous_alpha=0;
    if(nitrous.capacity) {
        // 4A64B0 normalizes live charge across six authored bars, filled 6 to 1.
        constexpr float tick_seconds=1.F/4000.F;
        const float capacity=float(double(nitrous.capacity)*tick_seconds);
        double remaining=double(nitrous.remaining)*tick_seconds;
        if(nitrous.active)remaining=float(remaining);
        full_nitrous_bars=unsigned(std::min(remaining/capacity*6.0,6.0));
        const double bar_seconds=double(capacity)*(1.F/6.F);
        const float partial=float((remaining-full_nitrous_bars*bar_seconds)/bar_seconds);
        partial_nitrous_alpha=static_cast<unsigned char>(double(std::clamp(partial,0.F,1.F))*192.0);
    }
    const auto& car=session.car();const auto now=session.timing().now_ticks;
    if(frame.drag_gear!=car.gear) {
        if(frame.drag_gear>=1&&car.gear>frame.drag_gear) {
            frame.drag_shift_at=now;frame.drag_quality=car.transmission.shift_quality;
        }
        frame.drag_gear=car.gear;
    }
    const bool hot=int(car.engine.heat*8.F)>4;
    if(hot&&!frame.drag_hot)frame.drag_heat_at=now;
    frame.drag_hot=hot;
    //4A5200..4A5796: use published RPM for the hint, physical RPM for
    // request quality. Green starts at half of the early95-percent window.
    const float rpm=float(frame.readout.engine_rpm),up=session.drag_upshift_rpm();
    const float window=player_drag_shift_window(session.drag_category(),unsigned(std::max(0,car.gear)),session.drag_shift_scale());
    const float green=up-(.95F*.5F)*window,late=up+(1.F-.95F)*window;
    const int lamp=rpm>late?2:rpm>green?1:0;
    const char* lamp_state=lamp==2?"PulseRed":lamp==1?"PulseGreen":"PulseBlue";
    const bool lamp_selected=lamp!=0||rpm>=green-1500.F;
    const auto script=std::find_if(atlas.drag_animations.begin(),atlas.drag_animations.end(),
        [&](const DriftHudAnimation& a){return a.object=="Shift_light"&&a.state==lamp_state;});
    const unsigned duration=script==atlas.drag_animations.end()?0:script->duration;
    const auto lamp_tick=std::max<std::int64_t>(0,std::int64_t(now)-frame.drag_lamp_at)*3/10;
    //4F6CE0 returns false after the state duration;4F6B70 then resets time.
    // Blue/red are not looping scripts. Their completed state must restart,
    // even when the RPM visibility gate kept the earlier pulse hidden.
    if(frame.drag_lamp!=lamp||(lamp_selected&&!frame.drag_lamp_selected)||
       (lamp_selected&&duration&&lamp_tick>duration))frame.drag_lamp_at=now;
    frame.drag_lamp=lamp;frame.drag_lamp_selected=lamp_selected;
    // Original argument+23 is the HUD view mode, not lap-record status.
    // This adapter renders the authored chase-camera HUD, including countdown.
    const bool lamp_visible=rpm>up-.95F*window;
    for(const auto& authored:objects) {
        auto o=authored;const auto& name=o.name;
        if(name=="ShiftMessage"||name=="konopaoverheat(119)") {
            const bool shift=name=="ShiftMessage";
            const auto elapsed=std::int64_t(now)-(shift?frame.drag_shift_at:frame.drag_heat_at);
            const unsigned tick=unsigned(std::max<std::int64_t>(0,elapsed)*3/10);
            if((shift&&tick>600)||(!shift&&!hot))continue;
            const auto a=sample_hud_animation(atlas.drag_animations,name.c_str(),shift?"ZoomIn":"Blink",tick);
            o.color={a.color[0],a.color[1],a.color[2],a.alpha};
            if(shift&&o.width!=0&&o.height!=0) {
                o.transform[0]*=a.scale/o.width;o.transform[1]*=a.scale/o.width;
                o.transform[2]*=a.scale/o.height;o.transform[3]*=a.scale/o.height;
            }
            if(a.positioned) {
                const auto& p=o.parent_transform;
                o.transform[4]=p[4]+p[0]*a.position[0]+p[2]*a.position[1];
                o.transform[5]=p[5]+p[1]*a.position[0]+p[3]*a.position[1];
            }
        }
        if(name=="Shift_light") {
            if(!lamp_visible)continue;
            const auto a=sample_hud_animation(atlas.drag_animations,"Shift_light",lamp_state,
                unsigned(std::max<std::int64_t>(0,std::int64_t(now)-frame.drag_lamp_at)*3/10));
            o.color={a.color[0],a.color[1],a.color[2],a.alpha};
        }
        // Constructor4A3FCD binds component+20 to the NEEDLE. The separate
        // gear label at+6C uses authored tint / orange clutch-transition tint.
        if(name=="3rdPersonGear"&&car.transmission.mode!=3)o.color={255,128,0,255};
        if(name=="3rdPersonNeedle") {
            if(lamp==2)o.color={128,0,0,255};
            else if(lamp==1)o.color={0,255,0,255};
            else if(rpm>=green-1500.F) {
                const float blend=(rpm-(green-1500.F))*.0006666666595265269F;
                const float target[4]={0,128,192,255};
                for(unsigned c=0;c<4;++c)o.color[c]=static_cast<unsigned char>(std::clamp(
                    float(o.color[c])+(target[c]-o.color[c])*blend,0.F,255.F));
            }
        }
        if(name=="321_Go"&&!race_countdown_value(session.timing_config(),session.timing()))continue;
        if((name.find("Turbo")!=std::string::npos)&&!turbo)continue;
        const bool nitrous_bar=name.rfind("NOS_bar_6_",0)==0;
        if((nitrous_bar||name.rfind("nos_",0)==0)&&!nitrous.capacity)continue;
        if(nitrous_bar) {
            const unsigned slot=unsigned('6'-name.back());
            if(slot>full_nitrous_bars)continue;
            if(slot==full_nitrous_bars)o.color[3]=partial_nitrous_alpha;
        }
        //4A5B60: truncating heat*8 selects the authored segments.
        if(name.size()==6&&name.rfind("heat_",0)==0&&name[5]>='1'&&name[5]<='8'&&
           name[5]-'0'>int(session.car().engine.heat*8.F))continue;
        auto& m=o.transform;
        const auto rotate=[&](float angle) {
            const float c=std::cos(angle),s=std::sin(angle);
            const auto& p=o.parent_transform;
            const float x=o.position[0]+o.pivot[0]-c*o.pivot[0]+s*o.pivot[1];
            const float y=o.position[1]+o.pivot[1]-s*o.pivot[0]-c*o.pivot[1];
            m={p[0]*c+p[2]*s,p[1]*c+p[3]*s,p[2]*c-p[0]*s,p[3]*c-p[1]*s,
               p[4]+p[0]*x+p[2]*y,p[5]+p[1]*x+p[3]*y};
        };
        if(name=="3rdPersonNeedle") {
            const float first=rpm_angles[0],last=rpm_angles[1];
            const float arc=first<last?last-first:360.F-first+last;
            float angle=first+arc*std::clamp(float(frame.readout.engine_rpm/frame.readout.max_rpm),0.F,1.F);
            if(angle>360.F)angle-=360.F;
            if(angle<=-360.F)angle+=360.F;
            rotate(angle*.017453292519943295F);
        }
        if(name=="3rdperson_TurboDial")
            rotate(race_hud_turbo_rotation(session.turbo_pressure(),true)*.017453292519943295F);
        const auto draw=[&](const AtlasRect& texture,float x,float y,float w,float h) {
            const float xs[]={x,x+w,x+w,x},ys[]={y,y,y+h,y+h};std::array<std::array<float,2>,4> corners{};
            for(unsigned k=0;k<4;++k)corners[k]={ox+(m[4]+m[0]*xs[k]+m[2]*ys[k])*scale,oy+(m[5]+m[1]*xs[k]+m[3]*ys[k])*scale};
            add_quad(frame,texture,corners,{o.color[0],o.color[1],o.color[2]},o.color[3]);
        };
        if(o.type==1) {
            const auto& t=o.texture;const unsigned q=frame.quads;
            draw({t[0],t[1],t[2],t[3]},-o.width*.5F,-o.height*.5F,o.width,o.height);
            if(frame.quads>q)for(unsigned k=0;k<4;++k) {
                auto& v=frame.vertices[q*4+k];
                const float u=(k==0||k==3)?o.uv[0]:o.uv[2],vv=k<2?o.uv[1]:o.uv[3];
                v.u=(t[0]+.5F+u*(t[2]-1))/kRaceHudAtlasWidth;
                v.v=(t[1]+.5F+vv*(t[3]-1))/atlas.height;
            }
        } else if(o.type==2) {
            char text[64]{};
            if(name=="321_Go") {
                const auto count=race_countdown_value(session.timing_config(),session.timing());
                if(count==4)std::snprintf(text,sizeof text,"GO!");
                else std::snprintf(text,sizeof text,"%d",count);
            } else if(name=="ShiftMessage")std::snprintf(text,sizeof text,"%s",atlas.drag_shift_labels[frame.drag_quality<0?3:frame.drag_quality==0?2:frame.drag_quality<=.5F?0:1].c_str());
            else if(name=="konopaoverheat(119)")std::snprintf(text,sizeof text,"%s",o.text.c_str());
            else if(name=="RaceTime") {
                const auto& timing=world.timing();const auto& racer=timing.racers[player];
                const int ticks=racer.finish_valid?racer.finish_ticks:timing.go_valid?std::max(0,timing.now_ticks-timing.go_ticks):0;
                const unsigned cs=unsigned(ticks)/40;std::snprintf(text,sizeof text,"%u:%02u.%02u",cs/6000,cs/100%60,cs%100);
            } else if(name=="RacePositionNumber")std::snprintf(text,sizeof text,"%u",place);
            else if(name=="RacePositionNth")std::snprintf(text,sizeof text,"%s",place==1?"st":place==2?"nd":place==3?"rd":"th");
            else if(name=="NumberRacers")std::snprintf(text,sizeof text,"/%u",unsigned(world.rival_count()+1));
            else if(name=="3rdPersonSpeed")std::snprintf(text,sizeof text,"%d",frame.metric_speed?frame.readout.kph:frame.readout.mph);
            else if(name=="3rdPersonSpeedUnits")std::snprintf(text,sizeof text,"%s",frame.metric_speed?"KM/H":"MPH");
            else if(name=="3rdPersonGear") {
                if(frame.readout.gear<2)std::snprintf(text,sizeof text,"%s",frame.readout.gear==0?"R":"N");
                else std::snprintf(text,sizeof text,"%d",frame.readout.gear-1);
            } else if(name=="TurboPSI")std::snprintf(text,sizeof text,"Turbo");
            else if(name=="TurboPlus")std::snprintf(text,sizeof text,"+");
            else if(name=="TurboMinus")std::snprintf(text,sizeof text,"-");
            float advance=0;for(const char* c=text;*c;++c)advance+=atlas.lap_fonts[o.font][unsigned(*c)].advance;
            float x=(o.justification&2)?-advance:(o.justification&1)?-advance*.5F:0;
            const float y=(o.justification&4)?-atlas.lap_font_heights[o.font]*.5F:
                          (o.justification&8)?-atlas.lap_font_heights[o.font]:0.F;
            for(const char* c=text;*c;++c) {
                const auto& g=atlas.lap_fonts[o.font][unsigned(*c)];
                if(g.width&&g.height)draw({g.x,g.y,g.width+2,g.height+2},x+g.bearing_x,y+float(g.bearing_y),float(std::max(4,g.width)),float(g.height));
                x+=g.advance;
            }
        }
    }
}
