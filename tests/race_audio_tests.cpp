#include "nfsu/race_audio.h"
#include "nfsu/audio.h"
#include "nfsu/arg_path.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_weights() {
    const auto low = nfsu::audio::engine_layer_weights(1000);
    check(std::abs(low[0] - 15.0 / 17.0) < 1e-12 &&
          std::abs(low[1] - 2.0 / 17.0) < 1e-12 &&
          low[2] == 0 && low[3] == 0, "engine crossfade direction");
    const auto high = nfsu::audio::engine_layer_weights(6000);
    check(high[0] == 0 && high[1] == 0 &&
          std::abs(high[2] - 0.4) < 1e-12 &&
          std::abs(high[3] - 0.6) < 1e-12,
          "engine high crossfade direction");
}

void test_playlist_order() {
    nfsu::audio::Bank bank;
    bank.entries = {{1, 200, 0}, {1, 0, 0}, {1, 100, 0}};
    std::vector<nfsu::audio::Segment> indexed(3);
    indexed[0].offset = 0;
    indexed[1].offset = 100;
    indexed[2].offset = 200;
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 2 && playlist[0].offset == 200 &&
          playlist[1].offset == 100,
          "music table order with preview segment skipped");
    bank.entries[0].value = 300;
    bool rejected = false;
    try { (void)nfsu::audio::race_music_playlist(bank, indexed); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "music bank reference must resolve");
}

void test_missing_audio() {
    nfsu::audio::RaceAudioService service;
    std::string diagnostic;
    check(!service.start("nfsu-audio-missing-fixture-835148", diagnostic),
          "missing audio must not start");
    check(diagnostic.find("music: missing AST") != std::string::npos &&
          service.diagnostic() == diagnostic,
          "missing music fallback and visible diagnostic");
}

// The retail root comes only from the command line (issue #233): CTest
// registers the retail run, nfsu_race_audio_retail, with the configured
// NFSU_RETAIL_ROOT, and the plain run has no root and no retail half.
void test_retail_music_prefix(const std::filesystem::path* root) {
    if (!root) {
        std::cout << "race audio: retail check not run here, no root argument "
                     "(see nfsu_race_audio_retail)\n";
        return;
    }
    check(!root->empty(), "the retail root argument is not empty");
    std::ifstream input(*root /
        "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
    check(bool(input), "retail music stream available");
    unsigned polls = 0;
    const auto segments = nfsu::audio::scan_ast_prefix(input,
        6 * 1024 * 1024, 2, [&] { ++polls; });
    check(segments.size() == 2 && segments[1].end == 5343716 &&
          segments[1].rate == 22050 && segments[1].channels == 2 &&
          polls >= 20, "retail music bounded index and progress");

    std::ifstream bank_input(*root /
        "Sound/STRMBANK/STRM_Music_UG0_MB.abk", std::ios::binary);
    check(bool(bank_input), "retail music bank available");
    const std::vector<std::uint8_t> bank_bytes(
        (std::istreambuf_iterator<char>(bank_input)),
        std::istreambuf_iterator<char>());
    const auto bank = nfsu::audio::parse_abk(bank_bytes);
    std::vector<std::uint64_t> offsets;
    for (const auto& entry : bank.entries)
        if (entry.type == 1) offsets.push_back(entry.value);
    std::sort(offsets.begin(), offsets.end());
    check(offsets.size() == 27 && offsets.front() == 0 &&
          std::adjacent_find(offsets.begin(), offsets.end()) == offsets.end(),
          "music bank has distinct segment offsets");
    input.clear();
    input.seekg(0, std::ios::end);
    const auto stream_size = static_cast<std::uint64_t>(input.tellg());
    std::vector<nfsu::audio::Segment> indexed;
    for (std::size_t i = 0; i < offsets.size(); ++i)
        indexed.push_back(nfsu::audio::index_ast_segment_by_boundary(input,
            offsets[i], i + 1 < offsets.size() ? offsets[i + 1] : stream_size));
    const auto fully_scanned = nfsu::audio::scan_ast(input);
    check(fully_scanned.size() == indexed.size(),
          "retail music index count matches full scan");
    for (std::size_t i = 0; i < indexed.size(); ++i)
        check(indexed[i].offset == fully_scanned[i].offset &&
              indexed[i].end == fully_scanned[i].end &&
              indexed[i].frames == fully_scanned[i].frames &&
              indexed[i].rate == fully_scanned[i].rate &&
              indexed[i].channels == fully_scanned[i].channels &&
              indexed[i].blocks == fully_scanned[i].blocks &&
              indexed[i].codec == fully_scanned[i].codec,
              "bank-boundary index matches full retail AST scan");
    check(indexed[1].offset == segments[1].offset &&
          indexed[1].end == segments[1].end &&
          indexed[1].frames == segments[1].frames &&
          indexed[16].codec == nfsu::audio::Codec::ea_xa_v1 &&
          indexed[16].rate == 44100 && indexed[26].end <= stream_size,
          "all retail music segments indexed by bank boundaries");
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 26 && playlist.front().offset == 717824 &&
          playlist.back().offset == 143246848,
          "retail playlist follows full song entries");
    std::cout << "race audio: retail music prefix pinned\n";
}

