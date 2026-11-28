// Shared local/network Quick Race ending. Included inside lan_main's namespace.
// It owns no campaign catalog, save, reward queue or bank balance.
struct MultiplayerFinishInput {
    std::function<FrontendInput()> sample;
    std::function<void()> poll;
    bool restart=true;
};
std::future<std::unique_ptr<RaceLoadingArt>> prepare_multiplayer_loading(const std::filesystem::path& root) {
    return std::async(std::launch::async,[root] {
        auto art=std::make_unique<RaceLoadingArt>();std::string error;
        require(art->load(root,error)&&art->prepare_overlay(error),error);
        return art;
    });
}
PostRaceAction show_multiplayer_finish(const std::filesystem::path& root,LanScene& scene,
    const RaceSession& world,const std::array<std::string,2>& models,RaceGpuFrame& frame,
    DekoRenderer& renderer,audio::RaceAudioService& sound,const FrontendArt& source_art,
    const RaceResultStatistics& statistics,std::future<std::unique_ptr<RaceLoadingArt>>& loading_job,
    const MultiplayerFinishInput& controls,std::string& error) {
    auto loading=loading_job.valid()?loading_job.get():std::make_unique<RaceLoadingArt>();
    if(loading->atlas().rgba.empty())require(loading->load(root,error)&&loading->prepare_overlay(error),error);
    require(renderer.finish_split_presentation(error),error);
    require(renderer.set_frontend_overlay(loading->atlas(),error),error);
    RaceFinishLoadingClock phase_clock(race_presentation_clock(),armTicksToNs(armGetSystemTick()));
    FrontendUiFrame ui;
    loading->record(frame.commands.width,frame.commands.height,0,ui);
    require(renderer.present_frontend(ui,error),error);
    trace("multiplayer.post_loading","PC_Loading.fng; both humans terminal");
    FrontendArt art;std::string scene_error;bool prepared=false,staged=false;
    const unsigned variant=unsigned(armGetSystemTick()%3);
    auto preparing=std::async(std::launch::async,[&] {
        std::string load_error;require(source_art.prepare_post_race(art,load_error,true),load_error);
        return scene.load_finish(root,world,variant,scene_error);
    });
    audio::FrontendMusic music;std::future<bool> starting_music;
    bool tried_music=false,playing_music=false;
    const auto update_music=[&] {
        if(starting_music.valid()) {
            if(starting_music.wait_for(std::chrono::seconds(0))==std::future_status::ready)
                playing_music=starting_music.get();
            return;
        }
        if(tried_music||!sound.finish_music_drained())return;
        tried_music=true;
        starting_music=std::async(std::launch::async,[&] {
            sound.stop();std::string music_error;const bool playing=music.start(root,music_error);
            if(!playing)std::fprintf(stderr,"MULTIPLAYER_POST_MUSIC %s\n",music_error.c_str());
            return playing;
        });
    };
    double scene_seconds=0;
    const auto record_scene=[&] {
        if(!scene.finish_frame(world,scene_seconds,frame,scene_error)) {
            trace("multiplayer.post_scene_unavailable",scene_error.c_str());
            scene.clear_finish();staged=false;
            require(scene.finish_frame(world,scene_seconds,frame,error),error);
        }
        require(renderer.update(frame.commands,error),error);
    };
    sound.fade_music_for_finish();
    while(appletMainLoop()) {
        controls.poll();
        if(appletGetFocusState()!=AppletFocusState_InFocus){phase_clock.resume();continue;}
        controls.sample(); // Drain held finish-frame edges while input is blocked.
        phase_clock.sample(armTicksToNs(armGetSystemTick()));
        scene_seconds=double(phase_clock.original_ticks())/kRaceTicksPerSecond;
        if(!prepared&&preparing.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            staged=preparing.get();prepared=true;
            trace(staged?"multiplayer.post_scene":"multiplayer.post_scene_unavailable",scene_error.c_str());
        }
        if(prepared)record_scene();
        if(race_finish_loading_ready(phase_clock.original_ticks(),prepared))break;
        loading->record(frame.commands.width,frame.commands.height,scene_seconds,ui);
        require(renderer.present_frontend(ui,error),error);update_music();
    }
    if(!appletMainLoop())return PostRaceAction::leave;
    require(renderer.set_frontend_overlay(art.atlas(),error),error);loading.reset();
    std::array<std::string,4> car_models{{models[0],models[1],models[0],models[1]}},labels;
    require(load_race_car_labels(root,car_models,labels,error),error);
    const std::array<std::string,2> human_labels{{labels[0],labels[1]}};
    auto values=post_race_two_human_report(world,statistics.values(world,labels[0]),human_labels);
    const auto statistic_actors=post_race_two_human_order(world);
    PostRaceMenu menu(true,false,0,2,statistic_actors[0]==0?0:1);
    menu.allow_restart(controls.restart);menu.animate();
    FrontendClock clock;FrontendMenu song;
    const auto record_ui=[&] {
        art.record_post_race(frame.commands.width,frame.commands.height,menu,values,ui);
        if(playing_music) {
            const auto current=music.now_playing();song.song(current.artist,current.title,current.album);
            art.append_song_card(frame.commands.width,frame.commands.height,song,ui);
        }
    };
    const auto cue=[&](PostRaceCue action) {
        audio::FrontendSound sound_id;
        switch(action) {
        case PostRaceCue::up:sound_id=audio::FrontendSound::up;break;
        case PostRaceCue::down:sound_id=audio::FrontendSound::down;break;
        case PostRaceCue::left:sound_id=audio::FrontendSound::left;break;
        case PostRaceCue::right:sound_id=audio::FrontendSound::right;break;
        case PostRaceCue::select:sound_id=audio::FrontendSound::select;break;
        case PostRaceCue::back:sound_id=audio::FrontendSound::back;break;
        case PostRaceCue::dialog:sound_id=audio::FrontendSound::dialog;break;
        default:return;
        }
        if(playing_music)music.sound(sound_id);else sound.frontend_sound(sound_id);
    };
    // The very first uncovered presentation already owns the results table.
    // No clean-NIS preview and no Accept/Start is needed to reach it.
    controls.sample();record_scene();record_ui();
    require(renderer.present_frontend(ui,error),error);
    trace("multiplayer.post_results","joint Quick Race table; no campaign reward pages");
    clock.sample(armTicksToNs(armGetSystemTick()));
    while(appletMainLoop()) {
        controls.poll();update_music();
        if(appletGetFocusState()!=AppletFocusState_InFocus){clock.resume();continue;}
        const auto input=controls.sample();
        const double seconds=clock.sample(armTicksToNs(armGetSystemTick()))/1000000000.0;
        scene_seconds+=seconds;art.advance(float(seconds));
        const auto before_page=menu.page();const auto before_racer=menu.racer();
        auto action=menu.advance(seconds);
        if(action==PostRaceAction::none){action=menu.update(input);cue(menu.cue());if(menu.dialog_sound())cue(PostRaceCue::dialog);}
        if(menu.statistics()&&(before_page!=menu.page()||before_racer!=menu.racer())) {
            const auto actor=statistic_actors.at(menu.racer());
            const auto details=statistics.values(world,labels[actor],actor);
            for(auto it=values.begin();it!=values.end();) {
                const auto& key=it->first;
                if(key.rfind("Data",0)==0||key.rfind("LapNumber_",0)==0||key.rfind("AllLapTimes",0)==0||
                   key.rfind("PositionAfterEachLap",0)==0)it=values.erase(it);else ++it;
            }
            for(const auto& item:details)values[item.first]=item.second;
        }
        record_scene();record_ui();require(renderer.present_frontend(ui,error),error);
        if(action!=PostRaceAction::none) {
            trace("multiplayer.post_action",action==PostRaceAction::restart?"restart Quick Race":"continue");
            const auto deadline=micros()+1000000;
            while(appletMainLoop()&&micros()<deadline&&((starting_music.valid())||
                  (playing_music?music.sound_active():sound.frontend_sound_active()))) {
                controls.poll();update_music();record_ui();require(renderer.present_frontend(ui,error),error);
            }
            return action;
        }
    }
    return PostRaceAction::leave;
}
