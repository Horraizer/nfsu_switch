void check_timeline_grid() {
    static_assert(sizeof(LanSnapshotBuffer)<4096,"timeline history must not occupy the Switch stack");
    struct Cadence {std::array<double,6> seconds;unsigned count;};
    const std::array<Cadence,9> cadences{{
        {{{1./60,0,0,0,0,0}},1},{{{1./30,0,0,0,0,0}},1},
        {{{1./60,2./60,3./60,0,0,0}},3},{{{.017,.018,.033,.025,.017,0}},5},
        {{{.019,.031,.016,0,0,0}},3},{{{.0167,.0168,.0334,.0169,.0501,.0170}},6},
        {{{.010,.018,.033,.025,.017,0}},5},
        {{{1./60,3./60,2./60,1./60,4./60,2./60}},6},
        {{{1./60,1./60,1./30,1./60,1./12,1./60}},6}
    }};
    for(const unsigned batch:{1U,3U,4U,6U,7U,8U,9U,10U})
    for(unsigned cadence=0;cadence<cadences.size();++cadence)
    for(const unsigned loss:{0U,7U,19U,23U})
    for(const unsigned release_phase:{0U,1U,batch/2}){
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        double elapsed=0,previous=0,maximum_error=0;unsigned sent=0,held=0,samples=0;
        bool paired=true;
        for(unsigned display=0;display<720;++display){
            const double seconds=cadences[cadence].seconds[display%cadences[cadence].count];
            elapsed+=seconds;const auto host=unsigned(std::floor(elapsed*60+1e-8));
            const unsigned available=host>=release_phase?release_phase+(host-release_phase)/batch*batch:0;
            for(unsigned step=sent+1;step<=available;++step)if(!loss||step%loss){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.5F;
                frame.cars[1].position[0]=step*.5F+8;timeline.push(frame,step);
            }
            sent=available;if(!timeline.sample(seconds,shown,ack))continue;
            const double position=shown.cars[0].position[0];
            if(elapsed>3){
                ++samples;held+=position==previous;
                maximum_error=std::max(maximum_error,std::abs((position-previous)/(.5*seconds*60)-1));
                paired=paired&&ack==shown.step&&std::abs(shown.cars[1].position[0]-position-8)<.001;
            }
            previous=position;
        }
        char name[160];std::snprintf(name,sizeof name,
            "cadence grid batch=%u pattern=%u loss=%u phase=%u max_error=%.6f held=%u",
            batch,cadence,loss,release_phase,maximum_error,held);
        check(samples>500&&paired&&!held&&maximum_error<.03,name);
    }
}

void check_timeline_pauses() {
    for(const unsigned group:{1U,4U,9U})
    for(const unsigned pause:{group+1,group+3,12U,17U,23U,31U,60U})if(pause>group)
    for(const bool accumulated:{false,true}){
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        std::vector<LanRaceFrame> held;unsigned resume_at=0,deadline=0;
        double before=0,after=1000;bool authoritative=true;
        constexpr unsigned start=720;
        const auto deliver=[&](unsigned step){LanRaceFrame frame;frame.step=step;
            frame.cars[0].position[0]=step*.5F;timeline.push(frame,step);};
        for(unsigned tick=1;tick<=start+pause+150;++tick){
            if(tick%group==0)for(unsigned step=tick-group+1;step<=tick;++step){
                if(step>start&&step<=start+pause){
                    if(accumulated){LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.5F;held.push_back(frame);}
                    continue;
                }
                if(step>start+pause){
                    for(const auto& frame:held)timeline.push(frame,frame.step);held.clear();
                    if(!resume_at){resume_at=tick;deadline=tick+2*(group+unsigned(std::ceil(before)))+8;}
                }
                deliver(step);
            }
            if(!timeline.sample(1./60,shown,ack))continue;
            const double position=shown.cars[0].position[0]*2;
            if(tick==start)before=tick-position;
            if(tick>start&&tick<=start+pause)authoritative&=position<=start;
            if(deadline&&tick==deadline)after=tick-position;
        }
        char name[160];std::snprintf(name,sizeof name,
            "pause recovery group=%u pause=%u accumulated=%u prior=%.3f recovered=%.3f",
            group,pause,unsigned(accumulated),before,after);
        check(authoritative&&std::abs(after-before)<.25,name);
    }
}

