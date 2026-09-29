# The stock-car frame the Switch application builds, held to nfsu_viewer's
# on the same six files (issue #141).
#
# nfsu_stock_frame repeats the calls src/viewer_main.cpp makes for its
# pinned stock-car command instead of sharing code with it: the viewer is
# left unchanged, byte for byte, so its frames and reports are what they
# were. This test is what holds the two copies together. It writes the
# synthetic inputs nfsu_stock_frame_tests builds - every file in the layout
# the game uses, each parsed by the real parsers - then renders them twice:
# once with the viewer and the pinned command's options, once through the
# four calls the Switch application makes (nfsu_stock_frame_tests --frame).
# It requires:
#
#   - both runs to succeed, with nothing on standard error and the inputs
#     clean, and the frame to be more than an empty background;
#   - the viewer's TGA file and the in-memory encoding to have the same size
#     and the same SHA-256, the file's taken by CMake's own file(SHA256), an
#     implementation independent of nfsu_sha256;
#   - every render statistic and the stock slot summary the application's
#     path prints to appear, character for character, in the viewer's report.
#
# A comparison that cannot fail proves nothing, so the viewer is then run
# once more without --place-wheels, which draws a different assembly, and
# that frame must differ from the application's.
#
# Then the painted frame (issue #148): the viewer with the same command and
# the painted skin options, --skin-car 240SX --skin-doorline plain
# --skin-base 30 60 160 128, against nfsu_stock_frame_tests --painted-frame,
# the application's path with nfsu::stock_240sx_skin(). The same three
# requirements hold, the report lines now including the viewer's "skin:",
# "skin bound:" and "skin generated:" lines, and the painted frame must
# differ from the unpainted one, so that a skin that painted nothing could
# not pass.
#
# The work directory is this test's alone. It is removed whole before the
# run, so no file of an earlier run can stand in for this one's, and again
# after a pass; after a failure it is kept for inspection.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS VIEWER TESTS WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
if(EXISTS "${WORK}")
    message(FATAL_ERROR "${WORK} could not be removed before the run")
endif()
file(MAKE_DIRECTORY "${WORK}")
set(ROOT "${WORK}/root")
set(SHOT "${WORK}/viewer.tga")
set(UNPLACED "${WORK}/viewer_unplaced.tga")

execute_process(
    COMMAND "${TESTS}" --write-fixture "${ROOT}"
    RESULT_VARIABLE fixture_result
    OUTPUT_VARIABLE fixture_output
    ERROR_VARIABLE fixture_error)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "--write-fixture exited ${fixture_result}: ${fixture_error}")
endif()
foreach(input IN ITEMS Cars/240SX/GEOMETRY.BIN Cars/240SX/TEXTURES.BIN
                       Global/GLOBALB.BUN Cars/TEXTURES.BIN
                       Cars/BRAKES/GEOMETRY.BIN Cars/PLATES/GEOMETRY.BIN)
    if(NOT EXISTS "${ROOT}/${input}")
        message(FATAL_ERROR "--write-fixture did not write ${input}")
    endif()
endforeach()

# The command docs/RENDERER.md pins for the retail 240SX, over the fixture.
set(viewer_inputs
    "${ROOT}/Cars/240SX/GEOMETRY.BIN" "${ROOT}/Cars/240SX/TEXTURES.BIN")
set(viewer_options
    --shared-textures "${ROOT}/Global/GLOBALB.BUN"
    --shared-textures "${ROOT}/Cars/TEXTURES.BIN"
    --stock-car 240SX --stock-parts "${ROOT}/Global/GLOBALB.BUN"
    --stock-geometry "${ROOT}/Cars/BRAKES/GEOMETRY.BIN"
    --stock-geometry "${ROOT}/Cars/PLATES/GEOMETRY.BIN")
execute_process(
    COMMAND "${VIEWER}" ${viewer_inputs} "${SHOT}" ${viewer_options} --place-wheels
            --material-state off
    RESULT_VARIABLE viewer_result
    OUTPUT_VARIABLE viewer_output
    ERROR_VARIABLE viewer_error)
string(REPLACE "\r\n" "\n" viewer_output "${viewer_output}")
if(NOT viewer_result EQUAL 0 OR NOT viewer_error STREQUAL "")
    message(FATAL_ERROR
        "the viewer exited ${viewer_result}; its standard error:\n${viewer_error}")
endif()
if(NOT EXISTS "${SHOT}")
    message(FATAL_ERROR "the viewer wrote no frame")
endif()
file(SIZE "${SHOT}" viewer_size)
file(SHA256 "${SHOT}" viewer_hash)
string(TOUPPER "${viewer_hash}" viewer_hash)

