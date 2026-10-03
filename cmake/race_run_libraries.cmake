# Event metadata and the console race orchestration compile on both hosts.
add_library(nfsu_race_events src/race_events.cpp)
target_link_libraries(nfsu_race_events PUBLIC nfsu_core)
nfsu_enable_warnings(nfsu_race_events)
add_library(nfsu_event0_setup src/event0_setup.cpp)
target_link_libraries(nfsu_event0_setup PUBLIC nfsu_race_events nfsu_race_session nfsu_sha256)
nfsu_enable_warnings(nfsu_event0_setup)
add_library(nfsu_light_glows src/light_glows.cpp)
target_link_libraries(nfsu_light_glows PUBLIC nfsu_stock_frame nfsu_render)
nfsu_enable_warnings(nfsu_light_glows)
add_library(nfsu_race_run src/race_run.cpp src/race_gpu_scene.cpp src/car_shadow.cpp)
target_link_libraries(nfsu_race_run PUBLIC nfsu_race_session nfsu_race_events
    nfsu_track_slice nfsu_stock_frame nfsu_light_glows)
nfsu_enable_warnings(nfsu_race_run)
add_library(nfsu_race_hud src/race_hud.cpp src/race_lap_hud.cpp)
target_link_libraries(nfsu_race_hud PUBLIC nfsu_race_session nfsu_core
    nfsu_screen_text)
nfsu_enable_warnings(nfsu_race_hud)
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_event0_setup nfsu_race_events nfsu_light_glows nfsu_race_run
        nfsu_race_hud PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
