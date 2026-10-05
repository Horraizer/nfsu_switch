"""Check the actual draw recorder against sparse shader input locations."""
from pathlib import Path
import argparse,re

def check(source,manifests):
    begin=source.index('std::array<DkVtxAttribState, 4> attributes{};')
    end=source.index('out.bindVtxBufferState(',begin)
    block=source[begin:end]
    if 'attributes[input.location]' not in block:raise ValueError('location-indexed fill absent')
    # This conversion sends every location including initialized None holes.
    if 'out.bindVtxAttribState(attributes);' not in block:raise ValueError('location span truncated')
    for path in manifests:
        locations=[int(x) for x in re.findall(r'^attribute location=(\d+)',path.read_text(),re.M)]
        if not locations or max(locations)>=4:raise ValueError('shader location exceeds recorder span: '+path.name)
    frontend=next(path for path in manifests if path.stem=='frontend_car_textured')
    locations=[int(x) for x in re.findall(r'^attribute location=(\d+)',frontend.read_text(),re.M)]
    if locations!=[0,2,3]:raise ValueError('expected authentic sparse frontend inputs')
    # The old API count includes indices0..2 only, proving the actual normal
    # location is omitted independently of the implementation comment.
    if max(locations)<len(locations):raise ValueError('negative control no longer exercises sparse span')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True)
    root=parser.parse_args().root
    source=(root/'switch/deko_renderer.cpp').read_text()
    manifests=list((root/'shaders').glob('*.manifest'))
    check(source,manifests)
    bad=source.replace('out.bindVtxAttribState(attributes);','out.bindVtxAttribState(dk::detail::ArrayProxy<DkVtxAttribState const>{draw.program->attribute_count,attributes.data()});',1)
    try:check(bad,manifests)
    except ValueError:print('PASS negative control rejects truncated sparse normal location')
    else:raise ValueError('negative control accepted')
    print('PASS actual recorder span covers all authored shader locations including FE0,2,3; holes disabled')
if __name__=='__main__':main()