void test_live_collision_clock() {
    using namespace nfsu;
    using namespace nfsu::audio;
    VehicleTrackCourse course;
    std::string error;
    check(build_vehicle_proving_ground(course,error),"audio clock proving ground");
    VehicleTrackScenario scenario;
    check(vehicle_track_scenario(VehicleTrackCourseKind::proving_ground,"wall-square",scenario),
          "audio clock wall scenario");
    course.start=vehicle_track_scenario_pose(course.start,scenario);
    RaceSession race(course), silent(course);
    check(race.start(error) && silent.start(error),"audio clock races start");
    CollisionGraph graph;
    graph.impact_gain.fill(20000); graph.scrape_gain.fill(20000);
    graph.scrape_pitch.fill(4096); graph.scrape_gain_targets.fill(32767);
    CollisionScheduler scheduler;
    unsigned countdown=0, contacts=0, scrapes=0, impacts=0;
    std::map<std::uint32_t,std::uint64_t> last_contact;
    while (race_car_held(race.timing()) && countdown<1000) {
        const auto pad=race_input_frame({1,0,0},countdown++);
        check(race.advance(pad,error) && silent.advance(pad,error),"audio countdown advances");
        const auto snapshot=race_audio_snapshot(race);
        check(snapshot.tick==race.timing().step && race.telemetry().step==0 &&
              snapshot.physics_step==0 && snapshot.contact_count==0,"countdown advances race clock without physics contacts");
        const auto events=scheduler.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),snapshot.contact_count,
                                           graph,snapshot.collision_reset);
        check(events.impacts.empty() && events.scrapes.empty(),"countdown has no collision voices");
    }
    check(countdown>0 && countdown<1000,"audio countdown reaches GO");
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for (unsigned n=0;n<2000;++n) {
        const bool reset=n==1700;
        const auto buttons=gas | (reset ? gamepad_button_bit(GamepadButton::select) : 0U);
        const auto pad=race_input_frame({1,buttons,0},countdown+n);
        check(race.advance(pad,error) && silent.advance(pad,error),"audio wall race advances");
        const auto snapshot=race_audio_snapshot(race);
        check(snapshot.physics_step==race.telemetry().step &&
              snapshot.tick>snapshot.physics_step/kDriveStepsPerFrame,
              "real countdown leaves distinct clock epochs");
        std::size_t copied=0;
        for (const auto& step : race.physics_telemetry()) {
            for (std::size_t k=0;k<step.audio_contact_count;++k) {
                const auto& input=step.audio_contacts[k];
                const auto& output=snapshot.contacts[copied++];
                check(output.step==step.step && output.pair==input.polygon &&
                      output.impulse.x==input.impulse.x && output.slide_mps==input.slide_mps,
                      "snapshot preserves physical contact identity, clock and value");
            }
        }
        check(copied==snapshot.contact_count,"snapshot copies all physics slices");
        const auto events=scheduler.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),snapshot.contact_count,
                                           graph,snapshot.collision_reset);
        if (reset) {
            check(snapshot.collision_reset && events.impacts.empty() && events.scrapes.empty(),
                  "real reset discards all contacts from its audio frame");
            last_contact.clear();
        } else {
            contacts+=unsigned(snapshot.contact_count); scrapes+=unsigned(events.scrapes.size());
            for (const auto& voice : events.impacts) {
                ++impacts;
                // Find this impact's observation in substep order. There can
                // be several updates for one polygon in the same audio frame.
                auto& last=last_contact[voice.pair];
                const CollisionContact* observed=nullptr;
                for (std::size_t k=0;k<snapshot.contact_count;++k)
                    if (snapshot.contacts[k].pair==voice.pair && snapshot.contacts[k].step>last &&
                        world_collision_subtype(snapshot.contacts[k])>=0) {
                        observed=&snapshot.contacts[k]; break;
                    }
                check(observed!=nullptr,"impact command belongs to copied real contact");
                if (last && observed->step==last+1) {
                    check(!voice.start,"continuous physical contact does not restart impact after countdown");
                }
                last=observed->step;
            }
        }
        if (!events.scrapes.empty()) {
            // Pause keeps the session still. Re-reading the snapshot must
            // neither replay an impact nor advance graph control state.
            const auto record=race.record();
            for (unsigned repeat=0;repeat<3;++repeat) {
                const auto paused=race_audio_snapshot(race);
                check(!scheduler.update(paused.tick,paused.physics_step,paused.contacts.data(),paused.contact_count,
                                         graph,paused.collision_reset).processed,
                      "paused or duplicate real snapshot does not advance collision graph");
            }
            check(race.record()==record,"paused snapshot leaves simulation record unchanged");
        }
        check(race.record()==silent.record(),"audio observation leaves real race replay unchanged");
    }
    check(contacts>0 && scrapes>0 && impacts>0,
          "real countdown wall run produces scrape and impact commands");
}

