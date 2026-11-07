// Career save-slot filesystem adapter; domain rules remain in career.cpp.
#include "nfsu/career.h"
#include "nfsu/career_profiles.h"
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <chrono>
#include <ctime>
#include "nfsu/sha256.h"
#include <fstream>
#include <limits>
#include <sstream>
namespace nfsu { namespace {
bool refuse(std::string& error,const char* why){error=why;return false;}
std::string digest(const std::string& s) {
    return sha256_hex(sha256(reinterpret_cast<const unsigned char*>(s.data()),s.size()));
}
std::filesystem::path slot(const std::filesystem::path& base,unsigned i) {
    auto p=base;p+=i?".1":".0";return p;
}
struct Saved {bool exists=false,valid=false;std::uint64_t generation=0;unsigned version=0;CareerProfile profile;};
Saved read_slot(const std::filesystem::path& path,const CareerCatalog& catalog,std::string* legacy_bytes=nullptr) {
    Saved out;out.exists=std::filesystem::exists(path);if(!out.exists)return out;
    if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>131072)return out;
    std::ifstream file(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});
    if(!file.eof()&&file.fail())return out;
    for(unsigned char c:bytes)if(c!='\n'&&(c<32||c>126))return out;
    const auto at=bytes.rfind("SHA256 ");if(at==std::string::npos)return out;
    const auto body=bytes.substr(0,at);if(bytes.substr(at)!="SHA256 "+digest(body)+"\n")return out;
    std::istringstream in(body);std::string magic,car,states,extra;unsigned version=0,page=0,seen=0;
    auto& p=out.profile;
    if(!(in>>magic>>version>>out.generation>>page>>p.difficulty>>p.bank>>p.selected_event>>seen>>car>>states) ||
       magic!="NFSU_CAREER"||(version<1||version>11)||!out.generation||page>10||seen>1||states.size()!=256)return out;
    out.version=version;
    if(version>=2 && !(in>>p.style_points))return out;
    p.style_high_water=p.style_points;
    if(version>=3) {
        if(!(in>>p.style_high_water))return out;
        for(auto& level:p.ride.performance)if(!(in>>level))return out;
        for(auto& hash:p.ride.installed)if(!(in>>hash))return out;
        for(auto& category:p.ride.owned)for(auto& key:category)if(!(in>>key))return out;
    }
    if(version>=4) {
        if(!(in>>p.tournament.event>>p.tournament.round))return out;
        for(unsigned r=0;r<(version>=8?p.tournament.places.size():3U);++r)
            for(auto& place:p.tournament.places[r])if(!(in>>place))return out;
    }
    if(version>=5) {
        if(!(in>>p.pending_unique))return out;
        for(auto& flag:p.ride.unique){unsigned value=0;if(!(in>>value)||value>1)return out;flag=value!=0;}
    }
    if(version>=6 && !(in>>p.movie_event>>p.movie_phase))return out;
    if(version>=7){
        unsigned count=0;if(!(in>>p.magazine_random>>p.magazine_pending>>count)||count>27)return out;
        for(unsigned i=0;i<count;++i){
            CareerMagazine cover;unsigned seen=0;
            if(!(in>>cover.index>>cover.seed>>seen>>cover.car)||seen>1)return out;
            cover.seen=seen!=0;
            for(auto& hash:cover.installed)if(!(in>>hash))return out;
            for(auto& level:cover.performance)if(!(in>>level))return out;
            for(auto& flag:cover.unique){unsigned value=0;if(!(in>>value)||value>1)return out;flag=value!=0;}
            p.magazine_covers.push_back(std::move(cover));
        }
    }
    if(version>=9&&!(in>>p.races_won>>p.races_lost>>p.restarts))return out;
    if(version>=10) {
        unsigned partial=0;auto& s=p.statistics;std::string tag;
        if(!(in>>tag>>partial>>s.starts>>s.wins>>s.losses>>s.totalled>>s.metres)||tag!="STATS"||partial>1)return out;
        s.partial_history=partial!=0;
        for(auto& mode:s.modes)if(!(in>>mode.starts>>mode.wins>>mode.style))return out;
    }else p.statistics.partial_history=true;
    if(version>=11&&!(in>>p.new_parts_event))return out;
    if(in>>extra)return out;
    p.page=static_cast<CareerPage>(page);p.car=car=="-"?"":car;p.customization_seen=seen!=0;
    for(unsigned i=0;i<256;++i){if(states[i]<'0'||states[i]>'5')return out;p.status[i]=std::uint8_t(states[i]-'0');}
    std::string error;out.valid=catalog.valid(p,error);
    if(out.valid&&version<11&&legacy_bytes)*legacy_bytes=std::move(bytes);
    return out;
}
bool retain_migration_backup(const std::filesystem::path& backup,const CareerCatalog& catalog,const std::string& bytes) {
    const auto existing=read_slot(backup,catalog);
    if(existing.valid)return true;
    if(existing.exists&&!std::filesystem::is_regular_file(backup))return false;
    auto pending=backup;pending+=".pending";
    // The Switch SD filesystem does not support filesystem::copy_file.
    std::ofstream file(pending,std::ios::binary|std::ios::trunc);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    file.close();
    if(!file)return false;
    std::string written;
    if(!read_slot(pending,catalog,&written).valid||written!=bytes)return false;
    // Only a failed backup may be replaced, after its replacement verifies.
    if(existing.exists&&!std::filesystem::remove(backup))return false;
    std::filesystem::rename(pending,backup);
    return true;
}
}
bool load_career(const std::filesystem::path& base,const CareerCatalog& catalog,CareerProfile& p,std::string& error) {
    if(catalog.free_drive())return refuse(error,"free driving cannot read career saves");
    try {
        const auto a=read_slot(slot(base,0),catalog),b=read_slot(slot(base,1),catalog);
        if(!a.exists&&!b.exists){p=CareerProfile{};return catalog.valid(p,error);}
        if(!a.valid&&!b.valid)return refuse(error,"career saves damaged; existing files preserved");
        p=(!b.valid||(a.valid&&a.generation>=b.generation))?a.profile:b.profile;
        if((p.page==CareerPage::menu || p.page==CareerPage::map || p.page==CareerPage::briefing) &&
           catalog.select_available_event(p) && p.page==CareerPage::briefing)p.page=CareerPage::map;
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool save_career(const std::filesystem::path& base,const CareerCatalog& catalog,const CareerProfile& p,std::string& error) {
    if(catalog.free_drive())return refuse(error,"free driving cannot write career saves");
    try {
        if(!catalog.valid(p,error))return false;
        std::string legacy_bytes[2];
        const auto a=read_slot(slot(base,0),catalog,&legacy_bytes[0]),b=read_slot(slot(base,1),catalog,&legacy_bytes[1]);
        if((a.exists||b.exists)&&!a.valid&&!b.valid)return refuse(error,"refusing to overwrite damaged career");
        const auto previous=std::max(a.valid?a.generation:0,b.valid?b.generation:0);
        if(previous==std::numeric_limits<std::uint64_t>::max())return refuse(error,"career save sequence exhausted");
        const unsigned target=!a.valid?0:!b.valid?1:a.generation<=b.generation?0:1;
        std::ostringstream body;body<<"NFSU_CAREER 11\n"<<previous+1<<' '<<unsigned(p.page)<<' '<<p.difficulty<<' '<<p.bank<<' '
            <<p.selected_event<<' '<<unsigned(p.customization_seen)<<' '<<(p.car.empty()?"-":p.car)<<'\n';
        for(auto s:p.status)body<<unsigned(s);
        body<<'\n'<<p.style_points<<' '<<p.style_high_water<<'\n';
        for(auto level:p.ride.performance)body<<level<<' ';
        body<<'\n';
        for(auto hash:p.ride.installed)body<<hash<<' ';
        body<<'\n';
        for(const auto& category:p.ride.owned){for(auto key:category)body<<key<<' ';body<<'\n';}
        body<<p.tournament.event<<' '<<p.tournament.round<<'\n';
        for(const auto& round:p.tournament.places){for(auto place:round)body<<place<<' ';body<<'\n';}
        body<<p.pending_unique<<'\n';for(auto flag:p.ride.unique)body<<unsigned(flag)<<' ';body<<'\n';
        body<<p.movie_event<<' '<<p.movie_phase<<'\n';
        body<<p.magazine_random<<' '<<p.magazine_pending<<' '<<p.magazine_covers.size()<<'\n';
        for(const auto& cover:p.magazine_covers){
            body<<cover.index<<' '<<cover.seed<<' '<<unsigned(cover.seen)<<' '<<cover.car<<'\n';
            for(auto hash:cover.installed)body<<hash<<' ';
            body<<'\n';
            for(auto level:cover.performance)body<<level<<' ';
            body<<'\n';
            for(auto flag:cover.unique)body<<unsigned(flag)<<' ';
            body<<'\n';
        }
        body<<p.races_won<<' '<<p.races_lost<<' '<<p.restarts<<'\n';
        const auto& s=p.statistics;
        body<<"STATS "<<unsigned(s.partial_history)<<' '<<s.starts<<' '<<s.wins<<' '<<s.losses<<' '<<s.totalled<<' '
            <<std::setprecision(17)<<s.metres<<'\n';
        for(const auto& mode:s.modes)body<<mode.starts<<' '<<mode.wins<<' '<<mode.style<<'\n';
        body<<p.new_parts_event<<'\n';
        const auto text=body.str();
        // Retain exact pre-upgrade slots for older NRO rollback.
        const Saved* previous_slots[]={&a,&b};
        for(unsigned version:{9U,10U,11U})for(unsigned i=0;i<2;++i)if(previous_slots[i]->valid&&previous_slots[i]->version<version) {
            auto backup=base;backup+=".before-v"+std::to_string(version)+"."+std::to_string(i);
            if(!retain_migration_backup(backup,catalog,legacy_bytes[i]))
                return refuse(error,"migration backup verification failed; save retained");
        }
        if(!base.parent_path().empty())std::filesystem::create_directories(base.parent_path());
        std::ofstream file(slot(base,target),std::ios::binary|std::ios::trunc);file<<text<<"SHA256 "<<digest(text)<<'\n';file.flush();
        if(!file)return refuse(error,"career save write failed; previous slot retained");
        file.close();if(!file || !read_slot(slot(base,target),catalog).valid)return refuse(error,"career save verification failed");
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}

namespace nfsu { namespace {
std::string profile_key(unsigned id) {
    char text[16];std::snprintf(text,sizeof text,"p%08u",id);return text;
}
std::string profile_saved_at(const std::filesystem::path& base) {
    std::error_code error;std::filesystem::file_time_type latest{};bool found=false;
    for(unsigned i=0;i<2;++i) {
        const auto stamp=std::filesystem::last_write_time(slot(base,i),error);
        if(!error&&(!found||stamp>latest)){latest=stamp;found=true;}
    }
    if(!found)return {};
    const auto system=std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        latest-std::filesystem::file_time_type::clock::now()+std::chrono::system_clock::now());
    const auto time=std::chrono::system_clock::to_time_t(system);const auto* calendar=std::localtime(&time);
    char text[32]{};if(calendar)std::strftime(text,sizeof text,"%Y-%m-%d %H:%M",calendar);return text;
}
std::string profile_fold(std::string text) {
    for(auto& c:text)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    return text;
}
std::string profile_text(const std::filesystem::path& path) {
    if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>1024)return {};
    std::ifstream in(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});
    for(unsigned char c:bytes)if(c!='\n'&&(c<32||c>126))return {};
    const auto at=bytes.rfind("SHA256 ");
    if(at==std::string::npos||bytes.substr(at)!="SHA256 "+digest(bytes.substr(0,at))+"\n")return {};
    return bytes.substr(0,at);
}
bool profile_write(const std::filesystem::path& path,const std::string& text,std::string& error) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<text<<"SHA256 "<<digest(text)<<'\n';out.flush();
    if(!out)return refuse(error,"profile write failed; previous selection retained");
    out.close();
    if(!out||profile_text(path)!=text)return refuse(error,"profile verification failed");
    return true;
}
struct ActiveProfile {std::uint64_t generation=0;unsigned id=0;};
ActiveProfile active_profile(const std::filesystem::path& path) {
    ActiveProfile out;std::istringstream in(profile_text(path));std::string tag,extra;
    if(!(in>>tag>>out.generation>>out.id)||tag!="NFSU_ACTIVE_PROFILE"||!out.generation||in>>extra)return {};
    return out;
}
}
bool CareerProfiles::valid_name(const std::string& name)noexcept {
    // Original417700 ->41F160 edits at most12 characters. Our FNTF is ASCII.
    if(name.empty()||name.size()>12||name.front()==' '||name.back()==' ')return false;
    for(unsigned char c:name)if(c<32||c>126)return false;
    return true;
}
bool CareerProfiles::list(std::vector<CareerProfileEntry>& entries,unsigned& selected,std::string& error)const {
    try {
        entries.clear();selected=0;
        const auto legacy=root_/"career";
        if(std::filesystem::exists(slot(legacy,0))||std::filesystem::exists(slot(legacy,1)))entries.push_back({0,"PLAYER",legacy});
        const auto folder=root_/"profiles";
        if(std::filesystem::is_directory(folder))for(const auto& dir:std::filesystem::directory_iterator(folder)) {
            if(!dir.is_directory())continue;
            const auto key=dir.path().filename().string();
            if(key.size()!=9||key[0]!='p'||!std::all_of(key.begin()+1,key.end(),[](char c){return c>='0'&&c<='9';}))continue;
            const unsigned id=unsigned(std::stoul(key.substr(1)));if(!id)continue;
            auto name=profile_text(dir.path()/"name");if(name.empty()||name.back()!='\n')continue;name.pop_back();
            const auto base=dir.path()/"career";
            if(!valid_name(name)||(!std::filesystem::exists(slot(base,0))&&!std::filesystem::exists(slot(base,1))))continue;
            entries.push_back({id,name,base});
        }
        for(auto& entry:entries)entry.saved_at=profile_saved_at(entry.base);
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        const auto a=active_profile(root_/"active-profile.0"),b=active_profile(root_/"active-profile.1");
        const auto active=a.generation>=b.generation?a:b;
        const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==active.id;});
        if(it!=entries.end())selected=unsigned(it-entries.begin());
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool CareerProfiles::select(unsigned id,std::string& error) {
    try {
        std::vector<CareerProfileEntry> entries;unsigned selected=0;if(!list(entries,selected,error))return false;
        if(std::none_of(entries.begin(),entries.end(),[&](const auto& e){return e.id==id;}))return refuse(error,"profile not found");
        const auto a=active_profile(root_/"active-profile.0"),b=active_profile(root_/"active-profile.1");
        const auto generation=std::max(a.generation,b.generation);
        if(generation==UINT64_MAX)return refuse(error,"profile selection sequence exhausted");
        return profile_write(root_/(a.generation<=b.generation?"active-profile.0":"active-profile.1"),
            "NFSU_ACTIVE_PROFILE "+std::to_string(generation+1)+" "+std::to_string(id)+"\n",error);
    }catch(const std::exception& e){error=e.what();return false;}
}
bool CareerProfiles::create(const std::string& name,const CareerCatalog& catalog,CareerProfileEntry& entry,std::string& error) {
    try {
        if(!valid_name(name))return refuse(error,"Use1-12 printable letters, numbers or symbols, without outer spaces.");
        std::vector<CareerProfileEntry> entries;unsigned selected=0;if(!list(entries,selected,error))return false;
        for(const auto& e:entries)if(profile_fold(e.name)==profile_fold(name))return refuse(error,"A profile with this name already exists.");
        const auto folder=root_/"profiles";std::filesystem::create_directories(folder);
        unsigned id=1;for(;id<=99999999;++id)if(std::filesystem::create_directory(folder/profile_key(id)))break;
        if(id>99999999)return refuse(error,"No free profile identifiers.");
        const auto dir=folder/profile_key(id);CareerProfile fresh;
        if(!save_career(dir/"career",catalog,fresh,error)||!profile_write(dir/"name",name+"\n",error))return false;
        entry={id,name,dir/"career"};return select(id,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
}
