# The whole track one race loads (issue #156): the streaming table of a
# track's TRACKB file, the recovered rule that picks which of its sections
# speed.exe loads, the bounded read of exactly those byte ranges, and their
# merge into one geometry report and one scenery report that the existing
# scene layer places as one scene. It links the zone decoder because the
# rule reads two of the zone file's lists, nfsu_texture_decode for the
# per-section texture sources, and nfsu_gpu_upload for the budgets of issue
# #52. It reads its files with std::ifstream, as nfsu_core's loaders do,
# reads no clock and makes no decision from a floating-point result - it
# only takes the minimum and maximum of bounds the parsers computed. It
# still inherits the global floating-point policy. See docs/TRACK_WHOLE.md.
add_library(nfsu_track_stream
    src/track_stream.cpp
)

target_link_libraries(nfsu_track_stream
    PUBLIC nfsu_track_zones nfsu_texture_decode nfsu_gpu_upload)
nfsu_enable_warnings(nfsu_track_stream)


# The bounded hardware slice shares asset checks, preparation, culling and
# contact queries with the desktop acceptance executable.
add_library(nfsu_track_slice src/track_slice.cpp)
target_link_libraries(nfsu_track_slice PUBLIC nfsu_track_stream nfsu_track_residency
    nfsu_track_contact nfsu_draw_commands nfsu_assets nfsu_race_events nfsu_race_route)
nfsu_enable_warnings(nfsu_track_slice)
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_track_stream nfsu_track_slice PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()
