#include "nfsu/arg_path.h"
#include "nfsu/image.h"
#include "nfsu/race_events.h"
#include "nfsu/texture.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
enum class Artwork { loading, splash, tips };

bool selected(std::string_view name,Artwork kind) {
    switch(kind) {
    case Artwork::loading:return name=="PC_LOAD";
    case Artwork::splash:return name=="LS_SPLASH_PC";
    case Artwork::tips:return name.compare(0,10,"GAME_TIPS_")==0;
    }
    return false;
}

void extract(const std::filesystem::path& input,const std::filesystem::path& output,
             Artwork kind,std::set<unsigned>& exported) {
    std::string error;
    std::vector<unsigned char> bytes;
    std::vector<nfsu::RaceTableIssue> issues;
    if(!nfsu::load_race_image(input,bytes,issues))
        throw std::runtime_error("cannot read artwork source "+input.filename().string());
    const auto textures=nfsu::parse_texture_pack_set(bytes.data(),bytes.size());
    if(!textures.ok())throw std::runtime_error("invalid artwork texture pack");
    for(const auto& pack:textures.packs)for(const auto& rec:pack.textures) {
        if(!selected(rec.name,kind)||!exported.insert(rec.hash).second)continue;
        std::vector<unsigned char> compressed,pixels;
        std::uint64_t level=0;
        std::size_t decoded=0;
        nfsu::P8Palette palette;
        if(!rec.valid||!nfsu::texture_payload(bytes.data(),bytes.size(),rec,compressed,decoded,error,&palette)||
           !nfsu::level_size(rec.format,rec.width,rec.height,level)||level>compressed.size()||
           !nfsu::decode_level(rec.format,rec.width,rec.height,compressed.data(),level,pixels,error,&palette))
            throw std::runtime_error("cannot decode "+rec.name+" "+error);
        std::ostringstream filename;
        filename<<std::hex<<std::setfill('0')<<std::setw(8)<<rec.hash<<".tga";
        const auto path=output/filename.str();
        if(std::filesystem::exists(path))throw std::runtime_error("artwork source output already exists");
        std::ofstream stream(path,std::ios::binary);
        if(!nfsu::write_tga(stream,rec.width,rec.height,pixels,error))
            throw std::runtime_error("cannot export "+rec.name+" "+error);
        stream.close();
        if(!stream)throw std::runtime_error("cannot close "+rec.name+" export");
        std::cout<<filename.str()<<' '<<rec.name<<' '<<rec.width<<'x'<<rec.height<<'\n';
    }
}
}

int main(int argc,char** argv) {
    if(argc!=3) {
        std::cerr<<"usage: nfsu_frontend_art_sources <retail-root> <empty-output-dir>\n";
        return 1;
    }
    try {
        const auto root=nfsu::argument_path(argv[1]);
        const auto output=nfsu::argument_path(argv[2]);
        if(!std::filesystem::is_empty(output))throw std::runtime_error("output directory must be empty");
        std::set<unsigned> exported;
        extract(root/"Global/GLOBALB.BUN",output,Artwork::loading,exported);
        if(exported.size()!=1)throw std::runtime_error("PC loading texture absent");
        extract(root/"FrontEnd/FrontB.lzc",output,Artwork::splash,exported);
        if(exported.size()!=2)throw std::runtime_error("PC splash texture absent");
        extract(root/"Global/DYNTEX.BIN",output,Artwork::tips,exported);
        if(exported.size()<=2)throw std::runtime_error("PC loading tips absent");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
