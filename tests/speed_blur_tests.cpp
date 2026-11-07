#include "nfsu/speed_blur.h"

#include "nfsu/camera_motion.h"

#include "nfsu/camera_surge.h"

#include "camera_surge_native.inc"

#include "nfsu/crash_camera.h"

#include <cstring>

#include "camera_presentation_native.inc"

#include "nfsu/render.h"

#include "../tools/speed_blur_preview.h"
#include "speed_blur_d3d9_native.inc"

#include <iostream>

#include <iomanip>

#include <string>



int main(int argc,char**) {

    nfsu::SpeedBlur blur;

    if(argc>1) {

        std::cout<<std::setprecision(10);

        std::string op;

        while(std::cin>>op) {

            if(op=="reset") {blur.reset();std::cout<<"reset\n";continue;}

            int nos,advance;float x,y,z;

            if(op!="frame" || !(std::cin>>nos>>x>>y>>z>>advance))return 2;

            const auto frame=blur.frame(nos!=0,x,y,z,advance!=0);

            std::cout<<blur.envelope()<<' '<<unsigned(frame.alpha);

            for(auto v:frame.offsets)std::cout<<' '<<v;

            std::cout<<'\n';

        }

        return 0;

    }

    int failed=0;

    const auto check=[&](bool ok,const char* what) {

        if(!ok){++failed;std::cerr<<"test failure: "<<what<<'\n';}

    };

    const auto real=[](unsigned bits){float f;std::memcpy(&f,&bits,4);return f;};

    const auto vector=[&](const std::array<unsigned,3>& bits){return std::array<float,3>{{real(bits[0]),real(bits[1]),real(bits[2])}};};

    check(nfsu::kCrashCameraDefaultIntensity==kNativeCrashDefault,"original fresh-profile crash intensity publication");

    for(const auto& row:kNativeCameraMotion) {

        const auto result=nfsu::camera_motion_velocity(vector(row.eye),vector(row.previous),real(row.dt),row.cut,vector(row.old));

        check(result==vector(row.velocity),"original47D1DB camera-eye derivative/cut/zero-dt behavior");

    }

    for(const auto& row:kNativeCameraVelocity) {

        const auto result=nfsu::camera_motion_to_view(vector(row.velocity),vector(row.right),vector(row.up),vector(row.forward));

        check(result==vector(row.result),"original scalar view transform and downward screen Y");

    }

    nfsu::CameraSurge surge;

    for(std::size_t i=0;i<std::size(kNativeCameraSurge);++i) {

        const auto& row=kNativeCameraSurge[i];

        if(row.reset)surge.reset();

        surge.update(real(row.input[0]),real(row.input[1]),real(row.input[2]),real(row.input[3]));

        const auto state=surge.state();

        for(unsigned k=0;k<state.size();++k)if(state[k]!=real(row.state[k])) {

            if(failed<3)std::cerr<<"native camera surge row="<<i<<" component="<<k

                <<" actual="<<std::setprecision(10)<<state[k]<<" expected="<<real(row.state[k])<<'\n';

            check(false,"original consecutive camera surge history/response");break;

        }

    }

    nfsu::CameraMotion motion;motion.update({{2000,1000,5}},1.F/60.F);

    check(motion.velocity()==std::array<float,3>{},"first published view has no artificial motion");

    motion.update({{2001,1000,5}},1.F/60.F);

    check(motion.velocity()[0]>59.9F&&motion.velocity()[0]<60.1F,"eye movement drives effect independently of car velocity");

    motion.update({{3000,1000,5}},1.F/60.F,true);

    check(motion.velocity()==std::array<float,3>{},"view cut resets derivative instead of producing a camera-teleport blur");

    check(!blur.frame(false,0,0,25).active(),"ordinary low speed skips passes");

    auto a=blur.frame(true,0,0,50);

    check(a.alpha==179 && std::abs(a.offsets[0]-0.003877500305F)<1e-9F,

          "original first NOS frame at 50 m/s");

    for(int i=0;i<4;++i)a=blur.frame(true,0,0,50);

    check(a.alpha==248 && std::abs(a.offsets[0]-0.007312499918F)<1e-9F,

          "original full NOS frame");

    const auto frozen=blur.frame(false,0,0,50,false);

    check(frozen.offsets==a.offsets && frozen.alpha==a.alpha,"pause freezes envelope");

    for(int i=0;i<10;++i)a=blur.frame(false,0,0,20);

    check(!a.active(),"resource release decays without held input");

    blur.reset();check(!blur.frame(false,0,0,0).active(),"race reset clears history");

    const auto forward=blur.frame(true,0,0,50,false);

    const auto reverse=blur.frame(true,0,0,-50,false);

    check(forward.offsets[0]==-reverse.offsets[0],"rear view reverses zoom direction");

    const auto side=blur.frame(true,50,0,0,false);

    check(side.offsets[0]==side.offsets[2] && side.offsets[1]==0,

          "sideways speed is directional rather than radial");

    std::vector<unsigned char> image(64*32*4,37);

    const auto uniform=image;

    speed_blur_preview(image,64,32,forward);

    check(image==uniform,"linear clamp passes preserve constant scene colors");

    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)

        for(unsigned c=0;c<3;++c)image[(y*64+x)*4+c]=(x%8<2)?255:0;

    {
        std::vector<unsigned char> ramp(8*8*4,255);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            for(unsigned c=0;c<3;++c)ramp[(y*8+x)*4+c]=static_cast<unsigned char>(16*x+8*y);
        nfsu::SpeedBlurFrame copy;copy.alpha=255;
        speed_blur_preview(ramp,8,8,copy);
        check(ramp[0]==0&&ramp[1]==0&&ramp[2]==0,
            "native D3D9 clip-corner geometry keeps the first sample at UV zero across all passes");
    }
    const auto sharp=image;speed_blur_preview(image,64,32,forward);

    check(image!=sharp,"radial passes affect world pattern");

    blur.reset();

    for(float speed:{0.F,25.F,50.F,125.F})

        check(!blur.nitrous_frame(false,0,0,speed).active(),"NOS-only mode is sharp at every speed");

    check(blur.nitrous_frame(true,0,0,50).active(),"actual resource activation starts effect");

    check(!blur.nitrous_frame(false,0,0,125).active() && blur.envelope()==0,

          "release or empty resource immediately clears blur and envelope at speed");

    check(blur.nitrous_frame(true,0,0,50).alpha==179,"next NOS activation starts a fresh envelope");

    nfsu::Frame scene;std::string error;const unsigned char background[]={0,0,0,255};

    check(scene.reset(64,32,background,{},error),"scene allocation");

    const auto depth=scene.depth();const auto ids=scene.ids();const auto blended=scene.blended();

    check(scene.replace_colors(image) && scene.rgba()==image && scene.depth()==depth &&

          scene.ids()==ids && scene.blended()==blended,"postprocess preserves all depth/ownership for later cars");

    check(!scene.replace_colors({}) && scene.rgba()==image,"bad postprocess dimensions leave scene intact");

    {
        constexpr unsigned width=256,height=144;
        std::vector<unsigned char> input(width*height*4);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            auto* pixel=&input[(y*width+x)*4];
            pixel[0]=static_cast<unsigned char>(x);
            pixel[1]=static_cast<unsigned char>(y*255/(height-1));
            pixel[2]=(x/8+y/8)%2?255:0;
            pixel[3]=static_cast<unsigned char>(x^y);
        }
        const nfsu::SpeedBlurFrame effect{{.0073125F,.0073125F,-.0073125F,-.0073125F},248};
        const auto reference=nfsu::diagnostic::speed_blur_reference(input,width,height,effect);
        for(const auto& row:kOriginalFilterPixels)for(unsigned c=0;c<4;++c) {
            const auto actual=reference.image[row[0]][(row[2]*reference.width[row[0]]+row[1])*4+c];
            check(std::abs(int(actual)-int(row[3+c]))<=3,
                "original Direct3D9 filter GPU pixels, four RGBA passes and constant-factor alpha");
        }
    }
    std::cout<<"speed blur tests: "<<(failed?"FAIL":"PASS")<<'\n';

    return failed?1:0;

}

