"""Check the dedicated stage against the existing packed-particle equation."""
from pathlib import Path
import argparse
import re

def members(source, name):
    body=re.search(r"uniform\s+"+name+r"\s*\{([^}]+)\}",source).group(1)
    body=re.sub(r"//[^\n]*","",body)
    result=[]
    for statement in body.split(";"):
        words=statement.strip().split(None,1)
        if words:
            result.extend((words[0],name.strip()) for name in words[1].split(","))
    return result

parser=argparse.ArgumentParser()
parser.add_argument("--root",type=Path,required=True)
args=parser.parse_args()
root=args.root
particle=(root/"shaders/race_particle.frag.glsl").read_text(encoding="ascii")
for file in ("textured.frag.glsl","textured_opaque.frag.glsl"):
    reference=(root/"shaders"/file).read_text(encoding="ascii")
    branch=re.search(r"if\s*\(material.fallback.w\s*==\s*9.0\)\s*\{([^}]+)\}",reference).group(1)
    equation=re.search(r"outColor\s*=([^;]+);",branch).group(1)
    actual=re.search(r"outColor\s*=([^;]+);",particle).group(1)
    assert re.sub(r"\s+","",equation)==re.sub(r"\s+","",actual),"packed-particle output equation differs"
    assert members(reference,"Frame")==members(particle,"Frame"),"frame uniform layout differs"
    assert members(reference,"Material")==members(particle,"Material"),"material uniform layout differs"
assert "layout(early_fragment_tests) in;" in particle,"discard must retain early depth rejection"
assert re.search(r"vec4 texel\s*=\s*texture\(baseTexture,uv\);",particle)
assert "if(particle_sample_is_zero(texel.a))discard;" in particle
assert "if(frame.options.x!=0u)uv.y=1.0-uv.y;" in particle
assert "texelFetch" not in particle and "textureLod" not in particle,"sampler path changed"
assert particle.count("discard;")==1
print("PASS particle stage: both fallback9 equations, uniform layouts, original filtered sample and early-depth declaration")
