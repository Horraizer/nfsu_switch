// Generated from contact_callback_native.py against the authenticated USA 1.4 image.
void callback_rotation_pins(){
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={100.0F,0.0F,0.0F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={50.0F,0.0F,0.0F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={1.0F,0.0F,-0.0F};
c.body_axes[0][1]={-0.0F,1.0F,0.0F};
c.body_axes[0][2]={0.0F,0.0F,1.0F};
c.actor_up[0]={0.0F,0.0F,1.0F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={2.000499963760376F,0.699999988079071F,0.0F};
b.linear_momentum={-100.0F,0.0F,0.0F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-50.0F,0.0F,0.0F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={1.0F,0.0F,-0.0F};
c.body_axes[1][1]={-0.0F,1.0F,0.0F};
c.body_axes[1][2]={0.0F,0.0F,1.0F};
c.actor_up[1]={0.0F,0.0F,1.0F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={4,4};
rec.state_gate=false;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={1.0F,-0.0001192179333884269F,0.0F};
g.point={1.000249981880188F,1.0F,1.0F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=1)++failures;
{ContactVector expected={50.806480407714844F,9.66862678527832F,3.3205628395080566F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"identity a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={-6.406071662902832F,-3.3213930130004883F,17.071950912475586F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"identity a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=1)++failures;
{ContactVector expected={-50.806480407714844F,-9.66862678527832F,3.3205628395080566F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"identity b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={7.269418239593506F,3.3213930130004883F,-2.928048610687256F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"identity b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={87.75825500488281F,47.94255447387695F,0.0F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={43.879127502441406F,23.971277236938477F,0.0F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={0.8775825500488281F,0.4794255495071411F,-0.0F};
c.body_axes[0][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[0][2]={0.0F,0.0F,1.0F};
c.actor_up[0]={0.0F,0.0F,1.0F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={1.4200060367584229F,1.5733985900878906F,0.0F};
b.linear_momentum={-87.75825500488281F,-47.94255447387695F,0.0F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-43.879127502441406F,-23.971277236938477F,0.0F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={0.8775825500488281F,0.4794255495071411F,-0.0F};
c.body_axes[1][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[1][2]={0.0F,0.0F,1.0F};
c.actor_up[1]={0.0F,0.0F,1.0F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={4,4};
rec.state_gate=false;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={0.8777041435241699F,0.47920286655426025F,0.0F};
g.point={0.3983764350414276F,1.3571279048919678F,1.0F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=1)++failures;
{ContactVector expected={40.045101165771484F,32.675086975097656F,3.32045316696167F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"yaw a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={-4.029603004455566F,-5.985920429229736F,17.071931838989258F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"yaw a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=1)++failures;
{ContactVector expected={-40.045101165771484F,-32.675086975097656F,3.32045316696167F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"yaw b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={4.787214279174805F,6.399805068969727F,-2.928042411804199F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"yaw b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={93.93727111816406F,0.0F,-34.28977966308594F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={46.96863555908203F,0.0F,-17.14488983154297F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={0.939372718334198F,0.0F,-0.34289780259132385F};
c.body_axes[0][1]={-0.0F,1.0F,0.0F};
c.body_axes[0][2]={0.34289780259132385F,0.0F,0.939372718334198F};
c.actor_up[0]={0.34289780259132385F,0.0F,0.939372718334198F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={1.879215121269226F,0.699999988079071F,-0.6859670877456665F};
b.linear_momentum={-93.93727111816406F,0.0F,34.28977966308594F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-46.96863555908203F,0.0F,17.14488983154297F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={0.939372718334198F,0.0F,-0.34289780259132385F};
c.body_axes[1][1]={-0.0F,1.0F,0.0F};
c.body_axes[1][2]={0.34289780259132385F,0.0F,0.939372718334198F};
c.actor_up[1]={0.34289780259132385F,0.0F,0.939372718334198F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={4,4};
rec.state_gate=false;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={0.9393890500068665F,-0.00011921180703211576F,-0.3428531587123871F};
g.point={0.8367382287979126F,1.0F,-0.6247953176498413F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=1)++failures;
{ContactVector expected={24.18311309814453F,9.036828994750977F,-5.809062957763672F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"pitch a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={9.503813743591309F,-2.448577880859375F,17.781789779663086F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"pitch a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=1)++failures;
{ContactVector expected={-24.18311309814453F,-9.036828994750977F,12.559017181396484F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"pitch b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={-2.6710152626037598F,3.2034053802490234F,-0.08633995056152344F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"pitch b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={82.43771362304688F,45.035926818847656F,-34.28977966308594F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={41.21885681152344F,22.517963409423828F,-17.14488983154297F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={0.8243771195411682F,0.4503592550754547F,-0.34289780259132385F};
c.body_axes[0][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[0][2]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.actor_up[0]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={1.3135685920715332F,1.5152515172958374F,-0.6859670877456665F};
b.linear_momentum={-82.43771362304688F,-45.035926818847656F,34.28977966308594F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-41.21885681152344F,-22.517963409423828F,17.14488983154297F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={0.8243771195411682F,0.4503592550754547F,-0.34289780259132385F};
c.body_axes[1][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[1][2]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.actor_up[1]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={0,4};
rec.state_gate=false;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={0.8242923021316528F,0.4501124620437622F,-0.34342533349990845F};
g.point={0.2548814117908478F,1.2787362337112427F,-0.6247951984405518F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=1)++failures;
{ContactVector expected={23.566829681396484F,15.855823516845703F,-7.020256042480469F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"airborne a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={6.005346775054932F,0.7902193069458008F,8.23810863494873F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"airborne a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=1)++failures;
{ContactVector expected={-23.566829681396484F,-15.855823516845703F,13.769271850585938F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"airborne b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={-4.214393615722656F,1.6901670694351196F,-5.337159633636475F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"airborne b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={82.43771362304688F,45.035926818847656F,-34.28977966308594F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={41.21885681152344F,22.517963409423828F,-17.14488983154297F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={0.8243771195411682F,0.4503592550754547F,-0.34289780259132385F};
c.body_axes[0][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[0][2]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.actor_up[0]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={1.3135685920715332F,1.5152515172958374F,-0.6859670877456665F};
b.linear_momentum={-82.43771362304688F,-45.035926818847656F,34.28977966308594F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-41.21885681152344F,-22.517963409423828F,17.14488983154297F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={0.8243771195411682F,0.4503592550754547F,-0.34289780259132385F};
c.body_axes[1][1]={-0.4794255495071411F,0.8775825500488281F,0.0F};
c.body_axes[1][2]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.actor_up[1]={0.30092114210128784F,0.16439396142959595F,0.939372718334198F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={2,4};
rec.state_gate=false;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={0.8242923021316528F,0.4501124620437622F,-0.34342533349990845F};
g.point={0.2548814117908478F,1.2787362337112427F,-0.6247951984405518F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=1)++failures;
{ContactVector expected={23.566829681396484F,15.855823516845703F,-7.020256042480469F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"mixed_support a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={6.005346775054932F,0.7902193069458008F,8.23810863494873F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"mixed_support a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=1)++failures;
{ContactVector expected={-23.566829681396484F,-15.855823516845703F,13.769271850585938F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"mixed_support b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={-4.214393615722656F,1.6901670694351196F,-5.337159633636475F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"mixed_support b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
{ContactBody a,b;ContactResponse r;ContactRecord rec;ContactGeometry g;ContactCallbackContext c;
a.identity=1;
a.inverse_mass=0.5F;
a.collision_state=0;
a.state_timer=0.75F;
a.position={0.0F,0.0F,0.0F};
a.linear_momentum={20.0F,0.0F,0.0F};
a.angular_momentum={0.0F,0.0F,0.0F};
a.cached_velocity={10.0F,0.0F,0.0F};
a.cached_angular_velocity={0.0F,0.0F,0.0F};
a.inverse_inertia[0]={1.0F,0.0F,0.0F};
a.inverse_inertia[1]={0.0F,1.0F,0.0F};
a.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[0][0]={1.0F,0.0F,-0.0F};
c.body_axes[0][1]={-0.0F,1.0F,0.0F};
c.body_axes[0][2]={0.0F,0.0F,1.0F};
c.actor_up[0]={0.0F,0.0F,1.0F};
b.identity=2;
b.inverse_mass=0.5F;
b.collision_state=0;
b.state_timer=0.75F;
b.position={2.000499963760376F,0.699999988079071F,0.0F};
b.linear_momentum={-20.0F,0.0F,0.0F};
b.angular_momentum={0.0F,0.0F,0.0F};
b.cached_velocity={-10.0F,0.0F,0.0F};
b.cached_angular_velocity={0.0F,0.0F,0.0F};
b.inverse_inertia[0]={1.0F,0.0F,0.0F};
b.inverse_inertia[1]={0.0F,1.0F,0.0F};
b.inverse_inertia[2]={0.0F,0.0F,1.0F};
c.body_axes[1][0]={1.0F,0.0F,-0.0F};
c.body_axes[1][1]={-0.0F,1.0F,0.0F};
c.body_axes[1][2]={0.0F,0.0F,1.0F};
c.actor_up[1]={0.0F,0.0F,1.0F};
c.world_seed=12345;
c.has_physical_bounds=true;
c.grounded_wheels={4,4};
rec.state_gate=true;
c.physical_lateral_bounds[0]={-1.0F,1.0F};
c.physical_lateral_bounds[1]={-1.0F,1.0F};
g.normal={1.0F,-0.0001192179333884269F,0.0F};
g.point={1.000249981880188F,1.0F,1.0F};
auto result=resolve_prepared_car_contact(a,b,rec,true,g,5,r,&c);
if(result!=ContactResolution::applied){++failures;std::cout<<"REFUSED\n";}
if(a.collision_state!=0)++failures;
{ContactVector expected={10.161295890808105F,-0.005864750128239393F,0.0F};
for(unsigned k=0;k<3;++k)if(std::abs(a.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"recovered_record a.linear_momentum["<<k<<"] got="<<a.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={0.0F,0.0F,3.9331352710723877F};
for(unsigned k=0;k<3;++k)if(std::abs(a.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"recovered_record a.angular_momentum["<<k<<"] got="<<a.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
if(b.collision_state!=0)++failures;
{ContactVector expected={-10.161295890808105F,0.005864750128239393F,0.0F};
for(unsigned k=0;k<3;++k)if(std::abs(b.linear_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"recovered_record b.linear_momentum["<<k<<"] got="<<b.linear_momentum[k]<<" expected="<<expected[k]<<"\n";}}
{ContactVector expected={0.0F,0.0F,-1.1829910278320312F};
for(unsigned k=0;k<3;++k)if(std::abs(b.angular_momentum[k]-expected[k])>0.00002F){++failures;std::cout<<"recovered_record b.angular_momentum["<<k<<"] got="<<b.angular_momentum[k]<<" expected="<<expected[k]<<"\n";}}
}
}
void collision_recovery_pins(){
{std::uint32_t state=1;float timer=0.0F;
update_car_collision_state(state,timer,4,0,1.0000000474974513);
check(state==1&&timer==1.0F,"original recovery begin");}
{std::uint32_t state=1;float timer=1.0F;
update_car_collision_state(state,timer,4,0,1.099750052235322);
check(state==1&&timer==1.0F,"original recovery before");}
{std::uint32_t state=1;float timer=1.0F;
update_car_collision_state(state,timer,4,0,1.1000000522471964);
check(state==0&&timer==1.0F,"original recovery threshold");}
{std::uint32_t state=1;float timer=1.0F;
update_car_collision_state(state,timer,4,0,1.1002500522590708);
check(state==0&&timer==1.0F,"original recovery after");}
{std::uint32_t state=1;float timer=1.0F;
update_car_collision_state(state,timer,3,0,2.0000000949949026);
check(state==1&&timer==0.0F,"original recovery support_lost");}
{std::uint32_t state=1;float timer=1.0F;
update_car_collision_state(state,timer,4,1,2.0000000949949026);
check(state==1&&timer==0.0F,"original recovery body_active");}
{std::uint32_t state=0;float timer=0.0F;
update_car_collision_state(state,timer,4,1,2.0000000949949026);
check(state==2&&timer==0.0F,"original recovery state2_begin");}
{std::uint32_t state=2;float timer=0.0F;
update_car_collision_state(state,timer,4,0,2.0000000949949026);
check(state==0&&timer==0.0F,"original recovery state2_end");}
{std::uint32_t state=2;float timer=0.0F;
update_car_collision_state(state,timer,3,0,2.0000000949949026);
check(state==2&&timer==0.0F,"original recovery state2_air");}
{std::uint32_t state=0;float timer=0.0F;
update_car_collision_state(state,timer,4,-1,2.0000000949949026);
check(state==0&&timer==0.0F,"original recovery negative_flag");}
}
