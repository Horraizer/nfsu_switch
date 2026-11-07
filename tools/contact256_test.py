"""Assertions for bounded original-byte probes; not original-game parity."""

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path

from world_body_trace import INPUTS, digest_file


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def near(actual, expected, label, tolerance=0.00002):
    if isinstance(expected, (list, tuple)):
        require(len(actual) == len(expected), label + ': length')
        for i, (a, b) in enumerate(zip(actual, expected)):
            near(a, b, label + '[%d]' % i, tolerance)
    else:
        require(math.isfinite(actual) and abs(actual - expected) <= tolerance,
                '%s: %r != %r' % (label, actual, expected))


def validate(report):
    require(report['schema'] == 'contact256-v1', 'schema')
    require(report['input'] == dict(size=INPUTS['speed.exe'][0], sha256=INPUTS['speed.exe'][1]), 'input')
    require(report['runtime_capture'] is None and report['cadence'] is None and
            report['final_published_kinematics'] is None, 'evidence boundary')
    cases = {c['name']: c for c in report['native']['cases']}
    require(set(cases) == {'head_on', 'offset', 'glancing', 'separating', 'stationary',
        'no_contact_query_only', 'distant_forced_solver', 'hard_hit', 'already_state1',
        'seeded_accumulator', 'light_mass', 'slow_closing', 'touching_origin_fallback',
        'overlap_without_separation_pass'}, 'complete case inventory')
    require(len(report['native']['cases']) == len(cases), 'duplicate case')
    for name, c in cases.items():
        require(len(c['before']) == len(c['after']) == 2, name + ': both bodies')
        require(c['witness']['dirty_after_query'] == 0, name + ': cache clear')
        near(c['geometry']['point'], [(a+b)/2 for a, b in
             zip(c['witness']['witness_a'], c['witness']['witness_b'])], name + ': midpoint')
        near(sum(n*n for n in c['geometry']['normal']), 1, name + ': normal')
        require(c['witness']['scalar'] >= 0, name + ': scalar is unsigned')
        require(c['boundaries']['00471DE0'] and c['boundaries']['005B0668'], name + ': actual geometry/math')
        require(not c['boundaries']['0046C09B'] and not c['boundaries']['00465DB4'], name + ': excluded branches')
        for a, b in zip(c['before'], c['after']):
            require(a['body'] == b['body'] and a['actor'] == b['actor'], name + ': identity')
            for field in ('position', 'orientation', 'velocity', 'angular_velocity', 'actor_velocity'):
                require(a[field] == b[field], name + ': raw cached ' + field)
        require(len(c['hits']) == len(c['after_hit']) == len(c['hooks']), name + ': callback boundary')
        for h in c['hooks']:
            require(h == dict(hook='skip_current_hit_effect_tail_4658B5_to_465CF2',
                              address='004658B5'), name + ': undeclared hook')
        for a in c['applied']:
            require('after' in a, name + ': application returned')
        seed = c['input']['accumulated']
        total = [seed[i] + sum(h['impulse'][i] for h in c['hits']) for i in range(3)]
        near(c['accumulated'], total, name + ': accumulation')
        near(c['magnitude_sum'], c['input']['magnitude_sum'] + sum(
            math.sqrt(sum(v*v for v in h['impulse'])) for h in c['hits']), name + ': magnitude sum')
    for name in ('separating', 'stationary', 'no_contact_query_only', 'overlap_without_separation_pass'):
        c = cases[name]
        require(not c['hits'] and not c['applied'] and c['after'] == c['before'], name + ': no response')
    for name in ('head_on', 'offset', 'glancing', 'already_state1', 'seeded_accumulator',
                 'light_mass', 'slow_closing', 'touching_origin_fallback', 'distant_forced_solver'):
        c = cases[name]
        require(len(c['hits']) == 1 and len(c['applied']) == 2, name + ': one hit/two final applications')
        require([a['return_address'] for a in c['applied']] == ['0046C5D5', '0046C5E7'], name + ': final order')
        require([a['body'] for a in c['applied']] == ['01101000', '01100000'], name + ': B then A')
        near(c['applied'][1]['impulse'], c['hits'][0]['impulse'], name + ': A channel')
        near(c['applied'][0]['impulse'], [-v for v in c['hits'][0]['impulse']], name + ': B channel')
    # Analytic head-on expectation with the ACTUAL box support corner (y=z=1):
    # denominator = .5+.5 + .4*(1+1) + .4*(1+1), restitution .1.
    head = cases['head_on']
    near(head['hits'][0]['impulse'], [-22/2.6, 0, 0], 'head-on effective inertia')
    near(head['after'][0]['linear_momentum'], [20-22/2.6, 0, 0], 'head-on momentum')
    near(head['after'][0]['angular_momentum'], [0, -4/math.sqrt(2), 4/math.sqrt(2)], 'angular clamp')
    near(cases['slow_closing']['hits'][0]['impulse'], [-.6, 0, 0], 'minimum impulse')
    near(cases['light_mass']['hits'][0]['impulse'], [-8.205645561218262, 0, 0], 'collision mass cap')
    near(cases['light_mass']['after'][0]['linear_momentum'], [5.564516067504883, 0, 0], 'apply mass rescaling')
    # Recovered witnesses pin the offset/glancing responses, including rounding.
    near(cases['offset']['hits'][0]['impulse'], [-9.838704109191895, -.005864750128239393, 0], 'offset impulse')
    near(cases['glancing']['hits'][0]['impulse'], [-3.4982447624206543, -1.0490189790725708, .00029195682145655155], 'glancing impulse')
    near(cases['glancing']['after'][0]['angular_momentum'], [.20992055535316467, -.6997657418251038, .9795854687690735], 'glancing yaw momentum')
    near(cases['no_contact_query_only']['witness']['scalar'], 6, 'separated witness')
    require(cases['no_contact_query_only']['witness']['flag14'] == 0, 'no contact flag')
    require(not cases['no_contact_query_only']['input']['solver_invoked'], 'query-only admission explicit')
    require(cases['distant_forced_solver']['witness']['flag14'] == 0 and
            cases['distant_forced_solver']['hits'], 'standalone solver has no distance admission')
    # A zero scalar is not penetration depth, nor equality of witness points.
    require(head['witness']['scalar'] == 0 and head['witness']['witness_a'] != head['witness']['witness_b'], 'zero scalar tolerance')
    require(cases['offset']['witness']['flag14'] == 1 and cases['offset']['witness']['flag15'] == 0, 'two distinct thresholds')
    require(cases['touching_origin_fallback']['witness']['witness_a'] ==
            cases['touching_origin_fallback']['witness']['witness_b'], 'coincident witnesses')
    near(cases['touching_origin_fallback']['geometry']['normal'], [1, 0, 0], 'origin fallback')
    require(cases['touching_origin_fallback']['boundaries']['0046B9E2'] and
            not cases['overlap_without_separation_pass']['boundaries']['0046B9E2'], 'normal path coverage')
    near(cases['overlap_without_separation_pass']['geometry']['normal'], [-1, 0, 0], 'unprepared overlap limit')
    hard = cases['hard_hit']
    require([a['return_address'] for a in hard['applied']] ==
            ['00466097', '00466286', '00466097', '00466286', '0046C5D5', '0046C5E7'], 'callback application order')
    require([a['body'] for a in hard['applied']] ==
            ['01100000', '01100000', '01101000', '01101000', '01101000', '01100000'], 'callback ownership')
    require([b['state'] for b in hard['hits'][0]['bodies']] == [0, 0] and
            [b['state'] for b in hard['after_hit'][0]['bodies']] == [1, 1], 'state transition inside callback')
    require(hard['record_gate'] == 1 and all(b['state_timer'] == 0 for b in hard['after']), 'hard-hit gate/timer')
    require(all(a['arm'][2] == 0 for a in hard['applied'][-2:]), 'planted hard-hit arm projection')
    near(hard['hits'][0]['impulse'], [-69.97073364257812, -.24191120266914368, -.24191120266914368], 'hard current hit')
    near(hard['after'][0]['linear_momentum'], [30.029266357421875, 6.829071521759033, 3.1330482959747314], 'callback-inclusive final momentum')
    require(cases['already_state1']['record_gate'] == 0 and
            all(b['state_timer'] == .75 for b in cases['already_state1']['after']), 'already-state1 gate')
    require(cases['seeded_accumulator']['record_gate'] == 0 and
            cases['seeded_accumulator']['after'] == head['after'], 'current-hit not accumulated state gate')
    cache = report['native']['cache']
    for name, value in [('first', 6), ('resized', 5), ('rotated_clean', 5), ('rotated_dirty', 6), ('moved', 7)]:
        near(cache[name]['scalar'], value, 'cache ' + name)
    require(cache['rotated_clean']['dirty_before_query'] == 0 and
            cache['rotated_dirty']['dirty_before_query'] == 1, 'rotation needs external dirty writer')
    require(report['guards'] == ['changed ownership source rejected', 'incomplete return rejected'], 'negative guards')


