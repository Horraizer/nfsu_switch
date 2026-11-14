#include "nfsu/race_session.h"
#include "nfsu/intro_grid.h"
#include "nfsu/player_powertrain.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
namespace nfsu {
struct RaceWorldContactProbe {
static int run(const char* root) {
 int failures=0;
 //The last actor in a full traffic roster must retain the same original
 //583DA0 magnitude/stop behavior as the first actor, including shared records.
 for(unsigned other:{0U,1+kRaceRivalCapacity+kRaceTrafficCapacity-1}){
  RaceTrafficActor actor;ContactRecord record;record.magnitude_sum=30;
  RaceTraffic::contact(actor,other,{1,0,0},1,&record);
  if(actor.crash_stop||!actor.pair_history[other].valid){std::cerr<<"traffic full-roster contact boundary";return 20;}
  record.magnitude_sum=31;RaceTraffic::contact(actor,other,{1,0,0},2,&record);
  if(!actor.crash_stop||actor.pair_history[other].magnitude!=31){std::cerr<<"traffic full-roster shared magnitude lost";return 21;}
 }

 // The queued job commits both corrected origins before the first refresh.
 // Smokeable publishes its public position from the rigid body at that point.
 {
  const auto spec=stock_240sx_vehicle_spec();
  const RaceContactBox box{{-1,-1,-1},{1,1,1},{},{}};
  VehicleState car;vehicle_import_rigid(spec.handling,car);
  RaceProp prop;prop.spec=spec;prop.state.position={2,0,.5};
  vehicle_import_rigid(spec.handling,prop.state);
  const auto car_history=race_contact_pose(car);
  const auto prop_history=race_contact_pose(prop.state);
  RaceContactJob job;job.admitted=true;job.correction={.125,.25,.125};
  job.query.witness_a={1,0,0};job.query.witness_b={2,0,0};
  RaceProps props;
  struct Refresh {RaceProps& props;RaceProp& prop;};
  Refresh refresh{props,prop};
  ContactRecord record;std::uint32_t seed=1;
  RaceContactContext context{record,seed,1};
  context.refresh_owner=&refresh;
  context.refresh=[](void* owner,unsigned slot,VehicleState& state) {
   auto& fixture=*static_cast<Refresh*>(owner);
   return slot==0||fixture.props.refresh_contact(fixture.prop,state);
  };
  RacePairContact result;
  if(!resolve_swept_race_prop(car,spec,box,prop.state,spec,box,prop.impact,
      car_history,prop_history,job,context,result)||
     prop.state.rigid_body.position!=ContactVector{2.125F,.25F,.625F}||
     result.resume_b.position.x!=2.125||result.resume_b.position.y!=.25||result.resume_b.position.z!=.625) {
   std::cerr<<"prop lost corrected position during contact refresh\n";++failures;
  }
 }

 struct Job {unsigned a,b;float remaining;ContactVector point;};
 struct Pin {float wall;unsigned actor;std::array<ContactVector,2> inertia;unsigned count;
  std::array<Job,4> jobs;std::array<std::array<float,10>,2> states;bool reverse=false;bool floor=false;};
 static const Pin pins[]={
 #include "race_world_contact_pins.inc"
 #include "race_ground_contact_pins.inc"
 };
 struct CacheCase {ContactVector position,velocity;std::array<float,4> bounds;std::vector<std::size_t> polygons;};
 static const CacheCase cache_cases[]={
 #include "race_polygon_cache_pins.inc"
 };
 std::vector<unsigned char> track_image;
 const auto cache_world=build_track_contact_world(load_track_collision(std::filesystem::path(root)/"Tracks/TRACKB1003.lzc",track_image));
 RaceTrackPolygonCache polygon_cache;
 for(const auto& expected:cache_cases) {
  RaceContactPose pose;pose.position={expected.position[0],expected.position[1],expected.position[2]};
  pose.velocity={expected.velocity[0],expected.velocity[1],expected.velocity[2]};
  if(!update_race_track_polygon_cache(cache_world,pose,polygon_cache)||polygon_cache.bounds!=expected.bounds||polygon_cache.polygons!=expected.polygons){
   std::cerr<<"original polygon cache mismatch\n";++failures;
  }
 }
 IntroGrid grid;std::string error;if(!load_intro_grid(root,grid,error)){std::cerr<<error;return 3;}
 // Native queue fixtures use full57F2B0 with explicit upgrades.
 // They do not override it with the raw575960 table result.
 // Rebuild through the same retail setup producer, never from expected inertia.
 for(auto& actor:grid.actors) {
  VehicleUpgradeLevels levels;
  if(actor.config.role==Event0Role::human)levels={1,1,1,1,1,1,1,1,1,1,1};
  if(!load_race_vehicle_spec(std::filesystem::path(root)/"Global/GLOBALB.BUN",
       actor.assets.car_type_name,levels,actor.placement_spec,error)){std::cerr<<error;return 14;}
 }

 // Original469810 fixture:53 coincident walls, reversed cache order.
 //The first48 admissions are sorted afterwards; sorting first is different.
 auto crowded=cache_world;crowded.collision.polygons.resize(53);
 RaceTrackPolygonCache dense;dense.valid=true;
 for(unsigned i=0;i<53;++i) {
  auto& poly=crowded.collision.polygons[i];poly.index=i;
  const float points[4][3]={{0,-8,-8},{0,-8,8},{0,8,8},{0,8,-8}};
  for(unsigned j=0;j<4;++j)for(unsigned k=0;k<3;++k)poly.corner[j][k]=points[j][k];
  dense.polygons.push_back(52-i);
 }
 std::vector<std::size_t> admitted;
 VehicleState centered;RaceTrackBodyBounds bounds;bounds.radius=3;bounds.above=bounds.below=1;
 if(!race_track_candidates(crowded,centered,bounds,dense,{},admitted)||admitted.size()!=48)++failures;
 else for(unsigned i=0;i<48;++i)if(admitted[i]!=i+5){++failures;break;}
 auto& player=grid.actors[1];
 std::vector<IntroGridActor*> selected{&grid.actors[0],&grid.actors[2],&grid.actors[3]};
 // Native fixtures name their input models in race_world_contact_native.py.
 // Live weighted selection order is not the fixture's actor identity.
 for(const auto* name:{"4DR_SEDAN02","TAXI02","PANELVAN","PICKUP","SUV","PARCELVAN"}) {
  const auto found=std::find_if(grid.traffic.begin(),grid.traffic.end(),
   [name](const auto& actor){return actor.assets.car_type_name==name;});
  if(found==grid.traffic.end()){std::cerr<<"native contact fixture model absent: "<<name;return 19;}
  selected.push_back(&*found);
 }
 // Same pose/speed inputs for original 420380 and the port world queue.
 struct TrafficCrash {std::uint32_t seed,after;double y,vy,bvx,vx,vz,pitch,traffic_vx,traffic_pitch;};
 const TrafficCrash crash_cases[]{
  {808147100U,3358020166U,0.7,0.0,-45.0,-2.398158311843872,1.7855316400527954,-2.117387533187866,2.3981547355651855,1.9662234783172607},
  {728455324U,3357639238U,0.7,0.0,-45.0,-2.398158311843872,1.7855316400527954,-2.117387533187866,2.3981547355651855,-2.185351610183716},
  {808147100U,426599326U,0.3,0.0,4.0,22.680089950561523,1.3608112335205078,-1.7356265783309937,26.316696166992188,1.597713589668274},
  {808147100U,426599326U,1.4,7.0,4.0,22.85784339904785,4.622661113739014,-3.843904733657837,26.638349533081055,0.7406395077705383},
 };
 for(const auto& expected:pins)for(unsigned variant=0;variant<2+std::size(crash_cases);++variant){
 const bool mixed=variant==1,strong_traffic=variant>=2;
 const auto& crash=crash_cases[strong_traffic?variant-2:0];
 if(mixed&&(expected.wall>=0||expected.actor!=1||expected.floor||expected.reverse))continue;
 if(strong_traffic&&(expected.wall>=0||expected.actor!=4||expected.floor||expected.reverse))continue;
 const float wall=strong_traffic?-8.F:expected.wall;const unsigned index=expected.actor-1;
 auto& rival=*selected[index];
 // Verify the actual loaded runtime producer before response/continuation.
 // Never replace a port input with the native answer to hide a setup mismatch.
 if(player.placement_spec.handling.inertia!=expected.inertia[0] ||
    rival.placement_spec.handling.inertia!=expected.inertia[1]) {
  std::cerr<<"runtime inertia producer "<<expected.actor<<"\n";++failures;
 }
 VehicleTrackCourse course;if(!build_vehicle_proving_ground(course,error))return 4;
 // Traffic owns actual road metadata; the collision world below stays synthetic.
 course.name="TRACKB1003.lzc";
 VehicleTrackQuad quad;
 float corners[4][3]={{wall,-8,-8},{wall,-8,8},{wall,8,8},{wall,8,-8}};
 for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)quad.corner[i][k]=corners[i][k];
 // Build the actual wall's quadtree, including negative-X cells. Reusing
 // the proving ground's positive-X cells silently omitted reversed actors.
 VehicleTrackQuad floor;const float fc[4][3]={{-16,-16,-.375F},{16,-16,-.375F},{16,16,-.375F},{-16,16,-.375F}};
 for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)floor.corner[i][k]=fc[i][k];
 const auto image=build_vehicle_track_collision_image(expected.floor?std::vector<VehicleTrackQuad>{quad,floor}:std::vector<VehicleTrackQuad>{quad},error);
 if(image.empty()){std::cerr<<error;return 10;}
 auto report=parse_track_collision(image.data(),image.size(),0);
 course.world=build_track_contact_world(report);
 RaceSession world(course,player.placement_spec,RaceSteering::baseline,player.hull);
 world.rivals_.push_back(std::make_unique<RaceSession>(course,rival.placement_spec,RaceSteering::baseline,rival.hull,RacePresentation::none));
 auto& other=*world.rivals_[0];float rating=0,low_slip=1;
 if(expected.floor&&!ai_setup_performance(rival.placement_spec.handling,rating,low_slip))return 13;
 if(!other.configure_ai(low_slip,error))return 5;
 const bool traffic=index>=3;
 VehicleState a,b;
 if(!world.model_.reset(a,course.start)||!other.model_.reset(b,course.start))return 6;
 const double sign=expected.reverse?-1:1;
 a.position={};b.position={4*sign,.7,.2};a.orientation={};b.orientation={};
 a.rigid_body_ready=b.rigid_body_ready=false;a.aero={};a.aero_ready=true;
 a.alternate_yaw_active=b.alternate_yaw_active=false;a.velocity={30*sign,0,0};b.velocity={-10*sign,0,0};
 if(strong_traffic){
  a.velocity={45,crash.vy,0};b.velocity={crash.bvx,0,0};b.position={3.8,crash.y,.2};
 }
 if(expected.floor) {
  // Same initialized neutral/native ordinary-grip inputs as the native
  // grounded fixture. This does not assert original reset or race policy.
  struct ClearGround final:VehicleGround {
   VehicleGroundHit cast(const VehicleVec3&,const VehicleVec3&,double)const override{return {};}
  } clear;
  VehicleInput initial;initial.road_steering=true;initial.road_angle_word_valid=true;
  VehicleTelemetry initialized;a.aero_ready=false;a.alternate_yaw_active=true;
  a.gear=a.published_gear=a.transmission.requested_gear=kVehicleGearNeutral;
  if(!world.model_.step(a,initial,clear,initialized,0,0,false,true,true))return 12;
  a.published_support=0;for(const auto& w:a.wheels)a.published_support+=w.contact?1U:0U;
  a.owner_speeds=player_publish_owner_speeds(a.velocity,{1,0,0});
  a.owner_slip_angle=player_owner_slip_angle(a.velocity,{1,0,0});
  world.vehicle_.last_input_=initial;
 }
 a.simulation_seconds=b.simulation_seconds=double(1.F/60.F);
 std::array<RaceContactPose,4> history{};history[0]=race_contact_pose(a);history[1]=race_contact_pose(b);
 history[0].time=history[1].time=0;history[0].position.x=-.5*sign;history[1].position.x=double(float((4.+1./6)*sign));
 if(strong_traffic){
  history[0].position={-.75,-crash.vy/60,0};history[1].position.x=3.8-crash.bvx/60;
 }
 if(!world.vehicle_.start(a)||!other.vehicle_.start(b))return 7;
 world.vehicle_.defer_body_contacts(true);other.vehicle_.defer_body_contacts(true);
 const VehicleState* opposite=nullptr;
 if(traffic) {
  world.traffic_=std::make_unique<RaceTraffic>();
  if(!world.traffic_->load(root,course,{{rival.placement_spec,rival.hull,rival.request.car_type_name}},error)) {
   std::cerr<<error;return 9;
  }
  // This synthetic fixture bypasses set_traffic; prepare its owner caches too.
  world.contact_traffic_epochs_.resize(1);world.near_pass_cache_.resize(4);
  world.traffic_->defer_body_contacts(true);
  auto& t=world.traffic_->contact_actors()[0];t.car=b;t.active=true;
  t.previous_pose=history[1];t.previous_valid=true;opposite=&t.car;
 } else opposite=&other.car();
 other.vehicle_.last_input_.road_steering=true;other.vehicle_.last_input_.road_angle_word_valid=true;
 world.timing_.now_ticks=1084227584;other.started_=true;
 if(traffic)world.rivals_.clear();
 struct CrashRoad final:CrashCameraRoadSource {
  mutable VehicleVec3 position{},velocity{};
  bool prepare(const VehicleState& car,float,AiRoadChain& chain,int& category)const override {
   position=car.position;velocity=car.velocity;category=0;chain={};chain.count=12;
   for(unsigned i=0;i<chain.count;++i) {
    auto& p=chain.cells[i];p.distance=float(i)*10.F;
    p.point.position[0]=float(position.x)-1.F+p.distance;p.point.position[1]=float(position.y);
    p.point.forward[0]=1;p.point.height=float(position.z);
    p.point.unknown_24=p.point.unknown_28=10;p.lower[0]=-10;p.upper[0]=10;
   }
   return true;
  }
  bool progress_at(const AiDriverPoint&,float& progress)const override {progress=90;return true;}
 } crash_road;
 auto live_seed=crash.seed;
 if(strong_traffic) {
  world.world_random_=&live_seed;
  if(!world.set_crash_camera(&crash_road,kCrashCameraDefaultIntensity,1003,error))return 22;
  world.timing_.stage=RaceStage::racing;world.timing_.outcome=RaceOutcome::running;
 }
 world.started_=true;
 if(mixed) {
  VehicleHull hull;
  for(float x:{-.5F,.5F})for(float y:{-.5F,.5F})for(float z:{-.5F,.5F})
   hull.points.push_back({x,y,z});
  RaceProp prop;prop.spec=stock_240sx_vehicle_spec();prop.spec.handling.centre_of_mass={};
  prop.spec.handling.mass=.1F;prop.spec.handling.inertia={.02F,.02F,.02F};
  prop.points=hull.points;prop.box=race_contact_box(hull);
  prop.bounds=race_track_body_bounds(prop.spec,prop.box);
  prop.state.position={-.5,-1.28,0};prop.state.velocity={0,16,0};prop.state.orientation={1,0,0,0};
  prop.state.simulation_seconds=a.simulation_seconds;
  vehicle_import_rigid(prop.spec.handling,prop.state);
  prop.history=race_contact_pose(prop.state);prop.history.time=0;prop.history.position.y-=16.F/60;
  prop.body_id=10;prop.world_slot=0;prop.awake=prop.moved=true;
  world.props_=std::make_unique<RaceProps>();
  world.props_->objects_.push_back(std::move(prop));world.props_->world_frame_ready_=true;
  world.world_shape_ids_.assign(1,true);
 }
 std::array<std::uint64_t,4> resets{};std::vector<RaceQueuedContact> trace;
 if(!world.resolve_world_contacts(history,resets,error,nullptr,nullptr,&trace)){std::cerr<<error;return 8;}
 if(strong_traffic) {
  const auto& hit_car=world.car();
  const auto& sedan=world.traffic_->actors()[0];
  // Original 420380, same retail cars and authored pose, 007F arithmetic.
  // 42C4B0 is a supplied boundary in the native witness; the port supplies
  // a 110 m test strip to exercise camera admission as well as the solver.
  if(hit_car.collision_state!=1||sedan.car.collision_state!=1||
     world.crash_camera_event()!=6||!world.crash_windows_damaged()||!sedan.crash_stop||
     crash_road.position.x!=a.position.x||crash_road.velocity.x!=a.velocity.x||
     std::abs(hit_car.velocity.x-crash.vx)>.005||
     std::abs(hit_car.velocity.z-crash.vz)>.005||
     std::abs(hit_car.angular_velocity.y-crash.pitch)>.005||
     std::abs(sedan.car.velocity.x-crash.traffic_vx)>.005||
     std::abs(sedan.car.angular_velocity.y-crash.traffic_pitch)>.005||
     live_seed!=crash.after||world.contact_seed_!=12345U) {
   std::cerr<<"traffic contact did not publish the original crash boundary"
            <<" state "<<hit_car.collision_state<<'/'<<sedan.car.collision_state
            <<" camera "<<world.crash_camera_event()<<" published "<<crash_road.velocity.x
            <<" hit vx/vz "<<hit_car.velocity.x<<'/'<<hit_car.velocity.z
            <<" pitch "<<hit_car.angular_velocity.y<<" traffic vx/pitch "<<sedan.car.velocity.x<<'/'<<sedan.car.angular_velocity.y<<'\n';
   ++failures;
  }
  // Original 42A530 on a supplied recovery timeline, after the real queued
  // impact above. Retiring the record must not begin the support timer.
  auto& car=world.vehicle_;
  auto input=car.last_input_;input.throttle=1;input.world_clock_valid=true;
  VehicleTrackTelemetry recovery;
  const auto step=[&](std::int32_t tick,unsigned support) {
   car.state_.published_support=support;input.world_clock=tick;
   return car.step(input,recovery,0,kVehicleStepSeconds,true,1,true);
  };
  car.publish_contact_count(0);
  if(car.state_.collision_support_since!=0||
     !step(119900,4)||car.state_.collision_support_since==0||
     !step(119950,3)||car.state_.collision_support_since!=0||
     !step(120000,4)||!step(120399,4)||car.state_.collision_state!=1||
     !step(120400,4)||car.state_.collision_state!=0||!world.crash_windows_damaged()) {
   std::cerr<<"original crash recovery timeline or persistent window damage mismatch\n";++failures;
  }
  continue;
 }
 if(mixed) {
  const auto none=std::numeric_limits<std::size_t>::max();
  std::array<std::size_t,3> physical{{none,none,none}},audio{{none,none,none}};
  for(std::size_t i=0;i<trace.size();++i) {
   const auto& hit=trace[i];
   if(hit.a!=0&&hit.b!=0)continue;
   if(hit.polygon!=none)physical[0]=std::min(physical[0],i);
   else if(hit.a==1||hit.b==1)physical[1]=std::min(physical[1],i);
   else if(hit.prop==0)physical[2]=std::min(physical[2],i);
  }
  const auto& road=world.track_telemetry_.back();
  const auto& cars=world.pair_contacts();
  const auto& props=world.prop_contacts();
  if(road.audio_contact_count)audio[0]=road.audio_contacts[0].audio_order;
  for(std::size_t i=0;i<cars.count;++i) {
   const auto& pair=cars.pairs[i];
   if((pair.first==0&&pair.second==1)||(pair.first==1&&pair.second==0))
    audio[1]=std::min(audio[1],std::size_t(pair.audio_order));
  }
  if(props.count)audio[2]=props.hits[0].audio_order;
  bool valid=true;
  for(unsigned a=0;a<3;++a) {
   valid&=physical[a]!=none&&audio[a]!=none&&audio[a]!=0;
   for(unsigned b=a+1;b<3;++b)
    valid&=audio[a]!=audio[b]&&(physical[a]<physical[b])==(audio[a]<audio[b]);
  }
  if(!valid){
   std::cerr<<"mixed player collision or audio order mismatch\n";
   for(const auto& hit:trace)
    std::cerr<<"mixed job "<<hit.a<<' '<<hit.b<<" t="<<hit.remaining<<'\n';
   ++failures;
  }
  continue;
 }
 if(trace.size()!=expected.count){std::cerr<<"queue size "<<expected.actor<<" "<<wall<<"\n";++failures;
 for(const auto& q:trace)std::cerr<<"actual job "<<q.a<<" "<<q.b<<" "<<std::setprecision(17)<<q.remaining<<" point "<<q.point.x<<","<<q.point.y<<","<<q.point.z<<" position "<<q.position[0].x<<","<<q.position[0].y<<","<<q.position[0].z<<" velocity "<<q.velocity[0].x<<","<<q.velocity[0].y<<","<<q.velocity[0].z<<"\n";
 continue;}
 for(unsigned j=0;j<trace.size();++j){
  const auto& actual=trace[j];const auto& want=expected.jobs[j];
  const auto actor_id=[&](unsigned id){return traffic&&id==1?4U:id;};
  if(actual.a!=actor_id(want.a)||actual.b!=actor_id(want.b)||actual.remaining!=want.remaining||
     float(actual.point.x)!=want.point[0]||float(actual.point.y)!=want.point[1]||float(actual.point.z)!=want.point[2]){
   std::cerr<<"queue geometry "<<expected.actor<<" "<<wall<<" "<<j<<"\n";++failures;
  }
 }
 unsigned body=0;
 for(const auto* state:{&world.car(),static_cast<const VehicleState*>(opposite)}){
  const std::array<float,10> actual{{float(state->position.x),float(state->position.y),float(state->position.z),
   float(state->velocity.x),float(state->velocity.y),float(state->velocity.z),float(state->angular_velocity.x),
   float(state->angular_velocity.y),float(state->angular_velocity.z),float(state->collision_state)}};
  for(unsigned k=0;k<actual.size();++k)if(actual[k]!=expected.states[body][k]){
   std::cerr<<"state "<<expected.actor<<" "<<wall<<" "<<body<<" "<<k<<" got "<<std::setprecision(17)<<actual[k]<<" expected "<<expected.states[body][k]<<"\n";++failures;
  }++body;
 }
 if(traffic&&!expected.floor&&expected.actor==4&&!expected.reverse) {
  auto& actor=world.traffic_->contact_actors()[0];actor.crash_stop=true;
  const auto published=actor.car;auto want=actor.driver;auto legacy=actor.driver;
  AiDriverForwardInput in;in.category=2;in.desired_speed=0;
  in.position={float(published.position.x),float(published.position.y),float(published.position.z)};
  in.velocity={float(published.velocity.x),float(published.velocity.y),float(published.velocity.z)};
  in.forward_speed=published.owner_speeds.forward;in.absolute_speed=published.owner_speeds.magnitude;
  const auto& axis=published.rigid_body.axes[0];in.heading=track_angle(axis[0],axis[1]);
  in.rear_drive_fraction=actor.setup.spec.handling.rear_drive_fraction;
  for(unsigned k=0;k<4;++k){in.contact[k]=published.wheels[k].contact;in.wheel_slip[k]=published.wheels[k].prepared_slip;}
  if(!ai_driver_traffic_impact_step(in,1.F/60,want,actor.steering_scale))return 15;
  auto old=in;old.contact.fill(true);
  if(!ai_driver_traffic_step(old,1.F/60,legacy,actor.steering_scale))return 16;
  if(want.pedals.brake==legacy.pedals.brake&&want.steering.word==legacy.steering.word){
   std::cerr<<"traffic binding fixture did not distinguish the old driver\n";++failures;
  }
  std::array<const VehicleState*,4> racers{{&world.car(),nullptr,nullptr,nullptr}};
  const std::array<const RaceContactBox*,4> shapes{{&world.contact_box_}};
  if(!world.traffic_->advance(racers,1,error,-1,1.F/60.F,{},shapes)){std::cerr<<error;return 17;}
  const auto& actual=world.traffic_->actors()[0].driver;
  if(actual.steering.word!=want.steering.word||actual.pedals.throttle!=want.pedals.throttle||
     actual.pedals.brake!=want.pedals.brake||actual.gear_request!=want.gear_request){
   std::cerr<<"live traffic driver lost selector/support/gear binding\n";++failures;
  }
  // A recycled traffic actor's local component clock starts at zero, while
  //42A530 reads the shared world timer. Mixing those clocks suppresses or
  //prematurely ends the recovery interval after the record has expired.
  auto& recovered=world.traffic_->contact_actors()[0].car;
  recovered.collision_state=1;recovered.collision_body_flag=0;
  recovered.published_support=4;recovered.collision_support_since=119.8F;
  if(!world.traffic_->advance(racers,2,error,480000,1.F/60.F,{},shapes)){std::cerr<<error;return 18;}
  if(world.traffic_->actors()[0].car.collision_state!=0){
   std::cerr<<"traffic recovery used component age instead of world clock\n";++failures;
  }
  actor.crash_stop=false;actor.target_speed=2;
  actor.car.collision_body_flag=1;actor.car.owner_speeds.magnitude=.5F;
  for(auto& wheel:actor.car.wheels)wheel.contact=true;
  if(!world.traffic_->prepare_frame(racers,3,error,480067,1.F/60.F,{},shapes)){std::cerr<<error;return 23;}
  if(!actor.crash_stop||actor.driver.pedals.throttle!=0||actor.driver.pedals.brake!=1){
   std::cerr<<"blocked traffic did not brake with a live contact and positive speed target\n";++failures;
  }
 }
 }
 //420310 snapshots the X-sorted world, not rivals->human->traffic.
 //No nearby body pair: the refresh interval must come from its last rigid car.
 for(bool ai_last:{false,true}) {
  VehicleTrackCourse flat;if(!build_vehicle_proving_ground(flat,error))return 30;
  auto human_pose=flat.start,ai_pose=flat.start;
  human_pose.ground.x=ai_last?64:128;human_pose.ground.y=16;
  ai_pose.ground.x=ai_last?128:64;ai_pose.ground.y=16;
  RaceSession frame(flat,player.placement_spec,RaceSteering::baseline,player.hull);
  RaceRivalSetup setup;setup.spec=selected[0]->placement_spec;setup.pose=ai_pose;
  setup.hull=selected[0]->hull;setup.name="phase-native-rival";
  if(!frame.set_planned_rivals({setup},{1,1,1},error)||!frame.start(error,&human_pose)) {
   std::cerr<<"FAIL world phase setup "<<error<<'\n';return 31;
  }
  auto& ai=*frame.rivals_[0];
  // Isolate the initial physical dispatch from later floor-contact rewinds.
  human_pose.ground.z=ai_pose.ground.z=50;
  VehicleState human_air,ai_air;
  if(!frame.model_.reset(human_air,human_pose)||!ai.model_.reset(ai_air,ai_pose)||
     !frame.vehicle_.start(human_air)||!ai.vehicle_.start(ai_air))return 33;
  for(auto* racer:{&frame,&ai}) {
   racer->timing_.stage=RaceStage::racing;racer->timing_.race_start_valid=true;
   racer->timing_.race_start_ticks=racer->timing_.now_ticks;racer->timing_.go_valid=true;
  }
  RaceRivalPlans plans;plans.count=1;plans.step=frame.timing_.step;
  auto& plan=plans.cars[0];plan.enabled=true;plan.desired_speed=20;plan.route_distance=100;
  plan.path.count=2;plan.path.points[0]={float(ai_pose.ground.x+100),16,0};
  plan.path.points[1]={float(ai_pose.ground.x+200),16,0};
  GamepadFrame pad;pad.pads[0].connected=true;
  if(!frame.advance_planned(pad,plans,error)) {
   std::cerr<<"FAIL physical phase execution "<<error<<'\n';return 32;
  }
  const float expected=ai_last?float(kDriveFrameSeconds)/2:float(kDriveFrameSeconds/4);
  if(frame.contact_component_interval_!=expected) {
   std::cerr<<"FAIL last original component interval ai_last="<<ai_last<<" interval="<<frame.contact_component_interval_<<" expected="<<expected<<'\n';++failures;
  }
 }
 std::cout<<"world collision tests: "<<failures<<" failures\n";return failures?1:0;
}
};
}
int main(int argc,char** argv){return argc==2?nfsu::RaceWorldContactProbe::run(argv[1]):2;}
