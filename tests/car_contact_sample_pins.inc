// Original468FE0/4692F0, from contact_search_native.py.
{auto spec=model.spec();spec.handling.mass=1;
{VehicleState before,end,sample;before.position={1,2,3};before.velocity={3,-1,7};
end.position={2,4,5};end.velocity={11,-3,2};end.simulation_seconds=0.016F;
vehicle_import_rigid(spec.handling,end);end.rigid_body.axes={{{{0,1,0}},{{-1,0,0}},{{0,0,1}}}};
check(sample_race_contact(sample,spec,race_contact_pose(before),end,0.008F),"native matrix sampler succeeds");
const ContactVector position={1.5F,3.0F,4.0F},velocity={7.0F,-2.0F,4.5F};
const PlayerRigidMatrix basis={{{0.5F,0.5F,0.0F},{-0.5F,0.5F,0.0F},{0.0F,0.0F,1.0F}}};
check(sample.position.x==position[0]&&sample.position.y==position[1]&&sample.position.z==position[2],"native sampled position exact");
check(sample.velocity.x==velocity[0]&&sample.velocity.y==velocity[1]&&sample.velocity.z==velocity[2],"native sampled velocity exact");
check(sample.rigid_body.axes==basis,"native sampled non-normalized matrix exact");}
{VehicleState before,end,sample;before.position={1,2,3};before.velocity={3,-1,7};
end.position={2,4,5};end.velocity={11,-3,2};end.simulation_seconds=0.016F;
vehicle_import_rigid(spec.handling,end);end.rigid_body.axes={{{{0,1,0}},{{-1,0,0}},{{0,0,1}}}};
check(sample_race_contact(sample,spec,race_contact_pose(before),end,0.004F),"native matrix sampler succeeds");
const ContactVector position={1.75F,3.5F,4.5F},velocity={9.0F,-2.5F,3.25F};
const PlayerRigidMatrix basis={{{0.25F,0.75F,0.0F},{-0.75F,0.25F,0.0F},{0.0F,0.0F,1.0F}}};
check(sample.position.x==position[0]&&sample.position.y==position[1]&&sample.position.z==position[2],"native sampled position exact");
check(sample.velocity.x==velocity[0]&&sample.velocity.y==velocity[1]&&sample.velocity.z==velocity[2],"native sampled velocity exact");
check(sample.rigid_body.axes==basis,"native sampled non-normalized matrix exact");}
{VehicleState before,end,sample;before.position={1,2,3};before.velocity={3,-1,7};
end.position={2,4,5};end.velocity={11,-3,2};end.simulation_seconds=0.008F;
vehicle_import_rigid(spec.handling,end);end.rigid_body.axes={{{{0,1,0}},{{-1,0,0}},{{0,0,1}}}};
check(sample_race_contact(sample,spec,race_contact_pose(before),end,0.006F),"native matrix sampler succeeds");
const ContactVector position={1.25F,2.5F,3.5F},velocity={5.0F,-1.5F,5.75F};
const PlayerRigidMatrix basis={{{0.75F,0.2500000298023224F,0.0F},{-0.2500000298023224F,0.75F,0.0F},{0.0F,0.0F,1.0F}}};
check(sample.position.x==position[0]&&sample.position.y==position[1]&&sample.position.z==position[2],"native sampled position exact");
check(sample.velocity.x==velocity[0]&&sample.velocity.y==velocity[1]&&sample.velocity.z==velocity[2],"native sampled velocity exact");
check(sample.rigid_body.axes==basis,"native sampled non-normalized matrix exact");}
{VehicleState before,end,sample;before.position={1,2,3};before.velocity={3,-1,7};
end.position={2,4,5};end.velocity={11,-3,2};end.simulation_seconds=0.0F;
vehicle_import_rigid(spec.handling,end);end.rigid_body.axes={{{{0,1,0}},{{-1,0,0}},{{0,0,1}}}};
check(sample_race_contact(sample,spec,race_contact_pose(before),end,0.0F),"native matrix sampler succeeds");
const ContactVector position={2.0F,4.0F,5.0F},velocity={11.0F,-3.0F,2.0F};
const PlayerRigidMatrix basis={{{0.0F,1.0F,0.0F},{-1.0F,0.0F,0.0F},{0.0F,0.0F,1.0F}}};
check(sample.position.x==position[0]&&sample.position.y==position[1]&&sample.position.z==position[2],"native sampled position exact");
check(sample.velocity.x==velocity[0]&&sample.velocity.y==velocity[1]&&sample.velocity.z==velocity[2],"native sampled velocity exact");
check(sample.rigid_body.axes==basis,"native sampled non-normalized matrix exact");}
{VehicleState before,end,sample;before.position={1,2,3};before.velocity={3,-1,7};
end.position={2,4,5};end.velocity={11,-3,2};end.simulation_seconds=5e-07F;
vehicle_import_rigid(spec.handling,end);end.rigid_body.axes={{{{0,1,0}},{{-1,0,0}},{{0,0,1}}}};
check(sample_race_contact(sample,spec,race_contact_pose(before),end,2.5e-07F),"native matrix sampler succeeds");
const ContactVector position={2.0F,4.0F,5.0F},velocity={11.0F,-3.0F,2.0F};
const PlayerRigidMatrix basis={{{0.0F,1.0F,0.0F},{-1.0F,0.0F,0.0F},{0.0F,0.0F,1.0F}}};
check(sample.position.x==position[0]&&sample.position.y==position[1]&&sample.position.z==position[2],"native sampled position exact");
check(sample.velocity.x==velocity[0]&&sample.velocity.y==velocity[1]&&sample.velocity.z==velocity[2],"native sampled velocity exact");
check(sample.rigid_body.axes==basis,"native sampled non-normalized matrix exact");}
}
