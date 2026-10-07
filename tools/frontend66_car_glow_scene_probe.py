"""Actual preset marker/source sprite versus genuine original primary consumer."""
from pathlib import Path
import argparse,subprocess,sys,struct,runpy,io,contextlib
sys.dont_write_bytecode=True
args=argparse.ArgumentParser();args.add_argument('--retail-root',type=Path,required=True);retail=args.parse_args().retail_root
root=Path.cwd();out=root/'build/worker-handoff';cpp=out/'menu66-car-glow-scene-probe.cpp';exe=cpp.with_suffix('.exe')
cpp.write_text(r"""#include "nfsu/frontend_scene.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/stock_frame.h"
#include "nfsu/race_events.h"
#include <iostream>
#include <iomanip>
int main(int argc,char** argv){using namespace nfsu;std::string error;std::filesystem::path root=argv[1];
 CarAssets assets;StockFrameInputs inputs;std::vector<unsigned char> front;std::vector<RaceTableIssue> issues;
 if(!load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",assets,error)||!read_car_frame_inputs(root,assets,inputs,true,error)||
 !load_race_image(root/"FrontEnd/FrontB.lzc",front,issues)){std::cerr<<error;return 1;}
 const auto& globals=inputs.files[2].bytes;const auto parts=parse_car_part_database(globals.data(),globals.size());
 InstalledPartSelection installed;if(!resolve_frontend_skyline_parts(front,parts,installed,error)){std::cerr<<error;return 1;}
 auto request=stock_car_request("SKYLINE");request.installed_parts=installed;request.pass=CarDrawPass::front_end;StockFrame car;
 if(!prepare_stock_frame(inputs,request,car,error)){std::cerr<<error;return 1;}
 const auto& type=car.car_types.records.at(car.report.car_type);
 const float height=type.wheels[0].rolling_radius-type.wheels[0].vertical+.02F;std::cout<<std::setprecision(10)<<"ROOT "<<height<<"\n";
 unsigned index=0;
 for(const auto& marker:car.report.markers){bool head=marker.name_hash==0x9DB90133U||marker.name_hash==0xD09091C6U;
  bool tail=marker.name_hash==0x31A66786U||marker.name_hash==0xA2A2FC7CU||marker.name_hash==0xBF700A79U;if(!head&&!tail)continue;
  const auto& g=marker.geometry_source==0?car.car_geometry:car.stock_geometry.at(marker.geometry_source-1);
  const auto& r=g.meshes.at(marker.source_mesh).markers.at(marker.index);
  std::cout<<"ANCHOR "<<index<<" "<<head;for(unsigned a=0;a<3;++a)std::cout<<" "<<r.matrix[12+a];for(unsigned a=0;a<3;++a)std::cout<<" "<<r.matrix[8+a];std::cout<<"\n";index+=head?2:1;
 }
 FrontendGpuScene scene;if(!scene.load(root,1280,720,error)){std::cerr<<error;return 1;}
 for(const auto& d:scene.commands().owning_draws())if(d.frontend_sprite_kind!=0){
  std::cout<<"QUAD "<<d.instance<<" "<<d.frontend_sprite_kind-1<<" "<<d.material.alpha[0];
  for(unsigned a=0;a<3;++a)std::cout<<" "<<d.material.environment_color[a];
  int signs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
  for(unsigned c=0;c<4;++c)for(unsigned a=0;a<3;++a)std::cout<<" "<<d.placement.rows[12+a]+signs[c][0]*d.placement.rows[a]+signs[c][1]*d.placement.rows[8+a];
  std::cout<<"\n";
 }
}
""",encoding='ascii')
if exe.exists():exe.unlink()
libs=list((root/'build').rglob('*.a'))
subprocess.run(['C:/Strawberry/c/bin/g++.exe','-std=c++17','-Iinclude','-Ibuild/generated',str(cpp),'-Wl,--start-group']+[str(p) for p in libs]+['-Wl,--end-group','-lwinmm','-o',str(exe)],check=True)
assert exe.is_file() and exe.stat().st_size>0
r=subprocess.run([str(exe),str(retail)],text=True,capture_output=True,check=True);assert not r.stderr
anchors=[];source={};height=None
for line in r.stdout.splitlines():
 fields=line.split()
 if fields[0]=='ROOT':height=float(fields[1])
 elif fields[0]=='ANCHOR':anchors.append((int(fields[1]),bool(int(fields[2])),[float(x) for x in fields[3:6]],[float(x) for x in fields[6:9]]))
 elif fields[0]=='QUAD':source[int(fields[1])]=[float(x) for x in fields[2:]]
assert anchors and source and height is not None
sys.argv=[sys.argv[0],'--retail-root',str(retail),'--display-width','1280']
with contextlib.redirect_stdout(io.StringIO()):base=runpy.run_path(str(root/'tools/frontend66_flare_native.py'))
m=base['m'];from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
matrix=0x1090000;node=0x1091000;m.uc.mem_write(matrix,base['base']['identity']);m.uc.mem_write(matrix+56,struct.pack('<f',height))
for i in range(3):m.write(0x72fa14+i*4,base['texture']+i*0x100)
quads=[]
def hook(uc,at,size,data):
 if at==0x40c300:
  sp=uc.reg_read(UC_X86_REG_ESP);p=uc.reg_read(UC_X86_REG_EAX);b=bytes(uc.mem_read(p,0x94))
  quads.append(([struct.unpack_from('<3f',b,x) for x in (16,32,48,64)],tuple(b[128:132])))
  uc.reg_write(UC_X86_REG_EIP,m.integer(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
h=m.uc.hook_add(UC_HOOK_CODE,hook);active=0
for index,head,point,normal in anchors:
 m.uc.mem_write(node+12,bytes((1,0 if head else 1,0,0)));m.uc.mem_write(node+16,struct.pack('<3f',*point));m.uc.mem_write(node+28,bytes(4));m.uc.mem_write(node+32,struct.pack('<3f',*normal));quads.clear()
 gain=1.0 if head else struct.unpack('<f',struct.pack('<f',.7))[0]
 m.run(0x5447c0,(0x1050000,node,struct.unpack('<I',struct.pack('<f',gain))[0],0,0,0),{UC_X86_REG_EAX:matrix})
 expected_count=2 if head else 1
 if not quads:
  assert all(source[index+i][1]==0 for i in range(expected_count)),(index,source[index]);continue
 assert len(quads)==expected_count
 for i,(corners,rgba) in enumerate(quads):
  actual=source[index+i];assert round(actual[1]*255)==rgba[3] and actual[2:5]==list(rgba[:3]),(index+i,actual,rgba)
  for c in range(4):
   for a in range(3):assert abs(actual[5+c*3+a]-corners[c][a])<.00001,(index+i,c,a,actual,corners)
  active+=1
m.uc.hook_del(h);assert active>0
print('PASS: actual FE_SKYLINE marker primary car sprites all corners/colors/visibility match genuine original')
print('BOUNDARY: frontend null-owner resting gains, original fixed primary view; texture allocation and primary clipping supplied; no occlusion/floor-car parity claim')
