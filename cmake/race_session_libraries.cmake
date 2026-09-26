add_library(nfsu_race_session src/race_session.cpp)
target_link_libraries(nfsu_race_session PUBLIC nfsu_drive_session nfsu_vehicle_track nfsu_race_timing)
target_compile_features(nfsu_race_session PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_race_session)
# Inherits cmake/floating_point.cmake; the whole-tree Switch census includes
# this neutral library even though no console application links it yet.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_race_session PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
