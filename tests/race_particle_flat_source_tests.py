"""Check the lean stage's strict source contract and original projection."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parent.parent
old=(ROOT/"shaders/placed_headlight.vert.glsl").read_text(encoding="ascii")
vs=(ROOT/"shaders/race_particle_flat.vert.glsl").read_text(encoding="ascii")
ps=(ROOT/"shaders/race_particle_flat.frag.glsl").read_text(encoding="ascii")
def projection(s):
    s=re.sub(r"//[^\n]*","",s)
    start=s.index("vec3 slot");end=s.index("vz);",start)+4
    return re.sub(r"\s+","",s[start:end])
assert projection(old)==projection(vs),"original triangle projection changed"
def normalized(s):return re.sub(r"\s+","",re.sub(r"//[^\n]*","",s))
restored=vs.replace("layout(location=1) flat out vec4 outColor;","layout (location = 1) out vec4 outColor;").replace("outColor=inColor*2.0;","outColor = inColor;")
assert normalized(restored)==normalized(old),"vertex specialization changed another original operation"
assert "layout(location=1) flat out vec4 outColor;" in vs
assert "outColor=inColor*2.0;" in vs and "outTexcoord = inTexcoord;" in vs
assert "layout (location = 2) out vec2 outTexcoord;" in vs
assert "layout(location=1) flat in vec4 inColor;" in ps
assert "layout(early_fragment_tests) in;" in ps
assert "if(texel.a==0.0)discard;" in ps and "outColor=texel*inColor;" in ps
assert ps.count("texture(")==1 and "uniform Frame" not in ps and "uniform Material" not in ps
renderer=(ROOT/"switch/deko_renderer.cpp").read_text(encoding="ascii")
for marker in ("race_particle_flat_material(material.fallback[3],material.alpha[0],material.alpha[3])","race_particle_flat_frame(commands->frame.options[0],commands->frame.options[2])",
               "race_particle_flat_colors(r.data","race_particle_flat_indices(indices.data","race_particle_stage_changed(particle,flat_particle,previous_particle,previous_flat)"):
    assert marker in renderer,marker
print("PASS original projection, flat authored tint, perspective UV, one sample/zero-kill/no fragment blocks and guarded stage transitions")
