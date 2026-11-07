// Original frontend rating producers5801B0/5802C0/580400. These are UI
// ratings, not changes to vehicle dynamics. Stock purchase domain only.
float career_float(const unsigned char* p,unsigned offset) {
    float f;std::memcpy(&f,p+offset,4);return f;
}
struct CareerRatingBounds {
    std::array<float,5> low{{1000,1000,1000,1000,1000}},high{};
    void add(const unsigned char* p) {
        const auto f=[&](unsigned x){return career_float(p,x);};
        const auto keep=[&](unsigned i,float v){low[i]=std::min(low[i],v);high[i]=std::max(high[i],v);};
        //581D30, called by57C8EB for all35 records with CarType+C54==0.
        keep(0,((f(0x1a0)+f(0x194))+(f(0x180)+f(0x174)))*.5F*.5F);
        keep(1,((f(0x100)+f(0xfc))+(f(0xe0)+f(0xdc)))*.5F*.5F);
        keep(2,f(0x278)*.35F+f(0x274)*.65F);keep(3,f(0x110));
        low[4]=0; //581F0F resets this lower bound on every selected record.
        for(unsigned i=0;i<3;++i) {
            const unsigned suspension=0x730+0x50*i,tyre=0x5b0+0x40*i,brake=0x700+0x10*i;
            keep(0,((f(suspension+0x30)+f(suspension+0x24))+
                    (f(suspension+0x10)+f(suspension+4)))*.5F*.5F);
            keep(1,(((f(tyre+0x30)+f(tyre+0x2c))+f(0x834))+f(0x83c)+
                    (((f(tyre+0x10)+f(tyre+0xc))+f(0x830))+f(0x838)))*.5F*.5F);
            keep(2,(f(brake)+f(0x82c))*.65F+f(brake+4)*.35F);
            keep(3,(f(0x2b0+0x10*i)-f(0x820))*f(0x110));
            keep(4,(f(0x67c+0x10*i)+f(0x828))*f(0x670+0x10*i));
        }
    }
    float normalized(unsigned i,float x)const {return (x-low[i])/(high[i]-low[i]);}
};
bool career_stock_ratings(const unsigned char* record,const CareerRatingBounds& bounds,
                          std::array<float,3>& out,int nitrous=0) {
    VehicleHandling h;std::vector<VehicleHandlingIssue> issues;
    if(!decode_vehicle_handling(record,kVehicleRecordSize,h,issues))return false;
    std::array<float,64> curve{};if(!ai_setup_acceleration_curve(h,curve))return false;
    constexpr float maximum=98.34600067138672F,step=1.3410818576812744F;
    const auto sample=[&](float x) {
        const float index=x*(63.F/maximum);
        if(index>=63.F)return curve.back();
        const auto i=static_cast<unsigned>(index);const float f=index-float(i);
        return (1.F-f)*curve[i]+f*curve[i+1];
    };
    const auto drag=[&](float x){return ((x*h.drag)*x)/h.mass;};
    const auto f=[&](unsigned offset){return career_float(record,0x110+offset);};
    float speed=0;
    while(speed<=maximum && sample(speed)>drag(speed))speed+=step;
    const float grip=bounds.normalized(4,f(0x2ac)*f(0x2a0));
    out[1]=((speed*2.237F-128.F)/(207.F-128.F))*(1.F-.175F)+grip*.175F;
    float area=0;
    for(float x=0;x<=44.7027282714844F;x+=step) {
        const float engine=sample(x),resistance=drag(x);
        if(engine>resistance)area=(engine-resistance)*step+area;
    }
    //5802C0 uses installed ride+20 nitrous level as its remaining weighted term.
    out[0]=((area-136.F)/(562.F-136.F))*(1.F-(.08F+.2F))+grip*.2F+float(nitrous)*.3333333432674408F*.08F;
    const float tyre=bounds.normalized(1,((f(0x100)+f(0xfc))+(f(0xe0)+f(0xdc)))*.5F*.5F);
    const float brake=bounds.normalized(2,f(0x278)*.35F+f(0x274)*.65F);
    const float suspension=bounds.normalized(0,((f(0x1a0)+f(0x194))+(f(0x180)+f(0x174)))*.5F*.5F);
    const float weight=1.F-bounds.normalized(3,f(0x110));
    const float drive=f(0x1c0)>.6F?(1.F-f(0x1c0))*2.5F:(f(0x1c0)+.2F)*1.25F;
    out[2]=(((.255F*tyre+.225F*brake)+.265F*suspension)+.295F*weight)+.1F*drive;
    for(auto& x:out){if(!std::isfinite(x))return false;x=std::clamp(x,.03F,1.F);}
    return true;
}