def load_checked(path):
    from contact256_trace import TOOLS
    report = json.loads(path.read_text(encoding='ascii'))
    require(report['tool_hashes'] == {name: digest_file(Path(__file__).with_name(name))
                                    for name in TOOLS}, 'stale tool hashes')
    before, after = path.with_name('before.json').read_bytes(), path.with_name('after.json').read_bytes()
    require(before == after and hashlib.sha256(before).hexdigest().upper() ==
            report['retail_manifest_sha256'], 'manifest evidence')
    require(digest_file(path.with_name('source.txt')) == report['source_sha256'], 'source evidence')
    validate(report)
    # Show that essential contract violations are detected, not only happy pins.
    for name, edit in [
        ('cached velocity rewrite', lambda r: r['native']['cases'][0]['after'][0]['velocity'].__setitem__(0, 0)),
        ('missing current hit', lambda r: r['native']['cases'][0]['hits'].clear()),
        ('invented runtime', lambda r: r.__setitem__('runtime_capture', 'live'))]:
        changed = copy.deepcopy(report)
        edit(changed)
        try:
            validate(changed)
        except AssertionError:
            continue
        raise AssertionError('negative report accepted: ' + name)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    result = load_checked(args.report)
    if args.compare:
        other = load_checked(args.compare)
        require(result['run_id'] != other['run_id'], 'different fresh runs required')
        for field in ('native', 'source', 'executed_blocks', 'guards', 'tool_hashes'):
            require(result[field] == other[field], 'repeatability: ' + field)
    print('PASS: all contact/state/cache pins, artifact identity and negative report guards')
    if args.compare:
        print('PASS: two distinct fresh runs agree')
