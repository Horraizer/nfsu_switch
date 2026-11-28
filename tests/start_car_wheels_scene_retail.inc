// Actual Switch scene commands, not a second root-motion implementation.
struct StartWheelSceneAudit {
    struct Previous {
        VehicleVec3 position{};
        std::array<double,4> phase{};
        bool valid=false,cinematic=false,end_snap=false;
        double travel=0;
        unsigned rolling=0,rolling_mask=0,stationary=0,submitted=0,steered=0;
    };
    std::vector<std::vector<PreparedInstance>> base;
    std::array<Previous,4> previous{};
    unsigned frames=0,transfers=0;
    void retain(const IntroGrid& grid) {
        const auto human=std::find_if(grid.actors.begin(),grid.actors.end(),
            [](const auto& actor){return actor.config.role==Event0Role::human;});
        if(human==grid.actors.end())throw std::runtime_error("wheel audit has no human");
        base.push_back(human->base_instances);
        for(const auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent)
            base.push_back(actor.base_instances);
    }
    void frame(const RaceSession& session,const RaceGpuFrame& frame) {
        ++frames;
        for(unsigned actor=0;actor<base.size();++actor) {
            const auto& racer=actor?session.rival(actor-1):session;
            const auto& state=racer.car();const auto& animation=racer.animation();
            auto& old=previous[actor];const bool cinematic=session.start_cinematic();
            old.steered+=cinematic&&std::abs(state.steer_angle)>1e-4;
            if(old.valid&&old.cinematic&&cinematic) {
                const double distance=std::hypot(state.position.x-old.position.x,state.position.y-old.position.y);
                if(old.end_snap&&distance!=0)
                    throw std::runtime_error("NIS rolling stopped before its final authored placement");
                if(distance>1e-5&&distance<1) {
                    old.travel+=distance;
                    // Native UpdateAnimatedCars suppresses travel on the
                    // controller's finishing update, even if that pose moves.
                    // Permit one final snap only after all wheels rolled and
                    // require the chassis to remain there on every later frame.
                    if(animation.wheel_phase==old.phase&&old.rolling_mask==15)old.end_snap=true;
                    else for(unsigned w=0;w<4;++w) {
                        if(animation.wheel_phase[w]<=old.phase[w])
                            throw std::runtime_error("moving NIS actor "+std::to_string(actor)+" has stationary wheel "+std::to_string(w));
                        old.rolling_mask|=1U<<w;
                    }
                    ++old.rolling;
                } else if(distance==0&&racer.physical_selector()==0) {
                    if(animation.wheel_phase!=old.phase)throw std::runtime_error("stationary NIS car acquired wheel rotation");
                    ++old.stationary;
                }
            }
            if(old.valid&&old.cinematic&&!cinematic) {
                for(unsigned w=0;w<4;++w)
                    if(std::abs(std::remainder(animation.wheel_phase[w]-old.phase[w],6.28318530717958647692))>1e-6)
                        throw std::runtime_error("NIS wheel phase reset at countdown handover");
                ++transfers;
            }
            std::vector<PreparedInstance> placed(base[actor].size());
            if(!drive_place_instances(base[actor],state,racer.spec(),placed,&animation))
                throw std::runtime_error("wheel audit could not place original car parts");
            const auto prefix=actor?"rival"+std::to_string(actor)+"/":std::string("race-car/");
            unsigned wheels=0;
            std::array<std::array<float,16>,4> published{};
            for(std::size_t d=0;d<frame.commands.draw_count();++d) {
                const auto draw=frame.commands.draw_view(d);
                if(draw.definition.name.compare(0,prefix.size(),prefix)||draw.instance>=placed.size())continue;
                const auto& expected=placed[draw.instance];
                if(expected.part_slot!=kSlotWheel&&expected.part_slot!=kSlotBrake)continue;
                for(unsigned k=0;k<16;++k)if(std::abs(expected.transform[k]-draw.placement.rows[k])>1e-5F)
                    throw std::runtime_error("NIS wheel/brake animation did not reach the scene command for actor "+std::to_string(actor));
                if(expected.part_slot==kSlotWheel) {
                    wheels|=1U<<expected.copy;
                    if(draw.submit)old.submitted|=1U<<expected.copy;
                    std::copy(draw.placement.rows,draw.placement.rows+16,published[expected.copy].begin());
                }
            }
            if(wheels!=15)throw std::runtime_error("wheel audit missed original rim command");
            if(frames%30==0||(old.valid&&old.cinematic!=cinematic)) {
                std::cout<<std::setprecision(10)<<"START_WHEEL actor="<<actor<<" ticks="<<session.start_scene_ticks()
                    <<" cinematic="<<cinematic<<" stage="<<int(session.timing().stage)
                    <<" position="<<state.position.x<<','<<state.position.y<<','<<state.position.z
                    <<" steer="<<state.steer_angle<<" phase=";
                for(double phase:animation.wheel_phase)std::cout<<phase<<',';
                std::cout<<" matrices=";
                for(const auto& matrix:published)for(float value:matrix)std::cout<<value<<',';
                std::cout<<'\n';
            }
            old.position=state.position;old.phase=animation.wheel_phase;
            old.valid=true;old.cinematic=cinematic;
        }
    }
    void finish() const {
        if(transfers!=base.size()||!previous[0].rolling||previous[0].travel<.1)
            throw std::runtime_error("wheel audit missed real moving intro or countdown handover");
        for(unsigned actor=0;actor<base.size();++actor) {
            const auto& row=previous[actor];
            if(row.travel>.1&&row.rolling_mask!=15)
                throw std::runtime_error("moving NIS actor "+std::to_string(actor)+" has stationary wheels");
            const char* cut=std::getenv("NFSU_CAREER_TEST_START_SCENE");
            if(cut&&std::string_view(cut)=="2303"&&!row.steered)
                throw std::runtime_error("turning NIS did not publish actor steering");
            if(row.submitted!=15)throw std::runtime_error("original rim animation never submitted");
            std::cout<<"PASS start wheel scene actor="<<actor<<" travel="<<row.travel
                <<" rolling_frames="<<row.rolling<<" stationary_frames="<<row.stationary
                <<" steering_frames="<<row.steered<<'\n';
        }
    }
};
