"""Check ordinary-race GPU sample wiring and reject the observed null-context bug."""
from pathlib import Path
import argparse


def check(main, renderer):
    call = main[main.index('require(renderer.present(error,'):]
    call = call[:call.index('),error);')]
    assert 'profile_frame ?' not in call
    assert 'ab_minimal?nullptr:&present,&scene_frame,' in call
    context = main[main.index('const DekoSceneFrame scene_frame{'):main.index('const auto present_start', main.index('const DekoSceneFrame scene_frame{'))]
    assert 'driving_before && !finished' in context
    assert 'profile_frame || cpu_probe' in context
    assert 'query_recording=profile_enabled && !frontend && (!scene_frame || scene_frame->sample);' in renderer
    present = renderer[renderer.index('bool present(std::string& error, DekoPresentTiming*'):renderer.index('bool capture(')]
    assert present.index('frame_fences[slot].wait()') < present.index('consume_query(slot)') < present.index('query_recording=')
    direct = main[main.index('const DekoSceneFrame scene_frame{'):]
    periodic = direct[direct.index('if (!profile_frame && (presented == 1'):]
    assert 'GPU_PROBE_PROGRESS samples=' in periodic[:periodic.index('PERF BUILD')]
    panel = main[main.index('if (panel_mode) {'):main.index('const DekoSceneFrame scene_frame{')]
    assert 'GPU_PROBE_PROGRESS samples=' not in panel


parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
args = parser.parse_args()
main = (args.root/'switch/race_main.cpp').read_text(encoding='ascii')
renderer = (args.root/'switch/deko_renderer.cpp').read_text(encoding='ascii')
check(main, renderer)
for name, changed in [
    ('ordinary null frame', main.replace('ab_minimal?nullptr:&present,&scene_frame,', 'ab_minimal?nullptr:&present,profile_frame ? &scene_frame : nullptr,')),
    ('missing ordinary progress', main.replace('GPU_PROBE_PROGRESS samples=', 'GPU_PROBE_HIDDEN samples=')),
    ('always sample', main.replace('profile_frame || cpu_probe', 'true')),
]:
    assert changed != main
    try:
        check(changed, renderer)
    except AssertionError:
        continue
    raise AssertionError('regression not detected: '+name)
print('PASS ordinary GPU sampling wiring; three known regressions rejected; hardware timings remain required')
