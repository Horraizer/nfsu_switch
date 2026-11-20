// Verified by tools/ai_path_body_vectors.py against authenticated PC1.4 code.
// This file is included inside main so it uses the test's check function.
{
    struct PathBodyCase {
        float points[12][3];unsigned heading;float speed,yaw,parameter,acceleration;
        float position[3],seed[5][12];
    };
    struct PathBodyFrame {
        unsigned variant,frame;float dt;unsigned missing;float parameter,rate;
        float position[3],velocity[3],axes[3][3];unsigned heading;
        float yaw,speed[2];unsigned sliding;float curvature,channels[5][4];
    };
    struct PathDepartureCase {
        float points[6][3],parameter,rate,acceleration,elapsed,scale,mass,front,rear,gap;
        unsigned departure;
    };
    struct PathProjectionCase {float points[6][3],position[3],parameter;};
    struct PathSpeedCase {
        float points[12][3],position[3],velocity[3],speed,desired,duration,mass,drag,
            scale,draft,limit,length,parameter,rate,acceleration,ppm,channels[5][12];
    };
#include "ai_path_body_native.inc"
    for(const auto& c:path_speed_cases) {
        AiPathBodyState state;state.path.count=12;
        for(unsigned k=0;k<12;++k)std::copy_n(c.points[k],3,state.path.points[k].begin());
        const PlayerRigidVector position{{c.position[0],c.position[1],c.position[2]}};
        state.parameter=ai_path_body_project(state.path,position);
        state.points_per_metre=11.F/c.length;
        ai_path_body_publish(state,position,{{c.velocity[0],c.velocity[1],c.velocity[2]}});
        auto handling=stock_240sx_handling();handling.mass=c.mass;handling.drag=c.drag;
        ai_path_body_speed_target(state,handling,path_speed_curve,
            {c.speed,c.desired,c.duration,c.scale,c.draft,c.limit});
        check(state.parameter==c.parameter&&state.points_per_metre==c.ppm&&
              state.rate==c.rate&&state.acceleration==c.acceleration&&
              state.channels[0].value==c.channels[0][0]&&state.channels[0].derivative==c.channels[0][1],
              "whole original463910 publication: projection, lateral motion, signed speed and acceleration cap");
    }
    for(const auto& c:path_projection_cases) {
        AiDriverPath path;path.count=6;
        for(unsigned k=0;k<6;++k)std::copy_n(c.points[k],3,path.points[k].begin());
        const auto parameter=ai_path_body_project(path,{{c.position[0],c.position[1],c.position[2]}});
        check(parameter==c.parameter,"original59D430/59D540 path projection retains float operation order");
    }
    for(const auto& c:path_departure_cases) {
        AiPathBodyState state;state.path.count=6;
        for(unsigned k=0;k<6;++k)std::copy_n(c.points[k],3,state.path.points[k].begin());
        state.parameter=c.parameter;state.rate=c.rate;state.acceleration=c.acceleration;
        state.elapsed=c.elapsed;state.points_per_metre=c.scale;
        auto handling=stock_240sx_handling();handling.mass=c.mass;
        handling.downforce_front=c.front;handling.downforce_rear=c.rear;
        check(ai_path_body_departure(state,handling)==(c.departure!=0),
            "original463D50 fixed publication speed decides the strict departure threshold");
    }
    const auto stationary=ai_path_body_motion({}, {},0,0,0,.0001F);
    check(stationary.velocity==PlayerRigidVector{}&&stationary.magnitude==0&&stationary.yaw==0,
        "stationary zero-vector safety policy cannot publish original masked-x87 NaN");
    std::array<AiPathBodyState,std::size(path_body_cases)> states;
    for(unsigned i=0;i<states.size();++i) {
        const auto& c=path_body_cases[i];auto& s=states[i];
        ai_path_body_seed(s,std::uint16_t(c.heading),c.speed,c.yaw);
        for(unsigned k=0;k<5;++k) {
            const auto& channel=s.channels[k];
            check(std::abs(channel.value-c.seed[k][0])<2e-5F&&
                  std::abs(channel.derivative-c.seed[k][1])<2e-5F&&
                  channel.duration==c.seed[k][9]&&channel.status==c.seed[k][11],
                  "original463436 path heading initialization");
        }
        s.path.count=12;
        for(unsigned k=0;k<12;++k)std::copy_n(c.points[k],3,s.path.points[k].begin());
        s.parameter=c.parameter;s.rate=c.speed*.1F;s.acceleration=c.acceleration;s.points_per_metre=.1F;
        s.channels[0].value=.5F;s.channels[0].derivative=.1F;
        s.channels[0].duration=.5F;s.channels[0].continuous=false;s.channels[0].status=2;
        s.channels[1].duration=.4F;s.channels[2].duration=.4F;
    }
    auto h=stock_240sx_handling();h.mass=1.5F;h.downforce_front=.012F;h.downforce_rear=.014F;
    for(auto& tyre:h.tyres){tyre.lateral_scale=1;tyre.static_friction=1.2F;tyre.sliding_friction=.9F;}
    float maximum_position=0,maximum_value=0,maximum_derivative=0,maximum_curvature=0;
    for(const auto& c:path_body_frames) {
        auto& s=states[c.variant];
        if(c.frame==50) {
            const auto& previous=path_body_frames[c.variant*100+c.frame-1];
            s.parameter=c.parameter;s.rate=c.rate;
            ai_path_body_publish(s,{{previous.position[0],previous.position[1],previous.position[2]}},
                {{previous.velocity[0],previous.velocity[1],previous.velocity[2]}});
        }
        const auto result=ai_path_body_step(s,h,c.dt);
        VehicleVec3 position{result.position[0],result.position[1],result.position[2]};
        PlayerRigidMatrix axes;
        const auto hubs=vehicle_placement_hubs(h,result.heading,position,axes);
        std::array<VehicleGroundHit,4> hits{};
        for(unsigned k=0;k<4;++k) {
            hits[k].hit=!(c.missing&(1U<<k));hits[k].has_plane=true;
            hits[k].plane={0,0,1,float(hubs[k].x*.02+hubs[k].y*.01)};
        }
        vehicle_placement_ground(h,hubs,hits,position,axes);
        // Isolate publication from the position tolerance: these are the
        // original actor's before/after ground-fit inputs to4642AD.
        const auto& initial=path_body_cases[c.variant];
        const float* previous_position=c.frame?path_body_frames[c.variant*100+c.frame-1].position:initial.position;
        const unsigned previous_heading=c.frame?path_body_frames[c.variant*100+c.frame-1].heading:initial.heading;
        const auto publication=ai_path_body_motion(
            {{previous_position[0],previous_position[1],previous_position[2]}},
            {{c.position[0],c.position[1],c.position[2]}},std::uint16_t(previous_heading),
            std::uint16_t(c.heading),c.speed[0],c.dt);
        const auto close=[](float actual,float expected) {
            return std::abs(actual-expected)<=std::max(2e-4F,std::abs(expected)*3e-6F);
        };
        bool publication_match=close(publication.yaw,c.yaw)&&close(publication.magnitude,c.speed[1]);
        for(unsigned k=0;k<3;++k)publication_match=publication_match&&close(publication.velocity[k],c.velocity[k]);
        if(!publication_match)std::cerr<<"PATH publication variant="<<c.variant<<" frame="<<c.frame<<'\n';
        check(publication_match,"original4642AD post-ground velocity and yaw publication, including dt cutoff");
        const float actual[]={float(position.x),float(position.y),float(position.z)};
        bool match=result.heading==c.heading&&s.sliding==(c.sliding!=0)&&
            std::abs(result.speed-c.speed[0])<1e-4F&&std::abs(s.curvature_average-c.curvature)<5e-4F;
        for(unsigned k=0;k<3;++k) {
            match=match&&std::abs(actual[k]-c.position[k])<1e-4F;
            for(unsigned j=0;j<3;++j)match=match&&std::abs(axes[k][j]-c.axes[k][j])<1e-5F;
        }
        for(unsigned k=0;k<5;++k) {
            const auto& channel=s.channels[k];
            match=match&&std::abs(channel.value-c.channels[k][0])<1e-4F&&
                std::abs(channel.derivative-c.channels[k][1])<2e-4F&&
                channel.duration==c.channels[k][2]&&channel.status==c.channels[k][3];
        }
        for(unsigned k=0;k<3;++k)maximum_position=std::max(maximum_position,std::abs(actual[k]-c.position[k]));
        maximum_curvature=std::max(maximum_curvature,std::abs(s.curvature_average-c.curvature));
        for(unsigned k=0;k<5;++k){
            maximum_value=std::max(maximum_value,std::abs(s.channels[k].value-c.channels[k][0]));
            maximum_derivative=std::max(maximum_derivative,std::abs(s.channels[k].derivative-c.channels[k][1]));
        }
        if(!match)std::cerr<<"PATH native variant="<<c.variant<<" frame="<<c.frame
            <<" heading="<<result.heading<<" expected="<<c.heading
            <<" position="<<actual[0]<<','<<actual[1]<<','<<actual[2]
            <<" expected="<<c.position[0]<<','<<c.position[1]<<','<<c.position[2]<<'\n';
        check(match,"original463F20 retained path body, slip and ground placement");
    }
    std::cout<<"PATH maximum position="<<maximum_position<<" channel="<<maximum_value
        <<" derivative="<<maximum_derivative<<" curvature="<<maximum_curvature<<'\n';
}
