# Event metadata and the console race orchestration compile on both hosts.
add_library(nfsu_race_events src/race_events.cpp)
target_link_libraries(nfsu_race_events PUBLIC nfsu_core)
nfsu_enable_warnings(nfsu_race_events)
add_library(nfsu_race_run src/race_run.cpp src/race_gpu_scene.cpp)
target_link_libraries(nfsu_race_run PUBLIC nfsu_race_session nfsu_race_events
    nfsu_track_slice nfsu_stock_frame)
nfsu_enable_warnings(nfsu_race_run)
add_library(nfsu_race_hud src/race_hud.cpp)
target_link_libraries(nfsu_race_hud PUBLIC nfsu_race_session nfsu_core
    nfsu_screen_text)
nfsu_enable_warnings(nfsu_race_hud)
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_race_events nfsu_race_run nfsu_race_hud PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
