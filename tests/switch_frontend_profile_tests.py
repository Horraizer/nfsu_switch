"""Bound opt-in menu timestamp storage and power/target ownership."""
from pathlib import Path
import argparse

def check(root,renderer,main):
    mark=renderer[renderer.index('const auto mark_frontend='):renderer.index('mark_frontend(0);')]
    assert 'if(!frontend || !frontend_profile_passes)return;' in mark
    assert 'slot*192+point*16' in mark
    for slot in range(2):
        addresses=[slot*192+point*16 for point in range(12)]
        assert all(a%16==0 and a+16<=384 for a in addresses)
    assert '!impl_->block(384,' in renderer
    assert 'mark_frontend(10);' in renderer and 'mark_frontend(11);' in renderer
    assert '+slot*24' in renderer and '+slot*192,0,192' in renderer
    assert 'frontend && frontend_profile_passes' in renderer[renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame(')):renderer.index('mark_frontend(8);')]
    for call in ('mark_frontend(0);','mark_frontend(face+1);','mark_frontend(7);','mark_frontend(8);','mark_frontend(9);'):
        assert call in renderer
    assert 'consume_frontend_query(slot);' in renderer
    assert 'frontend_query_pending[slot]=frontend && frontend_profile_passes;' in renderer
    transition=main[main.index('} else if (action==FrontendAction::free_drive)'):main.index('const auto music_error=')]
    assert transition.index('frontend_renderer.reset();')<transition.index('frontend_gpu_profile.reset();')<transition.index('run(root,')
    assert main.index('std::unique_ptr<HandheldGpuProfile> frontend_gpu_profile;')<main.index('std::unique_ptr<DekoRenderer> frontend_renderer;')
    assert 'deko_effective_scene_height(scene_height,menu_docked,true)' in main
    assert 'art.atlas(),frontend_error,menu_height,profile_frame,active_trace)' in main
    assert 'std::make_unique<HandheldGpuProfile>(gpu_boost)' in main
    # Normal cadence reads only an existing post-present timestamp. It adds
    # no GPU command, wait or barrier and excludes opt-in profiling runs.
    counter=main[main.index('FrontendPresentationWindow presentation_window;'):main.index('if(menu.selected()!=previous_selection) {',main.index('FrontendPresentationWindow presentation_window;'))]
    assert '!profile_frame && frontend_presentation_stats.sample(armTicksToNs(presented),presentation_window)' in counter
    assert main.index('present_frontend(frontend_ui,frontend_error,&present_timing)')<main.index('const auto presented=armGetSystemTick();')<main.index('FrontendPresentationWindow presentation_window;')
    assert all(word not in counter for word in ('wait','barrier','renderer->','armGetSystemTick'))
    opening=main[main.index('const auto open_frontend=[&]'):main.index('open_frontend();')]
    assert 'frontend_presentation_stats.reset();' in opening
    assert 'self.clock_.resume();self.stats_.reset();' in main
    assert 'MENU PRESENT measured_intervals=' in counter and 'measured_fps=' in counter
    assert 'configuration, not measured clocks' in main
    # The existing upscale path puts UI on native presentation target.
    upscale=renderer[renderer.index('if (scene_target.upscale) {',renderer.index('bool record_frame(')):renderer.index('mark_frontend(9);')]
    assert upscale.index('cmd.bindRenderTargets(&present_view, nullptr);')<upscale.index('emit_overlay(slot,frontend->vertices.data()')

p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);root=p.parse_args().root
r=(root/'switch/deko_renderer.cpp').read_text();m=(root/'switch/race_main.cpp').read_text();check(root,r,m)
for name,bad_r,bad_m in [('profiling always on',r.replace('if(!frontend || !frontend_profile_passes)return;',''),m),('timestamp overlap',r.replace('slot*192+point*16','slot*32+point*16'),m),('power guard held into race',r,m.replace('frontend_gpu_profile.reset();','')),('UI target lowered',r.replace('cmd.bindRenderTargets(&present_view, nullptr);',''),m),('profile counter mixed in',r,m.replace('!profile_frame && frontend_presentation_stats.sample','frontend_presentation_stats.sample')),('reentry counter not reset',r,m.replace('frontend_presentation_stats.reset();','')),('extra counter GPU wait',r,m.replace('FrontendPresentationWindow presentation_window;','FrontendPresentationWindow presentation_window; frontend_renderer->wait();'))]:
    try:check(root,bad_r,bad_m)
    except (AssertionError,ValueError):print('PASS negative control: '+name)
    else:raise AssertionError('negative control accepted: '+name)
print('PASS bounded menu timestamp and power/target ownership source checks; not measured clocks or GPU execution')
