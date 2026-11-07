#include "nfsu/deko_draw_bindings.h"
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
auto pipeline_key(const nfsu::RecordedDraw& d) {
    const auto& p=d.pipeline;
    return std::make_tuple(p.alpha_test,p.alpha_ref,p.blend,p.blend_src,p.blend_dst,
        p.blend_op,p.depth_test,p.depth_write,p.color_write,p.cull);
}
auto shader_key(const nfsu::RecordedDraw& d) {
    return std::make_tuple(d.program,d.texture_resource!=nfsu::kNoHandle,
        d.pipeline.alpha_test!=nfsu::shader_manifest::CompareOp::always);
}
}
int main() {try {
    using namespace nfsu;
    std::vector<RecordedDraw> sequence;
    for(const auto& program:shader_manifest::kPrograms)
        for(std::size_t i=0;i<program.variant_count;++i) {
            RecordedDraw d;d.program=&program;d.pipeline=program.variants[i];
            d.texture_resource=1;d.vertex_resource=2;d.index_resource=3;
            sequence.push_back(d);
            d.texture_resource=4;sequence.push_back(d);
            d.vertex_resource=5;sequence.push_back(d);
            d.index_resource=6;sequence.push_back(d);
            d.texture_resource=kNoHandle;sequence.push_back(d);
            d.texture_resource=4;sequence.push_back(d);
            // Exercise each independent pipeline input, including changes
            // within a program rather than only manifest variant boundaries.
            d.pipeline.alpha_ref^=1;sequence.push_back(d);
            d.pipeline.depth_write=!d.pipeline.depth_write;sequence.push_back(d);
            d.pipeline.blend=!d.pipeline.blend;sequence.push_back(d);
            d.pipeline.alpha_test=shader_manifest::CompareOp::never;sequence.push_back(d);
            d.pipeline.alpha_test=shader_manifest::CompareOp::always;sequence.push_back(d);
            d.pipeline.depth_test=shader_manifest::CompareOp::greater;sequence.push_back(d);
            d.pipeline.blend_src=shader_manifest::BlendFactor::dst_alpha;sequence.push_back(d);
            d.pipeline.blend_dst=shader_manifest::BlendFactor::dst_color;sequence.push_back(d);
            d.pipeline.blend_op=shader_manifest::BlendOp::subtract;sequence.push_back(d);
            d.pipeline.color_write=shader_manifest::ColorWrite::rgb;sequence.push_back(d);
            d.pipeline.cull=shader_manifest::Cull::front;sequence.push_back(d);
        }
    RecordedDraw bound,shader,layout;
    const RecordedDraw* previous=nullptr;
    unsigned transitions=0;
    for(unsigned round=0;round<2;++round)for(const auto& d:sequence) {
        // Pass/list boundaries must restore state even for identical draws.
        if(transitions%7==0)previous=nullptr;
        const auto b=deko_draw_bindings(d,previous);
        if(!previous)check(b.shaders&&b.pipeline&&b.layout&&b.texture&&b.vertex&&b.index,"pass reset incomplete");
        if(b.shaders)shader=d;
        if(b.pipeline)bound.pipeline=d.pipeline;
        if(b.layout)layout.program=d.program;
        if(b.texture&&d.texture_resource!=kNoHandle)bound.texture_resource=d.texture_resource;
        if(b.vertex)bound.vertex_resource=d.vertex_resource;
        if(b.index)bound.index_resource=d.index_resource;
        check(shader_key(shader)==shader_key(d),"stale shader selection");
        check(pipeline_key(bound)==pipeline_key(d),"stale pipeline state");
        check(layout.program==d.program,"stale vertex layout");
        check(d.texture_resource==kNoHandle||bound.texture_resource==d.texture_resource,"stale texture");
        check(bound.vertex_resource==d.vertex_resource&&bound.index_resource==d.index_resource,"stale geometry buffers");
        previous=&d;++transitions;
    }
    auto a=sequence.front(),b=a;b.texture_resource=42;
    const auto texture_only=deko_draw_bindings(b,&a);
    check(texture_only.texture&&!texture_only.pipeline&&!texture_only.shaders&&!texture_only.layout,
          "texture-only transition repeats fixed state");
    b=a;b.first_index+=3;b.first_vertex+=1;
    const auto range_only=deko_draw_bindings(b,&a);
    check(!range_only.vertex&&!range_only.index,"draw range unnecessarily rebinds buffers");
    std::cout<<"PASS binding-state reconstruction "<<transitions<<" transitions; pass resets and texture/buffer boundaries\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}

