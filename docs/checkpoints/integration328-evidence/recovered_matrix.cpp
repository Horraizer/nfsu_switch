#include "nfsu/vehicle.h"
#include "nfsu/steering.h"
#include "nfsu/chase_camera.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
int main() {
 using namespace nfsu;
 const auto spec=stock_240sx_vehicle_spec(); const auto& h=spec.handling;
 const int top=kVehicleGearFirst+int(h.forward_gears)-1;
 const double final=h.final_drive_rear*h.rear_drive_fraction+h.final_drive_front*(1-h.rear_drive_fraction);
 const float predictor=float(h.redline_rpm*(6.28318530717958647692/60)*h.wheels[2].radius/(final*h.gear_ratios[top]));
 unsigned cases=0, spins=0, unknown=0;
 std::printf("initial,throttle,brake,max_speed,max_slip,min_up,low_speed_slip,sliding_steps,min_support,release_slip,release_speed\n");
 auto run=[&](double initial,double throttle,double brake,int steps) {
  VehicleModel model(spec); VehicleState car; VehiclePose pose;pose.speed=initial;
  if(!model.reset(car,pose)) { ++unknown;return; }
  SteeringState steering; unsigned shift_tick=0; bool shifted=false;
  double max_speed=0,max_slip=0,min_up=1,low_slip=0,release_slip=0,release_speed=0; unsigned slides=0,min_support=4;
  for(int k=0;k<steps+240;++k) {
   const auto q=car.orientation; const double fx=1-2*(q.y*q.y+q.z*q.z),fy=2*(q.x*q.y+q.w*q.z);
   SteeringContext c; c.raw=k<steps?1:0;c.predicate=true;c.seconds=float(1+double(k)/120);
   c.forward_speed=float(car.velocity.x*fx+car.velocity.y*fy+car.velocity.z*2*(q.x*q.z-q.w*q.y));
   c.speed=float(vehicle_sqrt(car.velocity.x*car.velocity.x+car.velocity.y*car.velocity.y+car.velocity.z*car.velocity.z));
   const auto delta=std::uint16_t(chase_angle(fx,fy)-chase_angle(car.velocity.x,car.velocity.y));
   c.heading_minus_velocity_deg=float(double(std::int16_t(delta))*360/65536);
   c.throttle=float(throttle);c.brake=float(brake);c.predicted_max_speed=predictor;
   c.shift_elapsed=shifted?float(double(k-shift_tick)/120):1;
   SteeringResult result; if(!steering_step(c,steering,result)) { ++unknown;return; }
   VehicleInput input;input.steer=k<steps?1:0;input.throttle=throttle;input.brake=brake;
   input.road_steering=true;input.road_angle=result.angle_deg*3.14159265358979323846/180;
   input.road_target=result.target_deg*3.14159265358979323846/180;
   const int previous=car.gear;VehicleTelemetry t;
   if(!model.step(car,input,VehicleFlatGround(),t)) { ++unknown;return; }
   if(car.gear!=previous) { shifted=true;shift_tick=k; }
   const double v=std::hypot(t.velocity.x,t.velocity.y);
   const double slip=v>3?std::abs(std::atan2(t.lateral_speed,t.forward_speed)*180/3.14159265358979323846):0;
   if(!std::isfinite(v)||!std::isfinite(slip)) { ++unknown;return; }
   if(k>=steps) { release_slip=slip;release_speed=v;continue; }
   max_speed=std::max(max_speed,v);max_slip=std::max(max_slip,slip);
   if(v<=15)low_slip=std::max(low_slip,slip);
   const auto next=car.orientation;min_up=std::min(min_up,1-2*(next.x*next.x+next.y*next.y));
   unsigned support=0;bool sliding=false;
   for(const auto& w:t.wheels){support+=w.contact;sliding|=w.sliding;}
   min_support=std::min(min_support,support);slides+=sliding;
  }
  ++cases;spins+=max_slip>20;
  std::printf("%.0f,%.1f,%.1f,%.9g,%.9g,%.9g,%.9g,%u,%u,%.9g,%.9g\n",initial,throttle,brake,max_speed,max_slip,min_up,low_slip,slides,min_support,release_slip,release_speed);
 };
 for(double throttle:{0.,.5,1.})for(double initial:{8.,10.,12.,15.,18.,20.,22.,25.,30.,35.})run(initial,throttle,0,600);
 for(double brake:{.5,1.})run(30,0,brake,480);
 std::printf("SUMMARY cases %u sideslip-above20 %u unknown %u\n",cases,spins,unknown);
 return cases==32 && !unknown?0:1;
}
