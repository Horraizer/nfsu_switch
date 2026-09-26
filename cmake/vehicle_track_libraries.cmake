# The vehicle on the track (issue #60), declared once for both configures:
# the top-level CMakeLists.txt includes this file before the desktop and
# Switch configures part, after cmake/draw_libraries.cmake (nfsu_track,
# nfsu_memory), cmake/race_libraries.cmake (nfsu_race_route) and
# cmake/vehicle_libraries.cmake (nfsu_vehicle), and after
# cmake/car_libraries.cmake (nfsu_det_sqrt), so the console compiles the
# step with collision from the same sources and options as the desktop.
#
# nfsu_track_contact is the whole track's collision and its bounded contact
# queries of issue #158 (docs/TRACK_COLLISION.md). It was declared in the
# desktop part of CMakeLists.txt, and is declared here, as it was, because
# the console now queries collision: the vehicle's step asks it for the
# ground under each wheel and for the track round the car's hull. Every
# answer turns on a strict comparison - a determinant against zero, a
# barycentric coordinate against 0 and 1, a slope against
# kTrackContactRoadMinUp - so a fused multiply-add would move a boundary
# case, and with it a contact.
add_library(nfsu_track_contact
    src/track_contact.cpp
)

target_link_libraries(nfsu_track_contact PUBLIC nfsu_track nfsu_memory)
# PRIVATE: track_contact.h exposes nothing of det_sqrt. sweep_of's one
# double-precision square root was std::sqrt, not correctly rounded at -O0
# (issue #165, the same MinGW libm defect issue #125 found in the renderer
# and issue #165 also fixed in the controller dead zone); it now calls
# det_sqrt through nfsu/det_sqrt.h, a project header, which keeps this
# library's own source neutral under the include rule in CMakeLists.txt.
# Its two float roots take det_sqrt too since issue #60.
target_link_libraries(nfsu_track_contact PRIVATE nfsu_det_sqrt)
nfsu_enable_warnings(nfsu_track_contact)

# nfsu_vehicle_track: the model of docs/VEHICLE.md on a track's collision -
# the ground its wheels stand on, its hull against the track, the resets -
# and the courses and scenarios docs/VEHICLE_TRACK.md pins. It reads a
# TRACKB bundle only through load_track_contact_world and
# load_race_route_track, with std::ifstream, and reads no clock.
#
# The project-wide rule disables contraction for the reason nfsu_vehicle's is:
# the telemetry of a scripted run on a course is pinned byte for byte and
# must be the same bits on the desktop and on the console. The Switch tree's
# nfsu_switch_fma_census checks this library's objects and
# nfsu_track_contact's.
add_library(nfsu_vehicle_track
    src/vehicle_track.cpp
    src/vehicle_track_course.cpp
)

target_link_libraries(nfsu_vehicle_track
    PUBLIC nfsu_vehicle nfsu_track_contact nfsu_race_route nfsu_sha256)
# PRIVATE, as for nfsu_track_contact: its float roots - a hull point's arm,
# a node's distance, a loop's chords - take det_sqrt's correctly rounded
# root, and vehicle_track.h exposes nothing of it.
target_link_libraries(nfsu_vehicle_track PRIVATE nfsu_det_sqrt)
nfsu_enable_warnings(nfsu_vehicle_track)

# gnu++17 on the console, as for the car-path libraries; the comment at the
# end of cmake/car_libraries.cmake says why it is named there.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_track_contact nfsu_vehicle_track
        PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
