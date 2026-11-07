// Original contact_flow_native.py: three selected-mode query/response boundaries.
{auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};spec.handling.mass=1.22F;
spec.handling.inertia={1/2.1760244369506836F,1/.44518163800239563F,1/.4223935008049011F};
{VehicleState a,b;a.alternate_yaw_active=b.alternate_yaw_active=false;
a.velocity={10,0,0};b.velocity={-10,0,0};b.position={3.0F,0.0F,0.0F};
const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
ContactRecord record;std::uint32_t seed=12345;RaceContactContext context{record,seed,0};RacePairContact hit;
check(resolve_swept_race_pair(a,spec,box,b,spec,box,hit,pa,pb,&context)&&hit.applied,"native queued episode response");
const ContactVector impulse={-20.288230895996094F,-0.4266590476036072F,0.001714092679321766F};
for(unsigned k=0;k<3;++k)check(std::abs(hit.impulse[k]-impulse[k])<2e-5,"native episode impulse");
{const ContactVector pos={-0.8149998188018799F,0.0F,0.0F},vel={-0.9666106700897217F,-0.23062650859355927F,0.0009265365661121905F};
check(std::abs(a.position.x-pos[0])<2e-6&&std::abs(a.position.y-pos[1])<2e-6&&std::abs(a.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(a.velocity.x-vel[0])<2e-5&&std::abs(a.velocity.y-vel[1])<2e-5&&std::abs(a.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
{const ContactVector pos={3.81499981880188F,0.0F,0.0F},vel={0.9666106700897217F,0.23062650859355927F,-0.0009265365661121905F};
check(std::abs(b.position.x-pos[0])<2e-6&&std::abs(b.position.y-pos[1])<2e-6&&std::abs(b.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(b.velocity.x-vel[0])<2e-5&&std::abs(b.velocity.y-vel[1])<2e-5&&std::abs(b.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
}
{VehicleState a,b;a.alternate_yaw_active=b.alternate_yaw_active=false;
a.velocity={10,0,0};b.velocity={-10,0,0};b.position={3.0F,0.7F,0.0F};
const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
ContactRecord record;std::uint32_t seed=12345;RaceContactContext context{record,seed,0};RacePairContact hit;
check(resolve_swept_race_pair(a,spec,box,b,spec,box,hit,pa,pb,&context)&&hit.applied,"native queued episode response");
const ContactVector impulse={-20.61758804321289F,1.928514003753662F,0.0001620347611606121F};
for(unsigned k=0;k<3;++k)check(std::abs(hit.impulse[k]-impulse[k])<2e-5,"native episode impulse");
{const ContactVector pos={-0.6914271712303162F,-0.16133299469947815F,0.0F},vel={-1.1446421146392822F,1.0424399375915527F,8.758636249694973e-05F};
check(std::abs(a.position.x-pos[0])<2e-6&&std::abs(a.position.y-pos[1])<2e-6&&std::abs(a.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(a.velocity.x-vel[0])<2e-5&&std::abs(a.velocity.y-vel[1])<2e-5&&std::abs(a.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
{const ContactVector pos={3.691427230834961F,0.8613330125808716F,0.0F},vel={1.1446421146392822F,-1.0424399375915527F,-8.758636249694973e-05F};
check(std::abs(b.position.x-pos[0])<2e-6&&std::abs(b.position.y-pos[1])<2e-6&&std::abs(b.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(b.velocity.x-vel[0])<2e-5&&std::abs(b.velocity.y-vel[1])<2e-5&&std::abs(b.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
}
{VehicleState a,b;a.alternate_yaw_active=b.alternate_yaw_active=false;
a.velocity={10,0,0};b.velocity={-10,0,0};b.position={3.0F,0.7F,0.2F};
const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
ContactRecord record;std::uint32_t seed=12345;RaceContactContext context{record,seed,0};RacePairContact hit;
check(resolve_swept_race_pair(a,spec,box,b,spec,box,hit,pa,pb,&context)&&hit.applied,"native queued episode response");
const ContactVector impulse={-13.471720695495605F,-1.1415719985961914F,-2.919262647628784F};
for(unsigned k=0;k<3;++k)check(std::abs(hit.impulse[k]-impulse[k])<2e-5,"native episode impulse");
{const ContactVector pos={-0.6899745464324951F,-0.16099406778812408F,-0.04599830508232117F},vel={2.717988967895508F,-0.617065966129303F,-1.5779798030853271F};
check(std::abs(a.position.x-pos[0])<2e-6&&std::abs(a.position.y-pos[1])<2e-6&&std::abs(a.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(a.velocity.x-vel[0])<2e-5&&std::abs(a.velocity.y-vel[1])<2e-5&&std::abs(a.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
{const ContactVector pos={3.689974546432495F,0.8609940409660339F,0.2459983080625534F},vel={-2.717988967895508F,0.617065966129303F,1.5779798030853271F};
check(std::abs(b.position.x-pos[0])<2e-6&&std::abs(b.position.y-pos[1])<2e-6&&std::abs(b.position.z-pos[2])<2e-6,"native episode separated position");
check(std::abs(b.velocity.x-vel[0])<2e-5&&std::abs(b.velocity.y-vel[1])<2e-5&&std::abs(b.velocity.z-vel[2])<2e-5,"native episode linear velocity");}
}
}
