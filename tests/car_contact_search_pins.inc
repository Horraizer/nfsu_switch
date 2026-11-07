// Original contact_search_native.py; finite selector0 pair boundaries only.
{
 {auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
 const auto shape=RaceContactBox{{-1,-1,-1},{1,1,1},{}};
 VehicleState a,b;a.velocity={30,0,0};b.position={2.6F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={1.6F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.016F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search closing");
 check(job.remaining==0.006937500089406967F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.0F,0.0F,0.0F},wa={1.0F,1.0F,1.0F},wb={1.0335936546325684F,1.0F,1.0F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.03359365463256836F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
 const auto shape=RaceContactBox{{-1,-1,-1},{1,1,1},{}};
 VehicleState a,b;a.velocity={30,0,0};b.position={1.6F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={1.5F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.016F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search deep_start");
 check(job.remaining==0.015937499701976776F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.2449999749660492F,0.0F,0.0F},wa={0.7549999952316284F,1.0F,1.0F},wb={0.8446093797683716F,1.0F,1.0F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.08960938453674316F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
 const auto shape=RaceContactBox{{-1,-1,-1},{1,1,1},{}};
 VehicleState a,b;a.velocity={30,0,0};b.position={2.6F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={1.6F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.008F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search unequal");
 check(job.remaining==0.0034687500447034836F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.0F,0.0F,0.0F},wa={1.0F,1.0F,1.0F},wb={1.0335936546325684F,1.0F,1.0F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.03359365463256836F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
 const auto shape=RaceContactBox{{-1,-1,-1},{1,1,1},{}};
 VehicleState a,b;a.velocity={30,0,0};b.position={2.6F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={1.6F,0,0};a.simulation_seconds=0.0F;b.simulation_seconds=0.0F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search zero");
 check(job.remaining==0.0F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.2449999749660492F,0.0F,0.0F},wa={0.7549999952316284F,1.0F,1.0F},wb={0.8450000286102295F,1.0F,1.0F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.09000003337860107F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
 const auto shape=box;
 VehicleState a,b;a.velocity={30,0,0};b.position={5.0F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={4.0F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.016F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search closing");
 check(job.remaining==0.007187500596046448F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.004999999888241291F,0.0F,0.0F},wa={2.1116390228271484F,0.0003906107449438423F,-0.09704925864934921F},wb={2.15071702003479F,-0.0004921514191664755F,-0.09704859554767609F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.039087966084480286F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
 const auto shape=box;
 VehicleState a,b;a.velocity={30,0,0};b.position={4.0F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={3.9F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.016F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search deep_start");
 check(job.remaining==0.015937499701976776F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.2449999749660492F,0.0F,0.0F},wa={1.8716392517089844F,0.0003906107449438423F,-0.097049780189991F},wb={1.9411075115203857F,-0.0004921514191664755F,-0.09704859554767609F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.06947387009859085F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
 const auto shape=box;
 VehicleState a,b;a.velocity={30,0,0};b.position={5.0F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={4.0F,0,0};a.simulation_seconds=0.016F;b.simulation_seconds=0.008F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search unequal");
 check(job.remaining==0.003593750298023224F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.004999999888241291F,0.0F,0.0F},wa={2.1116390228271484F,0.0003906107449438423F,-0.09704925864934921F},wb={2.15071702003479F,-0.0004921514191664755F,-0.09704859554767609F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.039087966084480286F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
 {auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
 const auto shape=box;
 VehicleState a,b;a.velocity={30,0,0};b.position={5.0F,0,0};
 const auto pa=race_contact_pose(a),pb=race_contact_pose(b);
 b.position={4.0F,0,0};a.simulation_seconds=0.0F;b.simulation_seconds=0.0F;
 RaceContactJob job;check(search_race_contact(a,spec,shape,b,spec,shape,pa,pb,job)&&job.admitted,"native search zero");
 check(job.remaining==0.0F&&job.queries==8,"native remaining time and query count");
 const ContactVector shift={0.2449999749660492F,0.0F,0.0F},wa={1.8716392517089844F,0.0003906107449438423F,-0.097049780189991F},wb={1.941498041152954F,-0.0004921514191664755F,-0.09704859554767609F};
 check(job.correction.x==shift[0]&&job.correction.y==shift[1]&&job.correction.z==shift[2],"native fallback displacement");
 check(std::abs(job.query.distance-0.06986436247825623F)<2e-6,"native final distance");
 for(unsigned k=0;k<3;++k)check(std::abs(job.query.witness_a[k]-wa[k])<2e-6&&std::abs(job.query.witness_b[k]-wb[k])<2e-6,"native searched witnesses");}
}