void test_collision_clock_contract() {
    using namespace nfsu::audio;
    CollisionGraph graph;
    graph.impact_gain.fill(20000); graph.scrape_gain.fill(20000);
    graph.scrape_pitch.fill(4096);
    // Four full hull batches for one continuous polygon episode. The old
    //512 cap lost steps3/4, then expired the scrape using newest step4.
    std::array<CollisionContact,1024> four_slices{};
    for(std::size_t i=0;i<four_slices.size();++i) {
        auto& c=four_slices[i];
        c.step=1+i/256;c.pair=7;c.impulse={1,0,0};c.slide_mps=20;
    }
    CollisionScheduler full_frame;
    const auto complete=full_frame.update(1,4,four_slices.data(),four_slices.size(),graph);
    check(complete.impacts.size()==4 && complete.scrapes.size()==1 &&
          complete.impacts.front().start && !complete.impacts.back().start,
          "contacts beyond512 preserve newest slice and continuous scrape episode");
    for(auto& c:four_slices) c.other_kind=0;
    four_slices.back().other_kind=3;
    CollisionScheduler last_contact;
    const auto last=last_contact.update(1,4,four_slices.data(),four_slices.size(),graph);
    check(last.impacts.size()==1 && last.scrapes.size()==1 && last.impacts[0].pair==7,
          "last bounded contact at index1023 reaches the scheduler");
    for (int i=0;i<10;++i) {
        graph.scrape_gain_targets[i]=std::int16_t(15000+i*1000);
        graph.scrape_pitch_targets[i]=std::int16_t(200+i*10);
    }
    const auto equal=[](const CollisionAudioFrame& a,const CollisionAudioFrame& b) {
        const auto voices=[](const auto& x,const auto& y) {
            if (x.size()!=y.size()) return false;
            for (std::size_t i=0;i<x.size();++i)
                if (x[i].pair!=y[i].pair || x[i].sample!=y[i].sample ||
                    x[i].gain_q15!=y[i].gain_q15 || x[i].pitch_q16!=y[i].pitch_q16 ||
                    x[i].start!=y[i].start) return false;
            return true;
        };
        return a.processed==b.processed && voices(a.impacts,b.impacts) && voices(a.scrapes,b.scrapes);
    };
    CollisionScheduler zero, offset;
    for (unsigned tick=1;tick<=180;++tick) {
        CollisionContact c[2];
        for (unsigned k=0;k<2;++k) {
            c[k].step=2*tick-1+k; c[k].pair=7;
            c[k].impulse={1,0,0}; c[k].slide_mps=20;
        }
        // A full empty physics frame must expire the episode even without
        // a contact from which an expiry clock could be inferred.
        const auto count=tick==90 ? 0U : 2U;
        const auto a=zero.update(tick,2*tick,c,count,graph);
        const auto b=offset.update(tick+55,2*tick,c,count,graph);
        check(equal(a,b),"collision output independent of race clock epoch");
        if (tick==90) {
            check(a.processed && a.impacts.empty() && a.scrapes.empty(),"empty physics frame expires scrape");
        } else {
            check(a.impacts.size()==2 && a.scrapes.size()==1,"continuous contact retains scrape after clock offset");
            const bool start=tick==1 || tick==91;
            check(a.impacts[0].start==start && !a.impacts[1].start && a.scrapes[0].start==start,
                  "continuous episode starts only initially or after empty-contact expiry");
        }
        check(!offset.update(tick+55,2*tick,c,count,graph).processed,
              "duplicate frame preserves graph control and random state");
    }
    CollisionContact c; c.step=362; c.pair=7; c.impulse={1,0,0}; c.slide_mps=20;
    // A changed race tick with an unchanged physics clock represents a held
    // simulation. It must not age contacts, replay impacts or advance ramps.
    check(!offset.update(236,360,&c,1,graph).processed,"held physics clock freezes graph");
    check(equal(zero.update(181,362,&c,1,graph),offset.update(237,362,&c,1,graph)),
          "resume matches uninterrupted graph state after duplicate and held frames");
    auto f=offset.update(238,364,&c,1,graph,true);
    check(f.processed && f.impacts.empty() && f.scrapes.empty(),"reset clears voices and discards copied contacts");
    check(!offset.update(238,364,&c,1,graph,true).processed,"duplicate reset is idempotent");
    c.step=366;
    f=offset.update(239,366,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].start && f.scrapes.size()==1 && f.scrapes[0].start,
          "contact after reset creates a fresh episode");
    CollisionScheduler fresh;
    c.step=2;
    check(equal(fresh.update(1,2,&c,1,graph),offset.update(240,2,&c,1,graph)),
          "physical clock rewind clears episodes even when race tick moves forward");
}

