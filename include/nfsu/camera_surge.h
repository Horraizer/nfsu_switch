#pragma once

#include <array>
#include <algorithm>

namespace nfsu {

//489855..489E20: ordinary type-1 mover longitudinal response. Inputs are
//the published actor's local +x acceleration, signed forward speed and
//owner-heading minus velocity-heading slip, with scaled73457C seconds.
//This is a distance response, not an invented NOS field-of-view pulse.
class CameraSurge {
public:
    void reset() noexcept { *this=CameraSurge{}; }
    float update(float acceleration,float speed,float slip,float dt) noexcept {
        if(!(dt>=0.F&&dt<=.1F))return offset_;
        float sample=.045F*acceleration;
        int mode=-1;
        if(sample>=-.3F) {
            sample=.6F*acceleration;
            if(sample>.7F)mode=1;
            else {mode=0;sample=0.F;}
        }
        const float clock=dt*60.F+fraction_;
        const int count=int(clock);
        fraction_=clock-float(count);
        for(int tick=0;tick<count;++tick) {
            for(unsigned i=19;i>0;--i)history_[i]=history_[i-1];
            history_[0]=sample;
            bool trigger=false;
            float sum=sample;
            if((mode<0&&current_>=0.F)||(mode>0&&current_<=0.F)) {
                trigger=true;
                for(unsigned i=1;i<10;++i) {
                    sum+=history_[i];
                    if(mode<0?history_[i]>-.3F:history_[i]<.7F)trigger=false;
                }
                if(trigger)for(unsigned i=10;i<20;++i)
                    if(mode<0?history_[i]<-.3F:history_[i]>.7F)trigger=false;
            } else if(mode==0) {
                for(unsigned i=1;i<10;++i)
                    if(history_[i]<.7F&&history_[i]>-.3F)target_=0.F;
            }
            if(trigger) {
                target_=sum*.1F;
                float angle=speed>=1.F?slip:0.F;
                if(angle<0.F)angle=-angle;
                const float attenuation=std::max(1.F-angle*6.F,0.F);
                target_=attenuation*target_;
                target_=std::max(-1.F,std::min(target_,6.F));
                age_=0.F;
            }
        }
        const float step=dt*60.F;
        if(target_==0.F) {
            const float rate=.012F*step;
            if(current_>3.F)current_-=rate+rate;
            else if(current_>0.F)current_=current_>rate?current_-rate:0.F;
            else if(current_<0.F)current_=current_<-rate?current_+rate:0.F;
        } else if(target_>0.F) {
            const float difference=current_-target_;
            if(difference<=0.F) {
                float rate=.03F*step;
                if(difference>-.4F)rate*=.3F;
                else if(difference>-1.F)rate*=.6F;
                current_+=rate;
            } else expire(mode,dt);
        } else if(speed*2.237F<5.F)target_=0.F;
        else if(current_<target_)expire(mode,dt);
        else current_+=-.03F*step;
        offset_=(std::max(500.F-speed*2.237F,0.F)/500.F)*current_;
        return offset_;
    }
    float offset()const noexcept{return offset_;}
    // State access is used by original-executable differential checks.
    std::array<float,5> state()const noexcept{return {{current_,target_,age_,fraction_,offset_}};}
private:
    void expire(int mode,float dt) noexcept {
        age_+=dt;
        if(age_>2.F||mode==0)target_=0.F;
    }
    std::array<float,20> history_{};
    float current_=0.F,target_=0.F,age_=0.F,fraction_=0.F,offset_=0.F;
};

} // namespace nfsu