# Stock viewing defaults to retail alpha state, including the tire cutout.
set(DEFAULT_RETAIL "${WORK}/viewer_default_retail.tga")
set(EXPLICIT_RETAIL "${WORK}/viewer_explicit_retail.tga")
execute_process(
    COMMAND "${VIEWER}" ${viewer_inputs} "${DEFAULT_RETAIL}" ${viewer_options}
            --place-wheels
    RESULT_VARIABLE default_retail_result)
execute_process(
    COMMAND "${VIEWER}" ${viewer_inputs} "${EXPLICIT_RETAIL}" ${viewer_options}
            --place-wheels --material-state retail
    RESULT_VARIABLE explicit_retail_result)
if(NOT default_retail_result EQUAL 0 OR NOT explicit_retail_result EQUAL 0)
    message(FATAL_ERROR "the stock retail default could not render")
endif()
file(SHA256 "${DEFAULT_RETAIL}" default_retail_hash)
file(SHA256 "${EXPLICIT_RETAIL}" explicit_retail_hash)
if(NOT default_retail_hash STREQUAL explicit_retail_hash OR
   default_retail_hash STREQUAL viewer_hash)
    message(FATAL_ERROR "stock default must equal retail and differ from off")
endif()

execute_process(
    COMMAND "${TESTS}" --frame "${ROOT}"
    RESULT_VARIABLE frame_result
    OUTPUT_VARIABLE frame_output
    ERROR_VARIABLE frame_error)
string(REPLACE "\r\n" "\n" frame_output "${frame_output}")
if(NOT frame_result EQUAL 0 OR NOT frame_error STREQUAL "")
    message(FATAL_ERROR
        "--frame exited ${frame_result}:\n${frame_output}\n${frame_error}")
endif()
if(NOT frame_output MATCHES "\nsha256 ([0-9A-F]+)\n")
    message(FATAL_ERROR "--frame printed no sha256 line:\n${frame_output}")
endif()
set(frame_hash "${CMAKE_MATCH_1}")
if(NOT frame_output MATCHES "\ntga_bytes ([0-9]+)\n")
    message(FATAL_ERROR "--frame printed no tga_bytes line:\n${frame_output}")
endif()
set(frame_size "${CMAKE_MATCH_1}")
if(NOT frame_output MATCHES "\ninputs_ok 1\n")
    message(FATAL_ERROR "--frame found issues with the synthetic inputs:\n${frame_output}")
endif()

# Not an empty background: the fixture draws every kind of placement.
if(NOT viewer_output MATCHES "\npixels drawn: ([0-9]+) ")
    message(FATAL_ERROR "the viewer printed no pixel count:\n${viewer_output}")
endif()
if(CMAKE_MATCH_1 LESS 20000)
    message(FATAL_ERROR "the viewer's frame is nearly empty:\n${viewer_output}")
endif()
if(NOT viewer_output MATCHES "\ninstances: 14 exact 6 stand_in 8\n")
    message(FATAL_ERROR "the viewer did not draw the fixture's 14 copies:\n${viewer_output}")
endif()

if(NOT viewer_size EQUAL frame_size)
    message(FATAL_ERROR "the viewer wrote ${viewer_size} bytes, the application's path "
                        "encoded ${frame_size}")
endif()
if(NOT viewer_hash STREQUAL frame_hash)
    message(FATAL_ERROR
        "the frames differ:\n  viewer      ${viewer_hash}\n  application ${frame_hash}")
endif()

string(REGEX MATCHALL
    "(stock slots|meshes|material groups|unresolved hashes|triangles submitted|triangles bad_index|triangles outside|triangles front|pixel tests|fragments|pixels drawn|drawn bounds|instances): [^\n]*"
    frame_lines "${frame_output}")
list(LENGTH frame_lines frame_line_count)
if(NOT frame_line_count EQUAL 13)
    message(FATAL_ERROR "--frame printed ${frame_line_count} report lines, not 13:\n${frame_output}")
endif()
foreach(line IN LISTS frame_lines)
    string(FIND "\n${viewer_output}" "\n${line}\n" at)
    if(at EQUAL -1)
        message(FATAL_ERROR
            "the application's path reports \"${line}\", which the viewer does not:\n"
            "${viewer_output}")
    endif()
endforeach()

# The comparison has to be able to fail.
execute_process(
    COMMAND "${VIEWER}" ${viewer_inputs} "${UNPLACED}" ${viewer_options}
    RESULT_VARIABLE unplaced_result
    OUTPUT_VARIABLE unplaced_output
    ERROR_VARIABLE unplaced_error)
if(NOT unplaced_result EQUAL 0 OR NOT EXISTS "${UNPLACED}")
    message(FATAL_ERROR
        "the viewer without --place-wheels exited ${unplaced_result}: ${unplaced_error}")
