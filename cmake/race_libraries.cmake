# All floating-point flags below are inherited from the project-wide rule
# in cmake/floating_point.cmake (issue #175); no target owns a separate pin.

# The race route of issue #61 and the zone decoder it links, declared once
# for both configures: the top-level CMakeLists.txt includes this file after
# cmake/car_libraries.cmake and cmake/draw_libraries.cmake, whose nfsu_core
# and nfsu_track_visibility these link, and before the desktop and Switch
# configures part, so the console compiles both from the same source lists
# with the same floating-point options as the desktop. #159 needs the lap
# logic on the Switch, and switch/CMakeLists.txt checks the console objects
# of nfsu_race_route for fused multiply-adds.

# The visibility zones of one track: the 0x80034150 records that turn a
# camera position into the section list the group layer draws. It sits above
# nfsu_track_visibility because a decoded zone is handed straight to that
# library's TrackResidency. Floating-point contraction is disabled for the
# same reason it is disabled there: the crossing test at speed.exe
# 0x005679B0 decides on a strict comparison of an interpolated coordinate,
# so a fused multiply-add would move a boundary case and the selected zone
# with it. See docs/TRACK_ZONES.md. (Moved here, as it was, from the
# top-level CMakeLists.txt by issue #61, so that the console builds it.)
add_library(nfsu_track_zones
    src/track_zones.cpp
)

target_link_libraries(nfsu_track_zones PUBLIC nfsu_track_visibility)
nfsu_enable_warnings(nfsu_track_zones)

# The route of one race over a TRACKB bundle (issue #61): the track path
# spline of chunk 0x80034147, the position markers of 0x00034146, the start,
# split and finish percentages projected from them, and the lap, crossing
# and wrong-way rules a fixed-step position sequence is replayed through. It
# links nfsu_core for the JDLZ decoder and nfsu_track_zones for the polygon
# test 0x005679B0, reads with std::ifstream as nfsu_core's loaders do and
# reads no clock. It calls no transcendental function: the one angle it
# needs is speed.exe's own table routine (0x00564DB0), so the desktop and
# the console agree on every verdict. Floating-point contraction is
# disabled for the reason nfsu_track_zones disables it: every lap and
# crossing verdict is a comparison of a float the spline arithmetic
# produced, and a fused multiply-add - which devkitA64's GCC emits at -O2
# unless told not to, and the desktop's x86-64 target has none of - would
# move it. See docs/RACE_ROUTE.md.
add_library(nfsu_race_route
    src/race_route.cpp
)

target_link_libraries(nfsu_race_route PUBLIC nfsu_core nfsu_track_zones)
nfsu_enable_warnings(nfsu_race_route)

# Recovered ordinary rival driver. Action planning and selector3 physics are
# separate owners; RaceSession selects it only for explicitly planned rivals.
add_library(nfsu_racing_line src/racing_line.cpp)
target_include_directories(nfsu_racing_line PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_racing_line PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_racing_line)
add_library(nfsu_ai_driver src/ai_driver.cpp src/ai_path.cpp src/ai_path_fitness.cpp src/ai_collision_prediction.cpp src/ai_path_choice.cpp src/ai_road.cpp src/ai_action.cpp src/ai_traffic_action.cpp src/ai_traffic_intersection.cpp src/ai_action_runtime.cpp src/ai_rival_action.cpp src/ai_neighbors.cpp src/ai_strategy.cpp src/ai_catchup.cpp src/ai_recovery.cpp)
target_link_libraries(nfsu_ai_driver PUBLIC nfsu_race_route nfsu_racing_line nfsu_vehicle)
nfsu_enable_warnings(nfsu_ai_driver)

# gnu++17 on the console, as for the car-path libraries; the comment at the
# end of cmake/car_libraries.cmake says why.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_track_zones nfsu_race_route nfsu_ai_driver nfsu_racing_line
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()

# The race state machine of issue #62: the stages from staging through the
# countdown to racing, the race clock of speed.exe's world timer, the split,
# lap and total times, the finish, the results, a restart and a quit, driven
# by fixed steps and by nfsu_race_route's progress events rather than by a
# car. It reads no file and no clock: real time reaches it only as the
# microseconds a caller reads from the platform's TimingService and hands to
# its step clock. Its float arithmetic is the executable's own - the
# hundredths rounding at 0x00586690, the countdown's comparisons at
# 0x00434D10 and the crossing backdate at 0x00433650 - so contraction is
# disabled for the reason it is for nfsu_race_route: a fused multiply-add
# would move a rounded time by a tick. switch/CMakeLists.txt checks its
# console object for fused instructions. See docs/RACE_TIMING.md.
add_library(nfsu_race_timing
    src/race_timing.cpp
)

target_link_libraries(nfsu_race_timing PUBLIC nfsu_race_route)
nfsu_enable_warnings(nfsu_race_timing)

# gnu++17 on the console, as for the two libraries above.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_race_timing
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
