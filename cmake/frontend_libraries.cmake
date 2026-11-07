# Save-slot publication owns directory creation and file writes. Domain and
# frontend rendering remain portable and continue through the neutrality gate.
add_library(nfsu_career_save src/career_save.cpp)
target_link_libraries(nfsu_career_save PUBLIC nfsu_core nfsu_sha256)
nfsu_enable_warnings(nfsu_career_save)
add_library(nfsu_frontend src/frontend.cpp src/frontend_scene.cpp src/frontend_preset.cpp src/frontend_motion.cpp src/freeride_skyline.cpp src/intro_grid.cpp src/traffic_model.cpp src/career.cpp src/career_magazine.cpp src/career_tuning.cpp src/career_ride.cpp src/career_garage.cpp)
target_link_libraries(nfsu_frontend PUBLIC nfsu_career_save nfsu_race_events nfsu_screen_text nfsu_sha256 nfsu_race_hud nfsu_stock_frame nfsu_track nfsu_race_run nfsu_event0_setup)
target_compile_features(nfsu_frontend PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_frontend)

add_library(nfsu_frontend_music src/frontend_music.cpp)
target_link_libraries(nfsu_frontend_music PUBLIC nfsu_audio_playback nfsu_audio_output nfsu_sha256 nfsu_frontend_sfx)
target_compile_features(nfsu_frontend_music PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_frontend_music)
