#include "nfsu/race_particles.h"
#include "nfsu/sha256.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

std::string frame_hash(const nfsu::RaceParticles& particles) {
    nfsu::RenderOptions options;
    options.width=320; options.height=180;
    options.camera.distance=5;
    options.camera.target={0,0,0};
    std::vector<unsigned char> rgba(options.width*options.height*4);
    for (std::size_t i=0; i<rgba.size(); i+=4) {
        rgba[i]=20; rgba[i+1]=30; rgba[i+2]=40; rgba[i+3]=255;
    }
    std::string error;
    check(nfsu::draw_race_particles(rgba,options,particles,error),"particle image failed");
    return nfsu::sha256_hex(nfsu::sha256(rgba.data(),rgba.size()));
}

nfsu::VehicleTrackTelemetry sample(std::uint64_t step) {
    nfsu::VehicleTrackTelemetry t;
    t.step=step;
    t.orientation.w=1;
    t.position={0,0,0.45};
    t.velocity={8,0,0};
    for (auto& wheel : t.model.wheels) {
        wheel.contact=true;
        wheel.slip_ratio=0.6;
    }
    return t;
}
} // namespace

int main() {
    try {
        nfsu::VehicleSpec spec;
        for (std::size_t wheel=0; wheel<4; ++wheel) {
            spec.handling.wheels[wheel].longitudinal=wheel<2?1.0F:-1.0F;
            spec.handling.wheels[wheel].lateral=wheel%2?0.65F:-0.65F;
            spec.handling.wheels[wheel].radius=0.30F;
        }
        nfsu::RaceParticles a,b;
        for (std::uint64_t step=1; step<=48; ++step) {
            const auto t=sample(step);
            a.advance(t,spec); b.advance(t,spec);
        }
        check(a.smoke_count()>0 && a.smoke_count()<=nfsu::kRaceSmokeLimit,
              "sliding did not produce bounded tyre smoke");
        check(a.spark_count()==0,"sliding produced sparks without impact");
        const auto smoke_hash=frame_hash(a);
        std::cout << "smoke-frame-sha256 " << smoke_hash << '\n';
        // A sliding tyre at fixed step 48. The hash pins the soft blended
        // quad and its step-derived drift in the CPU reference image.
        check(smoke_hash=="AF8DA763BC94DFAC4AE58BFFB6CAF1C3266F2F85D3DF6E37FB758952162C4E2A",
              "slide reference frame moved");
        check(smoke_hash==frame_hash(b),"identical steps produced different smoke frames");
        nfsu::RaceParticles below;
        for (std::uint64_t step=1; step<=48; ++step) {
            auto t=sample(step);
            for (auto& wheel : t.model.wheels) wheel.slip_ratio=0.25;
            below.advance(t,spec);
        }
        check(below.smoke_count()==0,"smoke threshold accepted equality");
        nfsu::RaceParticles wall;
        auto hit=sample(1);
        for (auto& wheel : hit.model.wheels) wheel.contact=false;
        hit.impulses=1; hit.impulse={3,0,0};
        wall.advance(hit,spec);
        check(wall.smoke_count()==0 && wall.spark_count()==2,
              "wall impulse did not produce two sparks");
        const auto spark_hash=frame_hash(wall);
        std::cout << "spark-frame-sha256 " << spark_hash << '\n';
        // A synthetic wall impulse at step 1: event1.txt has no hull hit.
        // This pins the additive burst without changing the replay input.
        check(spark_hash=="81E40F7D52F5AAC18E3A395BC27EC0B5D26663A19E25F3AB90C0D8F1A52236A3",
              "wall-hit reference frame moved");
        check(spark_hash!=frame_hash(below),"spark frame equals an empty frame");
        for (std::uint64_t step=49; step<=240; ++step) {
            auto t=sample(step);
            t.impulses=1; t.impulse={3,0,0};
            a.advance(t,spec);
        }
        check(a.smoke_count()<=nfsu::kRaceSmokeLimit &&
              a.spark_count()<=nfsu::kRaceSparkLimit,
              "particle live cap exceeded");
        std::cout << "PASS: deterministic smoke and wall sparks; bounded live slots\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