endif()
file(SHA256 "${UNPLACED}" unplaced_hash)
string(TOUPPER "${unplaced_hash}" unplaced_hash)
if(unplaced_hash STREQUAL frame_hash)
    message(FATAL_ERROR
        "the frame without the wheels hashes the same as the frame with them, so the "
        "comparison above could not have told two assemblies apart")
endif()

# The painted frame (issue #148).
set(PAINTED "${WORK}/viewer_painted.tga")
execute_process(
    COMMAND "${VIEWER}" ${viewer_inputs} "${PAINTED}" ${viewer_options} --place-wheels
            --material-state off
            --skin-car 240SX --skin-doorline plain --skin-base 30 60 160 128
    RESULT_VARIABLE painted_result
    OUTPUT_VARIABLE painted_output
    ERROR_VARIABLE painted_error)
string(REPLACE "\r\n" "\n" painted_output "${painted_output}")
if(NOT painted_result EQUAL 0 OR NOT painted_error STREQUAL "")
    message(FATAL_ERROR
        "the painted viewer exited ${painted_result}; its standard error:\n${painted_error}")
endif()
if(NOT EXISTS "${PAINTED}")
    message(FATAL_ERROR "the painted viewer wrote no frame")
endif()
file(SIZE "${PAINTED}" painted_size)
file(SHA256 "${PAINTED}" painted_hash)
string(TOUPPER "${painted_hash}" painted_hash)

execute_process(
    COMMAND "${TESTS}" --painted-frame "${ROOT}"
    RESULT_VARIABLE app_result
    OUTPUT_VARIABLE app_output
    ERROR_VARIABLE app_error)
string(REPLACE "\r\n" "\n" app_output "${app_output}")
if(NOT app_result EQUAL 0 OR NOT app_error STREQUAL "")
    message(FATAL_ERROR
        "--painted-frame exited ${app_result}:\n${app_output}\n${app_error}")
endif()
if(NOT app_output MATCHES "\nsha256 ([0-9A-F]+)\n")
    message(FATAL_ERROR "--painted-frame printed no sha256 line:\n${app_output}")
endif()
set(app_hash "${CMAKE_MATCH_1}")
if(NOT app_output MATCHES "\ntga_bytes ([0-9]+)\n")
    message(FATAL_ERROR "--painted-frame printed no tga_bytes line:\n${app_output}")
endif()
set(app_size "${CMAKE_MATCH_1}")
if(NOT app_output MATCHES "\ninputs_ok 1\n" OR NOT app_output MATCHES "\nview_default 1\n")
    message(FATAL_ERROR
        "--painted-frame found issues or rendered away from the default camera:\n"
        "${app_output}")
endif()

if(NOT painted_size EQUAL app_size)
    message(FATAL_ERROR "the painted viewer wrote ${painted_size} bytes, the application's "
                        "path encoded ${app_size}")
endif()
if(NOT painted_hash STREQUAL app_hash)
    message(FATAL_ERROR
        "the painted frames differ:\n  viewer      ${painted_hash}\n"
        "  application ${app_hash}")
endif()

string(REGEX MATCHALL
    "(stock slots|meshes|material groups|unresolved hashes|triangles submitted|triangles bad_index|triangles outside|triangles front|pixel tests|fragments|pixels drawn|drawn bounds|instances|skin|skin bound|skin generated): [^\n]*"
    app_lines "${app_output}")
list(LENGTH app_lines app_line_count)
if(NOT app_line_count EQUAL 17)
    message(FATAL_ERROR
        "--painted-frame printed ${app_line_count} report lines, not 17:\n${app_output}")
endif()
foreach(line IN LISTS app_lines)
    string(FIND "\n${painted_output}" "\n${line}\n" at)
    if(at EQUAL -1)
        message(FATAL_ERROR
            "the application's path reports \"${line}\", which the painted viewer does "
            "not:\n${painted_output}")
    endif()
endforeach()

# A skin that painted nothing would pass everything above.
if(app_hash STREQUAL frame_hash)
    message(FATAL_ERROR
        "the painted frame hashes the same as the unpainted one, so the skin changed "
        "nothing on screen")
endif()

# The summary, only once every check above has run.
message(STATUS
    "stock frame: the viewer and the application's path agree on ${frame_size} bytes, "
    "SHA-256 ${frame_hash}, and on all 13 report lines; without --place-wheels the "
    "viewer's frame is ${unplaced_hash}")
message(STATUS
    "painted frame: the viewer and the application's path agree on ${app_size} bytes, "
    "SHA-256 ${app_hash}, and on all 17 report lines; the unpainted frame is "
    "${frame_hash}")
file(REMOVE_RECURSE "${WORK}")
