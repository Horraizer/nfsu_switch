"""Actual-source moving-road pool/H comparison against original execution.
Synthetic RGB/lateral/attenuation are explicit test boundaries, not defaults.
"""
from pathlib import Path
import argparse,ast,subprocess,sys,math
sys.dont_write_bytecode=True
parser=argparse.ArgumentParser();parser.add_argument('--retail-root',type=Path,required=True)
args=parser.parse_args();root=Path.cwd();out=root/'build/worker-handoff'
out.mkdir(parents=True,exist_ok=True)
CPP='#include "nfsu/frontend_motion.h"\n#include <iostream>\n#include <iomanip>\nint main() {\n const float markers[4][3]={{41.37820053100586F,-1.0800316333770752F,13.673964500427246F},{16.810354232788086F,-1.0800316333770752F,13.673964500427246F},{-9.519950866699219F,-1.0800316333770752F,13.673964500427246F},{-35.60659408569336F,-1.0800316333770752F,13.673964500427246F}};\n const float rgb[3]={17,39,71},atten[3]={.4F,.03F,.001F},car[3]={0,0,.20027998089790344F};\n std::cout<<std::setprecision(10);\n for(float offset:{0.F,99.F,101.F,199.F}) {\n  nfsu::FrontendRoad road;road.offset=offset;std::array<nfsu::FrontendPointLight,32> pool;\n  const auto count=nfsu::frontend_road_lights(road,markers,.125F,rgb,atten,pool);\n  for(unsigned i=0;i<count;++i) {\n   std::cout<<"SOURCE_POOL "<<offset<<" "<<i;\n   for(float v:pool[i].position)std::cout<<" "<<v;\n   for(float v:pool[i].rgb)std::cout<<" "<<v;\n   std::cout<<" "<<pool[i].radius;for(float v:pool[i].attenuation)std::cout<<" "<<v;std::cout<<"\\n";\n  }\n  const auto h=nfsu::frontend_point_lighting(pool.data(),count,car);\n  std::cout<<"SOURCE_H "<<offset;for(const auto& v:h.harmonic)for(float x:v)std::cout<<" "<<x;std::cout<<"\\n";\n }\n}\n'
cpp=out/'menu66-road-harmonic-source.cpp';cpp.write_text(CPP,encoding='ascii')
exe=out/'menu66-road-harmonic-source.exe'
if exe.exists():exe.unlink()
subprocess.run(['C:/Strawberry/c/bin/g++.exe','-std=c++17','-Iinclude',str(cpp),'-Wl,--start-group']+[str(p) for p in (root/'build').rglob('*.a')]+['-Wl,--end-group','-lwinmm','-o',str(exe)],check=True)
assert exe.is_file()
source=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
assert source.stdout.strip() and not source.stderr.strip()
pools={};harmonics={}
for line in source.stdout.splitlines():
 values=line.split();offset=float(values[1])
 if values[0]=='SOURCE_H':harmonics[offset]=list(map(float,values[2:]))
 elif values[0]=='SOURCE_POOL':pools.setdefault(offset,[]).append((int(values[2]),list(map(float,values[3:]))))
 else:raise AssertionError('unexpected source output')
for offset in (0.,99.,101.,199.):
 result=subprocess.run([sys.executable,str(root/'tools/frontend66_lighting_native.py'),'--retail-root',str(args.retail_root),'--road-offset',str(offset)],check=True,capture_output=True,text=True)
 assert result.stdout.strip() and 'PASS:' in result.stdout and not result.stderr.strip()
 native_h=ast.literal_eval(next(x.split(' ',1)[1] for x in result.stdout.splitlines() if x.startswith('NATIVE_FE_LOCAL_HARMONIC ')))
 native_pool=ast.literal_eval(next(x.split(' ',2)[2] for x in result.stdout.splitlines() if x.startswith('NATIVE_ROAD_LIGHT_POOL ')))
 assert len(harmonics[offset])==len(native_h)==40
 assert all(math.isfinite(v) for v in harmonics[offset])
 worst=max(abs(a-b) for a,b in zip(harmonics[offset],native_h));assert worst<.000001,worst
 assert len(pools[offset])==len(native_pool)==20
 for (index,actual),(native_index,position,rest) in zip(pools[offset],native_pool):
  assert index==native_index
  expected=list(position)+[17.,39.,71.,rest[0]]+list(rest[5:8])
  assert len(actual)==len(expected)==10
  assert all(abs(a-b)<.00002 for a,b in zip(actual,expected))
 print('PASS source pool/all40 harmonic values against genuine native offset',offset,'max_error',worst)
assert any(abs(a-b)>.001 for a,b in zip(harmonics[99.],harmonics[101.]))
print('PASS: actual-source moving pool and native-equation harmonic generation; explicit synthetic startup inputs')