// Source-derived collision selectors and port episode policy are tested
// separately: these tests do not assert retail lifecycle/RNG parity.
void test_collision_graph(const std::filesystem::path* root) {
    using namespace nfsu::audio;
    CollisionDeck deck;
    unsigned previous=99;
    for (unsigned cycle=0;cycle<6;++cycle) {
        std::array<bool,10> seen{};
        for (unsigned i=0;i<10;++i) {
            const unsigned selected=deck.next(97*cycle+3*i+7);
            check(selected<10 && !seen[selected],"deck consumes every variant once per cycle");
            if (i==0) check(selected!=previous,"deck excludes previous cycle final choice at wrap");
            seen[selected]=true; previous=selected;
        }
    }
    CollisionContact c;
    c.impulse={1,0,0};
    check(world_collision_subtype(c)==4,"longitudinal subtype from greater branch");
    c.impulse={0,1,0};
    check(world_collision_subtype(c)==5,"transverse subtype from lesser branch");
    c.impulse={0,0,1};
    check(world_collision_subtype(c)==3,"upright vertical impulse");
    c.up.z=0; check(world_collision_subtype(c)==2,"vertical overturned branch");
    c.up.z=0.5; check(world_collision_subtype(c)==4,"vertical intermediate orientation");
    c.up.z=double(0.7070000171661377F);
    check(world_collision_subtype(c)==4,"vertical up boundary is strict");
    c.up.z=std::nextafter(c.up.z,1.0);
    check(world_collision_subtype(c)==3,"just above vertical up boundary");
    c.up={0,0,1}; c.impulse={1,0,0}; c.forward={0.707,0,0};
    check(world_collision_subtype(c)==5,"horizontal equality selects lesser family");
    c.forward.x=std::nextafter(0.707,1.0);
    check(world_collision_subtype(c)==4,"horizontal threshold greater selects family4");
    c.other_kind=4; check(world_collision_subtype(c)==4,"placed body uses world class");
    c.other_kind=1; check(world_collision_subtype(c)==-1,"smokeable not guessed as world");
    c.other_kind=0; check(world_collision_subtype(c)==-1,"car class not guessed as world");
    c.other_kind=3; c.impulse={};
    check(world_collision_subtype(c)==-1,"zero impulse cannot classify impact");
    check(collision_intensity(51,4)==127 && collision_intensity(39,5)==127 &&
          collision_intensity(3,3)==127,"subtype-scaled saturation boundaries");
    check(collision_intensity(51.0/2,4)==64 && collision_intensity(0,4)==0 &&
          collision_intensity(-1,4)==0 && collision_intensity(1,-1)==0,
          "intensity nearest-even and invalid input");
    check(collision_intensity(51.0/254,4)==0 && collision_intensity(3*51.0/254,4)==2,
          "half-integer intensity ties round to even");
    CollisionGraph graph;
    for (int i=0;i<128;++i) {
        graph.impact_gain[i]=std::int16_t(i*200);
        graph.impact_pitch[i]=std::int16_t(i*3-100);
        graph.scrape_gain[i]=std::int16_t(i*200);
        graph.scrape_pitch[i]=std::int16_t(2000+i*10);
    }
    graph.scrape_gain_targets.fill(32767); graph.scrape_pitch_targets.fill(400);
    c={}; c.step=1; c.pair=7; c.impulse={51,0,0}; c.slide_mps=20;
    CollisionScheduler scheduler;
    auto f=scheduler.update(1,c.step,&c,1,graph);
    check(f.processed && f.impacts.size()==1 && f.impacts[0].sample==40 &&
          f.impacts[0].start,"increment-before-use three-family sample binding");
    const auto volume=std::uint32_t(std::nearbyint(127.0*200*32767/32768));
    check(f.impacts[0].gain_q15==volume,"graph gain product, not impulse guessed gain");
    const int base_pitch=4096+127*3-100;
    check(f.impacts[0].pitch_q16 >= unsigned(base_pitch-150)*16 &&
          f.impacts[0].pitch_q16 <= unsigned(base_pitch+150)*16,
          "pitch lookup plus separate bounded jitter");
    check(f.scrapes.size()==1 && f.scrapes[0].start && f.scrapes[0].gain_q15>0,
          "scrape starts and ramp leaves zero");
    const double ramp_gain=32767.0*(1000.0/60)/370;
    const int ramp_integer=int(std::nearbyint(ramp_gain));
    const int scrape_base=int(std::nearbyint(64.0*200*20000/32768));
    check(f.scrapes[0].gain_q15==std::uint32_t(std::nearbyint(
          double(scrape_base)*ramp_integer*double(0x1.0002p-15F))),
          "scrape gain curves, configured gain and native ramp product");
    check(!scheduler.update(1,c.step,&c,1,graph).processed,"same tick ignored without silence command");
    c.step=2; f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.size()==1 && !f.impacts[0].start && f.impacts[0].sample==40,
          "same episode updates parameters without restarting PCM");
    // Another polygon is an independent port episode. Opposing impulses do
    // not cancel and one event cannot impose an invented shared cooldown.
    CollisionContact simultaneous[2]{c,c};
    simultaneous[0].step=5; simultaneous[0].pair=8;
    simultaneous[1].step=5; simultaneous[1].pair=9; simultaneous[1].impulse={-51,0,0};
    f=scheduler.update(3,5,simultaneous,2,graph);
    check(f.impacts.size()==2 && f.impacts[0].sample==41 && f.impacts[1].sample==39,
          "simultaneous opposite contacts select independent round-robin variants");
    f=scheduler.update(4,8,nullptr,0,graph);
    check(f.scrapes.empty(),"contact separation stops scrape commands");
    c.step=9; f=scheduler.update(5,c.step,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].start,"separate contact restarts immediately, no ten-tick cooldown");
    f=scheduler.update(6,12,&c,1,graph,true);
    check(f.impacts.empty() && f.scrapes.empty(),"reset discards contacts copied before reset");
    c.step=13; c.impulse={0,1,0}; f=scheduler.update(7,c.step,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].sample==36,"reset restores four-family counter");
    scheduler.clear(); c.step=1; c.impulse={25.5,0,0}; c.slide_mps=0;
    f=scheduler.update(1,c.step,&c,1,graph);
    const auto first_gain=f.impacts[0].gain_q15;
    c.step=2; c.impulse={-25.5,0,0};
    f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.size()==1 && !f.impacts[0].start && f.impacts[0].gain_q15>first_gain &&
          f.impacts[0].gain_q15==volume,"opposing impulses accumulate magnitudes within a pair");
    scheduler.clear(); c.step=1; c.impulse={0,0,3}; c.slide_mps=20;
    f=scheduler.update(1,c.step,&c,1,graph);
    check(f.impacts.size()==1 && f.impacts[0].sample==31,"landing family is independent");
    c.step=4; c.impulse={0,0,0}; c.slide_mps=20;
    f=scheduler.update(2,c.step,&c,1,graph);
    check(f.impacts.empty() && !f.scrapes.empty(),"slide observation need not manufacture an impulse");
    for (unsigned tick=3;tick<180;++tick) {
        CollisionContact steps[2]{c,c};
        steps[0].step=2*tick-1; steps[1].step=2*tick;
        f=scheduler.update(tick,2*tick,steps,2,graph);
        check(f.scrapes.size()==1 && !f.scrapes[0].start && f.scrapes[0].gain_q15<=20000,
              "sustained scrape bounded across oscillator/deck wraps");
    }
    std::array<std::uint8_t,nfsu::kVehicleTrackTelemetryRecordBytes> before{},after{};
    nfsu::VehicleTrackTelemetry telemetry;
    nfsu::encode_vehicle_track_telemetry(telemetry,before);
    telemetry.audio_contact_count=1; telemetry.audio_contacts[0].impulse={1,2,3};
    nfsu::encode_vehicle_track_telemetry(telemetry,after);
    check(before==after,"output contact copy excluded from replay bytes");
    if (!root) return;
    const auto read=[&](const char* name) {
        std::ifstream in(*root/name,std::ios::binary);
        check(bool(in),"retail collision bank available");
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),{});
    };
    auto impact=read("Sound/IG_GLOBAL/IMPACT_MB.abk");
    const auto scrape=read("Sound/IG_GLOBAL/SCRAPE_MB.abk");
    const auto retail=collision_graph(impact,scrape);
    // Expected entries are read independently at traced source offsets.
    const auto signed16=[](const auto& b,std::size_t at) {
        const unsigned u=unsigned(b[at]) | (unsigned(b[at+1])<<8);
        return u<32768 ? int(u) : int(u)-65536;
    };
    for (unsigned i=0;i<128;++i)
        check(retail.impact_gain[i]==signed16(impact,0xA4C+2*i) &&
              retail.impact_pitch[i]==signed16(impact,0xB5C+2*i) &&
              retail.scrape_gain[i]==signed16(scrape,0x39C+2*i) &&
              retail.scrape_pitch[i]==signed16(scrape,0x4AC+2*i),
              "collision curves bind to identified graph nodes");
    const auto bank=parse_abk(impact);
    for (unsigned sample=30;sample<=42;++sample)
        check(std::any_of(bank.samples.begin(),bank.samples.end(),
              [&](const auto& entry){return entry.index==sample && entry.frames>0;}),
              "world Player table selects existing nonempty PCM");
    impact[0]^=1;
    bool rejected=false;
    try { (void)collision_graph(impact,scrape); } catch(const std::runtime_error&) {rejected=true;}
    check(rejected,"unsupported bank must fail, not guess a collision mapping");
    std::cout << "race audio: collision graph checked\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: nfsu_race_audio_tests [RETAIL_ROOT]\n";
        return 1;
    }
    try {
        test_weights();
        test_playlist_order();
        test_missing_audio();
        test_live_collision_clock();
        test_collision_clock_contract();
        std::string argument_error;
        check(nfsu::check_arguments(argc, argv, 1, argument_error),
              argument_error.c_str());
        const std::filesystem::path root =
            argc == 2 ? nfsu::argument_path(argv[1]) : std::filesystem::path();
        test_retail_music_prefix(argc == 2 ? &root : nullptr);
        test_collision_graph(argc == 2 ? &root : nullptr);
        std::cout << "race audio tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "race audio tests: " << e.what() << '\n';
        return 1;
    }
}
