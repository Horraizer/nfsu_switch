# Retail ABK/AST parsing and PCM decode use only the C++ standard library.
# Both desktop and Switch configure this SDK-free library from one source list.
add_library(nfsu_audio src/audio.cpp)
target_include_directories(nfsu_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_audio PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_audio)

# Playback consumes PCM from the format decoder; race simulation never links
# this library. Both desktop and Switch compile the same bounded mixer.
add_library(nfsu_audio_playback src/audio_playback.cpp)
target_link_libraries(nfsu_audio_playback PUBLIC nfsu_audio)
target_include_directories(nfsu_audio_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_audio_playback PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_audio_playback)

if(NINTENDO_SWITCH)
    set(NFSU_AUDIO_OUTPUT_SOURCE src/audio_output_switch.cpp)
elseif(WIN32)
    set(NFSU_AUDIO_OUTPUT_SOURCE src/audio_output_win.cpp)
else()
    set(NFSU_AUDIO_OUTPUT_SOURCE src/audio_output_null.cpp)
endif()
add_library(nfsu_audio_output ${NFSU_AUDIO_OUTPUT_SOURCE})
target_include_directories(nfsu_audio_output PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_audio_output PUBLIC cxx_std_17)
if(WIN32 AND NOT NINTENDO_SWITCH)
    target_link_libraries(nfsu_audio_output PRIVATE winmm)
endif()
nfsu_enable_warnings(nfsu_audio_output)

add_library(nfsu_race_audio src/race_audio.cpp src/collision_audio.cpp)
target_link_libraries(nfsu_race_audio PUBLIC nfsu_audio_playback
    nfsu_audio_output nfsu_race_session)
target_include_directories(nfsu_race_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_race_audio PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_race_audio)
