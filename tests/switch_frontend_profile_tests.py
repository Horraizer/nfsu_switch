"""Bound opt-in menu timestamp storage and power/target ownership."""
from pathlib import Path
import argparse

def check(root,renderer,main):
    mark=renderer[renderer.index('const auto mark_frontend='):renderer.index('mark_frontend(0);')]
    assert 'if(!frontend || !frontend_profile_passes)return;' in mark
    assert 'slot*208+point*16' in mark
    for slot in range(2):
        addresses=[slot*208+point*16 for point in range(13)]
        assert all(a%16==0 and a+16<=416 for a in addresses)
    assert '!impl_->block(416,' in renderer
    assert 'mark_frontend(10);' in renderer and 'mark_frontend(11);' in renderer
    assert '+slot*26' in renderer and '+slot*208,0,208' in renderer
    assert 'frontend && frontend_profile_passes' in renderer[renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame(')):renderer.index('mark_frontend(8);')]
    for call in ('mark_frontend(0);','mark_frontend(face+1);','mark_frontend(7);','mark_frontend(8);','mark_frontend(9);'):
        assert call in renderer
    split=renderer[renderer.index('cmd.barrier(DkBarrier_Fragments, 0);',renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame('))):renderer.index('if(!checkpoint(previous_dynamic?',renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame(')))]
    assert 'if(frontend && frontend_profile_passes)' in split
    assert 'cmd.reportCounter(DkCounter_Timestamp,frontend_query_memory.getGpuAddr()+slot*208+12*16);' in split
    assert split.count('cmd.barrier(')==1
    assert 'fxaa_enabled=presentation && source.race_environment;' in renderer
    frame=renderer[renderer.index('bool record_frame('):renderer.index('bool present(std::string& error')]
    assert frame.index('if(fxaa_enabled) {')<frame.index('if (hud) emit_overlay(')
    assert frame.index('if (hud) emit_overlay(')<frame.index('query_memory.getGpuAddr()+slot*160+16')
    assert 'sprites_end<car_end || words[17]<sprites_end' in renderer
    assert 'main_sprites=%.3f upscale=%.3f' in renderer
    assert 'consume_frontend_query(slot);' in renderer
    assert 'frontend_query_pending[slot]=frontend && frontend_profile_passes;' in renderer
    startup=main[main.index('const auto intro=play_original_intro('):main.index('const auto run_career_action=[&]')]
    splash=startup[startup.index('const auto show_startup_splash='):startup.index('show_startup_splash(true);')]
    assert splash.index('screen.startup_splash(art);')<splash.index('music.start(')<splash.index('padGetButtonsDown(&pad)')
    assert startup.count('music.start(')==1 and 'if(start_music)' in splash
    assert startup.index('show_startup_splash(true);')<startup.index('while(!choose_profile(true))')
    assert startup.index('while(!choose_profile(true))')<startup.index('show_startup_splash(false);')
    assert startup.index('while(!choose_profile(true))')<startup.index('trace("profile.after")')
    assert startup.index('trace("profile.after")')<startup.index('FrontendAction pending=FrontendAction::none;')
    assert 'refresh_profile()' not in startup
    picker=startup[startup.index('const auto choose_profile='):startup.index('trace("profile.before")')]
    assert picker.index('art.record_profiles(')<picker.index('padGetButtonsDown(&pad)')
    start=main.index('const auto run_career_action=[&]')
    transition=main[start:main.index('\n                while (appletMainLoop()) {',start)]
    assert transition.index('frontend_renderer.reset();')<transition.index('frontend_gpu_profile.reset();')<transition.index('run(root,')
    assert transition.count('run(root,')==3
    close=transition[transition.index('const auto close_career='):transition.index('const auto prepare_garage=')]
    assert close.index('frontend_renderer->close(')<close.index('frontend_renderer.reset();')<close.index('frontend_gpu_profile.reset();')
    for start, call in (('}else if(accept&&row==Row::start)', 'result=run(root,'),
                        ('}else if(profile.page==CareerPage::briefing)', 'outcome=run(root,')):
        begin=transition.index(start)
        launch=transition[begin:transition.index(call,begin)]
        assert 'close_career();music.stop();' in launch
    intro=transition[transition.index('intro_result=run(root,'):transition.index('while(intro_result==RaceReturn::restart && appletMainLoop())')]
    assert 'selected_car,screen,pad,false,nullptr,0,&art,' in intro
    assert 'false,profile.name,' in intro and '&music);' in intro
    assert main.index('std::unique_ptr<HandheldGpuProfile> frontend_gpu_profile;')<main.index('std::unique_ptr<DekoRenderer> frontend_renderer;')
    assert 'constexpr std::uint32_t menu_height=720;' in main
    assert 'art.atlas(),frontend_error,menu_height,profile_frame && !ab_minimal,active_trace,frontend_scene.draw_capacity())' in main
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
for name,bad_r,bad_m in [('minimal menu pass profiling enabled',r,m.replace('menu_height,profile_frame && !ab_minimal,active_trace','menu_height,profile_frame,active_trace')),('FXAA applied to menu',r.replace('fxaa_enabled=presentation && source.race_environment;','fxaa_enabled=presentation;'),m),('profiling always on',r.replace('if(!frontend || !frontend_profile_passes)return;',''),m),('timestamp overlap',r.replace('slot*208+point*16','slot*32+point*16'),m),('extra sprite barrier',r.replace('cmd.reportCounter(DkCounter_Timestamp,frontend_query_memory.getGpuAddr()+slot*208+12*16);','cmd.barrier(DkBarrier_Fragments,0); cmd.reportCounter(DkCounter_Timestamp,frontend_query_memory.getGpuAddr()+slot*208+12*16);'),m),('power guard held into race',r,m.replace('frontend_gpu_profile.reset();','')),('menu scene height lowered',r,m.replace('menu_height=720;','menu_height=540;')),('UI target lowered',r.replace('cmd.bindRenderTargets(&present_view, nullptr);',''),m),('profile counter mixed in',r,m.replace('!profile_frame && frontend_presentation_stats.sample','frontend_presentation_stats.sample')),('reentry counter not reset',r,m.replace('frontend_presentation_stats.reset();','')),('extra counter GPU wait',r,m.replace('FrontendPresentationWindow presentation_window;','FrontendPresentationWindow presentation_window; frontend_renderer->wait();'))]:
    try:check(root,bad_r,bad_m)
    except (AssertionError,ValueError):print('PASS negative control: '+name)
    else:raise AssertionError('negative control accepted: '+name)
print('PASS bounded menu timestamp and power/target ownership source checks; not measured clocks or GPU execution')
