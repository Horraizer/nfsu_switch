#include "nfsu/race_particles.h"

#include <algorithm>
#include <cmath>

namespace nfsu {
namespace {
constexpr std::uint64_t kSmokeLife = 60;
constexpr std::uint64_t kSparkLife = 18;

VehicleVec3 rotate(const VehicleQuat& q, const VehicleVec3& v) noexcept {
    const double xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    const double xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    const double wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    return {
        (1-2*(yy+zz))*v.x + 2*(xy-wz)*v.y + 2*(xz+wy)*v.z,
        2*(xy+wz)*v.x + (1-2*(xx+zz))*v.y + 2*(yz-wx)*v.z,
        2*(xz-wy)*v.x + 2*(yz+wx)*v.y + (1-2*(xx+yy))*v.z};
}

VehicleVec3 add(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}

// Stateless integer mixing. The step and wheel uniquely determine each
// direction; changing render frequency cannot change a particle.
std::uint32_t seed(std::uint64_t step, std::uint32_t wheel, std::uint32_t lane) noexcept {
    std::uint32_t x = static_cast<std::uint32_t>(step) ^
        static_cast<std::uint32_t>(step >> 32) ^ (wheel+1)*0x9E3779B9U ^
        (lane+1)*0x85EBCA6BU;
    x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15;
    x *= 0x846CA68BU; x ^= x >> 16;
    return x;
}

double jitter(std::uint64_t step, std::uint32_t wheel, std::uint32_t lane) noexcept {
    return static_cast<double>(seed(step,wheel,lane) & 0xFFFFU)/32767.5-1.0;
}

template<std::size_t N>
void expire(std::array<RaceParticle,N>& slots, std::size_t& count,
            std::uint64_t step, std::uint64_t life) noexcept {
    std::size_t keep = 0;
    for (std::size_t i=0; i<count; ++i)
        if (step >= slots[i].born_step &&
            (slots[i].elapsed_seconds>=0?slots[i].elapsed_seconds:static_cast<double>(step-slots[i].born_step)*slots[i].solver_dt) <
            static_cast<double>(life)*kVehicleStepSeconds)
            slots[keep++] = slots[i];
    count = keep;
}

template<std::size_t N>
void emit(std::array<RaceParticle,N>& slots, std::size_t& count,
          const RaceParticle& particle) noexcept {
    if (count == N) {
        for (std::size_t i=1; i<N; ++i) slots[i-1] = slots[i];
        --count;
    }
    slots[count++] = particle;
}
} // namespace

void RaceParticles::reset() noexcept {
    smoke_count_ = spark_count_ = 0;
    step_ = 0;seconds_=last_solver_dt_=0;variable_time_=false;
}

void RaceParticles::advance(const RaceSession& session) noexcept {
    const auto& spec = session.spec();
    for (const auto& t : session.physics_telemetry()) advance(t,spec);
}

void RaceParticles::advance(const VehicleTrackTelemetry& t,
                            const VehicleSpec& spec) noexcept {
        if (!t.step || t.step <= step_) return;
        if (step_ && t.step != step_+1) { smoke_count_ = 0; spark_count_ = 0; }
        const auto previous_step=step_;
        step_ = t.step;
        const double solver_dt=t.model.solver_dt;
        if(!(solver_dt>0.0)||!std::isfinite(solver_dt))return;
        const double previous_seconds=seconds_;
        variable_time_=variable_time_||(last_solver_dt_>0&&last_solver_dt_!=solver_dt);
        seconds_=variable_time_?seconds_+solver_dt:double(step_)*solver_dt;
        last_solver_dt_=solver_dt;
        if(variable_time_) {
            const auto age=[&](auto& particles,std::size_t count){for(std::size_t i=0;i<count;++i){auto& p=particles[i];
                if(p.elapsed_seconds<0)p.elapsed_seconds=double(previous_step-p.born_step)*p.solver_dt;
                p.elapsed_seconds+=solver_dt;}};
            age(smoke_,smoke_count_);age(sparks_,spark_count_);
        }
        expire(smoke_,smoke_count_,step_,kSmokeLife);
        expire(sparks_,spark_count_,step_,kSparkLife);
        if (t.reset != VehicleTrackReset::none) {
            smoke_count_ = spark_count_ = 0;
            return;
        }
        const auto& q = t.orientation;
        const auto& com = spec.handling.centre_of_mass;
        const VehicleVec3 body_origin = add(t.position,rotate(q,
            {-com[0],-com[1],-com[2]}));
        const double speed = std::sqrt(t.velocity.x*t.velocity.x+
                                       t.velocity.y*t.velocity.y);
        const auto emission_step=static_cast<std::uint64_t>(
            (variable_time_?seconds_:static_cast<double>(step_)*solver_dt)/kVehicleStepSeconds+1e-9);
        const auto previous_emission_step=static_cast<std::uint64_t>(
            (variable_time_?previous_seconds:static_cast<double>(step_-1)*solver_dt)/kVehicleStepSeconds+1e-9);
        for (std::uint32_t wheel=0; wheel<kVehicleWheelCount; ++wheel) {
            const auto& w = t.model.wheels[wheel];
            const double slip = std::max(std::abs(w.slip_ratio),std::abs(w.slip_lateral));
            // APPROXIMATION: the retail particle threshold remains untraced.
            if (!w.contact || speed <= 2.0 || slip <= 0.25 ||
                emission_step==previous_emission_step || emission_step % 4 != wheel) continue;
            const auto& geometry = spec.handling.wheels[wheel];
            const VehicleVec3 local{geometry.longitudinal,geometry.lateral,
                                    geometry.vertical-geometry.radius};
            RaceParticle p;
            p.kind = RaceParticleKind::smoke;
            p.born_step = step_;
            p.solver_dt = solver_dt;p.elapsed_seconds=variable_time_?0:-1;
            p.origin = add(body_origin,rotate(q,local));
            p.velocity = {t.velocity.x*0.20+jitter(emission_step,wheel,0)*0.28,
                          t.velocity.y*0.20+jitter(emission_step,wheel,1)*0.28,
                          0.35+jitter(emission_step,wheel,2)*0.08};
            p.size = 0.42+jitter(emission_step,wheel,3)*0.03;
            emit(smoke_,smoke_count_,p);
        }
        if (t.impulses && speed > 1.0) {
            //Use a real solver witness, never a guessed body-side point.
            //The emission threshold/appearance below remain port policy.
            const VehicleContactObservation* hit=nullptr;double largest=0;
            for(std::size_t i=0;i<std::min(t.audio_contact_count,t.audio_contacts.size());++i){const auto& c=t.audio_contacts[i];
                const double magnitude=(c.impulse.x*c.impulse.x+c.impulse.y*c.impulse.y)+c.impulse.z*c.impulse.z;
                if(std::isfinite(magnitude)&&magnitude>largest&&std::isfinite(c.point.x)&&std::isfinite(c.point.y)&&std::isfinite(c.point.z)){
                    hit=&c;largest=magnitude;
                }
            }
            if(!hit)return;
            const VehicleVec3 point=hit->point;
            for (std::uint32_t lane=0; lane<2; ++lane) {
                RaceParticle p;
                p.kind = RaceParticleKind::spark;
                p.born_step = step_;
                p.solver_dt = solver_dt;p.elapsed_seconds=variable_time_?0:-1;
                p.origin = point;
                p.velocity = {t.velocity.x*0.10+jitter(step_,lane,4)*2.0,
                              t.velocity.y*0.10+jitter(step_,lane,5)*2.0,
                              0.8+jitter(step_,lane,6)*0.6};
                p.size = 0.055;
                emit(sparks_,spark_count_,p);
            }
        }
}

bool race_particle_visual(const RaceParticle& particle, std::uint64_t step,
                          VehicleVec3& position, double& diameter,
                          double& opacity) noexcept {
    const auto life = particle.kind == RaceParticleKind::smoke ? kSmokeLife : kSparkLife;
    if (step < particle.born_step) return false;
    const double elapsed_steps=static_cast<double>(step-particle.born_step);
    const double life_steps=static_cast<double>(life)*kVehicleStepSeconds/particle.solver_dt;
    const bool managed=particle.elapsed_seconds>=0;
    if(managed?particle.elapsed_seconds>=double(life)*kVehicleStepSeconds:elapsed_steps>=life_steps)return false;
    const double age=managed?particle.elapsed_seconds:(particle.solver_dt==kVehicleStepSeconds?elapsed_steps/120.0:
        elapsed_steps*particle.solver_dt);
    const double fraction=managed?age/(double(life)*kVehicleStepSeconds):elapsed_steps/life_steps;
    position = {particle.origin.x+particle.velocity.x*age,
                particle.origin.y+particle.velocity.y*age,
                particle.origin.z+particle.velocity.z*age};
    if (particle.kind == RaceParticleKind::smoke) {
        diameter = particle.size*(1.0+2.2*fraction);
        opacity = 0.70*(1.0-fraction);
    } else {
        diameter = particle.size*(1.0-0.55*fraction);
        opacity = 0.95*(1.0-fraction);
    }
    return true;
}

std::array<std::uint8_t,4> race_particle_texel(RaceParticleKind kind,
                                                unsigned x, unsigned y) noexcept {
    const int dx = static_cast<int>(x)*2-15;
    const int dy = static_cast<int>(y)*2-15;
    const int radius = dx*dx+dy*dy;
    if (kind == RaceParticleKind::smoke) {
        const int alpha = std::max(0, 210-radius);
        return {210,208,205,static_cast<std::uint8_t>(alpha*175/210)};
    }
    const int alpha = std::max(0, 110-radius);
    return {255,214,112,static_cast<std::uint8_t>(alpha*255/110)};
}

bool draw_race_particles(std::vector<unsigned char>& rgba,
                         const RenderOptions& options,
                         const RaceParticles& particles,
                         std::string& error,
                         const Frame* track,
                         const Frame* car) {
    if (rgba.size() != static_cast<std::uint64_t>(options.width)*options.height*4) {
        error="particle frame size differs from the render options";
        return false;
    }
    if ((track || car) && (!track || !car ||
        track->width()!=options.width || track->height()!=options.height ||
        car->width()!=options.width || car->height()!=options.height)) {
        error="particle depth frames differ from the render options";
        return false;
    }
    CameraFrame camera;
    if (!make_camera_frame(options.camera,options.width,options.height,camera,error)) return false;
    const auto draw = [&](const RaceParticle& particle) {
        VehicleVec3 at;
        double diameter=0, opacity=0;
        if (!race_particle_visual(particle,particles.step(),at,diameter,opacity)) return;
        const ClipVertex clip=project_point(camera,{at.x,at.z,-at.y});
        if (clip.w <= camera.near_plane || clip.w > camera.far_plane) return;
        const double cx=(clip.x/clip.w+1.0)*options.width*0.5;
        const double cy=(1.0-clip.y/clip.w)*options.height*0.5;
        const double pixels=std::min(diameter*camera.focal_y*options.height/(2.0*clip.w),
                                     48.0*options.height/720.0);
        if (pixels < 0.5) return;
        const double half=pixels*0.5;
        const int left=std::max(0,static_cast<int>(std::floor(cx-half)));
        const int right=std::min(static_cast<int>(options.width),
                                 static_cast<int>(std::ceil(cx+half)));
        const int top=std::max(0,static_cast<int>(std::floor(cy-half)));
        const int bottom=std::min(static_cast<int>(options.height),
                                  static_cast<int>(std::ceil(cy+half)));
        for (int y=top; y<bottom; ++y) for (int x=left; x<right; ++x) {
            const int tx=std::clamp(static_cast<int>(16.0*((x+0.5-cx)/pixels+0.5)),0,15);
            const int ty=std::clamp(static_cast<int>(16.0*((y+0.5-cy)/pixels+0.5)),0,15);
            const auto texel=race_particle_texel(particle.kind,
                static_cast<unsigned>(tx),static_cast<unsigned>(ty));
            const int alpha=static_cast<int>(texel[3]*opacity+0.5);
            if (!alpha) continue;
            const auto index=static_cast<std::size_t>(y)*options.width+x;
            if (track) {
                double scene_depth=track->depth()[index];
                if (car->ids()[index]!=kNoTriangle && car->depth()[index]<=scene_depth)
                    scene_depth=car->depth()[index];
                if (clip.z/clip.w > scene_depth) continue;
            }
            const auto pixel=index*4;
            for (unsigned channel=0; channel<3; ++channel) {
                const int value=particle.kind == RaceParticleKind::smoke
                    ? (texel[channel]*alpha+rgba[pixel+channel]*(255-alpha)+127)/255
                    : std::min(255,static_cast<int>(rgba[pixel+channel])+
                                   (texel[channel]*alpha+127)/255);
                rgba[pixel+channel]=static_cast<unsigned char>(value);
            }
        }
    };
    for (std::size_t i=0; i<particles.smoke_count(); ++i) draw(particles.smoke(i));
    for (std::size_t i=0; i<particles.spark_count(); ++i) draw(particles.spark(i));
    error.clear();
    return true;
}
} // namespace nfsu