void check_timeline_pause_sequences() {
    for(const unsigned group:{1U,4U,9U})for(const unsigned spacing:{8U,20U})
    for(const bool accumulated:{false,true}){
        LanSnapshotBuffer timeline;LanRaceFrame shown;std::uint32_t ack=0;
        unsigned tick=0;double prior=0;std::vector<LanRaceFrame> held;
        const auto advance=[&](unsigned begin,unsigned end){
            ++tick;
            if(tick%group==0)for(unsigned step=tick-group+1;step<=tick;++step){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.5F;
                if(step>begin&&step<=end){if(accumulated)held.push_back(frame);continue;}
                if(step>end){for(const auto& delayed:held)timeline.push(delayed,delayed.step);held.clear();}
                timeline.push(frame,step);
            }
            timeline.sample(1./60,shown,ack);
        };
        while(tick<720)advance(0,0);
        prior=tick-shown.cars[0].position[0]*2;
        for(const unsigned pause:{6U,12U,16U,22U,23U,24U,30U,60U})if(pause>group){
            while(tick%group)advance(0,0);
            const unsigned begin=tick,end=begin+pause;
            const unsigned deadline=end+group+2*(group+unsigned(std::ceil(prior)))+8;
            while(tick<deadline)advance(begin,end);
            const double lag=tick-shown.cars[0].position[0]*2;
            char name[160];std::snprintf(name,sizeof name,
                "repeated pauses group=%u spacing=%u pause=%u accumulated=%u prior=%.3f recovered=%.3f",
                group,spacing,pause,unsigned(accumulated),prior,lag);
            check(std::abs(lag-prior)<.3&&ack==shown.step,name);
            const unsigned settle=tick+spacing*group;
            while(tick<settle)advance(0,0);
        }
    }
}

void check_timeline_pause_loss() {
    for(const unsigned loss:{0U,7U,19U})for(const unsigned pause:{2U,3U,12U,23U,30U,60U})
    for(const bool accumulated:{false,true}){
        LanSnapshotBuffer timeline,control;LanRaceFrame shown,reference;std::uint32_t ack=0,reference_ack=0;
        constexpr double periods[]={.017,.018,.033,.025,.017};
        double elapsed=0,maximum_extra_lag=0;unsigned sent=0;bool paired=true;
        std::vector<LanRaceFrame> held;constexpr unsigned start=720;
        for(unsigned display=0;elapsed<22;++display){
            const double seconds=periods[display%5];elapsed+=seconds;
            const auto available=unsigned(std::floor(elapsed*60+1e-8))/4*4;
            for(unsigned step=sent+1;step<=available;++step)if(!loss||step%loss){
                LanRaceFrame frame;frame.step=step;frame.cars[0].position[0]=step*.5F;
                control.push(frame,step);
                if(step>start&&step<=start+pause){if(accumulated)held.push_back(frame);continue;}
                if(step>start+pause){for(const auto& delayed:held)timeline.push(delayed,delayed.step);held.clear();}
                timeline.push(frame,step);
            }
            sent=available;control.sample(seconds,reference,reference_ack);
            if(!timeline.sample(seconds,shown,ack))continue;
            paired=paired&&std::isfinite(shown.cars[0].position[0])&&ack==shown.step;
            // A gap that never starves playback ages its temporary reserve
            // under the ordinary gentle clock trim instead of jumping time.
            if(elapsed*60>start+pause+(pause<12?420:90))
                maximum_extra_lag=std::max(maximum_extra_lag,double((reference.cars[0].position[0]-shown.cars[0].position[0])*2));
        }
        char name[160];std::snprintf(name,sizeof name,
            "fractional loss pause loss=%u pause=%u accumulated=%u extra_lag=%.6f",
            loss,pause,unsigned(accumulated),maximum_extra_lag);
        check(paired&&maximum_extra_lag<.5,name);
    }
}
