# What `nfsu_probe track-whole` refuses, prints and exits with.
#
# The first half needs NO INPUT FILES AT ALL: the argument count, the track
# number, every option refusal, the render options that need --out, and
# two paths that do not exist. The second half drives the command over the
# synthetic stream nfsu_track_stream_tests writes on request - a TRACKB
# image carrying a streaming table and a zone container, a stream of eight
# sections at 2048-byte boundaries with non-chunk filler between them, and
# a separate texture pack - which is the only way to reach the per-section
# listing, the totals, the budget rows and a rendered frame without retail
# data. The fixture is the same bytes the unit tests parse, so every number
# pinned below is one those tests assert.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
#
# Run from the build directory. PROBE is the built program and TESTS the
# built nfsu_track_stream_tests.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE OR NOT DEFINED TESTS)
    message(FATAL_ERROR "PROBE and TESTS must both be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_whole_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(failures 0)
set(cases 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case. `code` is the exact exit status required. `needles` is a list of
# texts the combined output must each contain literally, and `banned` a list
# it must not contain; both are semicolon-separated CMake lists. Everything
# after them is the command line after "track-whole". The standard output,
# with CR LF folded to LF, is left in ${last_stdout}.
function(nfsu_case label code needles banned)
    math(EXPR next_case "${cases}+1")
    set(cases "${next_case}" PARENT_SCOPE)
    execute_process(
        COMMAND "${PROBE}" track-whole ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
    set(last_stdout "${stdout_text}" PARENT_SCOPE)
    set(text "${stdout_text}${stderr_text}")
    if(text MATCHES "terminate called")
        nfsu_fail("${label}: the run terminated on an exception:\n${text}")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    if(NOT status MATCHES "^[0-9]+$" OR status GREATER 128)
        nfsu_fail("${label}: exit status ${status} is a crash, not a refusal")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    if(NOT status EQUAL ${code})
        nfsu_fail("${label}: exit status ${status}, expected ${code}\n${text}")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    foreach(needle IN LISTS needles)
        string(FIND "${text}" "${needle}" found)
        if(found LESS 0)
            nfsu_fail("${label}: expected the output to contain\n  ${needle}\nand it reads\n${text}")
        endif()
    endforeach()
    foreach(needle IN LISTS banned)
        string(FIND "${text}" "${needle}" found)
        if(NOT found LESS 0)
            nfsu_fail("${label}: the output must NOT contain\n  ${needle}\nand it reads\n${text}")
        endif()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

set(usage_line "nfsu_probe track-whole <STREAM.BUN> <TRACKB####.lzc> <track-number>")
set(missing_stream "${work}/missing_stream.bun")
set(missing_trackb "${work}/missing_trackb.lzc")
# What only a run that got past its argument checks prints.
set(ran "stream: ;trackb: ;table: ")

# ---------------------------------------------------------------------------
# No input files
# ---------------------------------------------------------------------------

nfsu_case("two arguments are a usage error" 1 "${usage_line}" "${ran}"
          "${missing_stream}" "${missing_trackb}")
nfsu_case("a track number that is not a number" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" abc)
nfsu_case("track number 0" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" 0)
nfsu_case("track number 32768" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" 32768)

# Both files missing: the zone file and the table report it, nothing is
# selected, and the run exits 3 before any section is read.
nfsu_case("two missing files" 3
          "zone issue at 0: cannot size the file;table: not valid\; no section is selected;table issue at 0: expected exactly one chunk 0x00034107, found 0"
          "set: ;files: ;budget: "
          "${missing_stream}" "${missing_trackb}" 32767)
nfsu_case("track number 1 is accepted" 3 "table: not valid" ""
          "${missing_stream}" "${missing_trackb}" 1)
nfsu_case("an unknown option" 1 "error: unknown option --bogus" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --bogus)
nfsu_case("an option given twice" 1 "error: --set is given more than once" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --set permanent --set zones)
nfsu_case("--texture may repeat" 3 "table: not valid" ""
          "${missing_stream}" "${missing_trackb}" 1003 --texture a.bin --texture b.bin)
nfsu_case("--set with a wrong word" 1
          "error: --set must be permanent, zones or stream" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --set whole)
foreach(word permanent zones stream)
    nfsu_case("--set ${word} is accepted" 3 "table: not valid" ""
              "${missing_stream}" "${missing_trackb}" 1003 --set ${word})
endforeach()
nfsu_case("--set with no value" 1 "error: --set must be" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --set)
nfsu_case("--pack with no value" 1 "error: --pack needs a path" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --pack)
nfsu_case("--texture with no value" 1 "error: --texture needs a path" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --texture)
nfsu_case("--out with no value" 1 "error: --out needs a path" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out)
nfsu_case("--lod-slot 6" 1 "error: --lod-slot needs a slot from 0 to 5" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --lod-slot 6)
nfsu_case("--lod-slot 5 is accepted" 3 "table: not valid" ""
          "${missing_stream}" "${missing_trackb}" 1003 --lod-slot 5)
nfsu_case("--list with a word" 1 "error: --list needs a count" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --list many)

# Every render option needs --out: it would otherwise be read by nothing.
foreach(view --width --height --view-yaw --view-pitch --view-fov --view-margin
             --view-distance --view-near --view-far)
    nfsu_case("${view} without --out" 1
              "error: ${view} describes the render, which needs --out" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${view} 2)
endforeach()
nfsu_case("--view-target without --out" 1
          "error: --view-target describes the render, which needs --out" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --view-target 1 2 3)

set(out "${work}/never.tga")
nfsu_case("--width 0" 1 "error: --width needs a size from 1 to 8192" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --width 0)
nfsu_case("--height 8193" 1 "error: --height needs a size from 1 to 8192" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --height 8193)
nfsu_case("--view-pitch 90" 1 "error: --view-pitch needs a number from -89 to 89"
          "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-pitch 90)
nfsu_case("--view-fov 0.5" 1 "error: --view-fov needs a number from 1 to 170" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-fov 0.5)
nfsu_case("--view-margin 0.99" 1 "error: --view-margin needs a number from 1 to 100"
          "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-margin 0.99)
nfsu_case("--view-yaw 36001" 1
          "error: --view-yaw needs a number from -36000 to 36000" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-yaw 36001)
nfsu_case("--view-distance 0.001" 1
          "error: --view-distance needs a number from 0.01 to 10000" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}"
          --view-distance 0.001)
nfsu_case("--view-near 11" 1 "error: --view-near needs a number from 1e-04 to 10"
          "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-near 11)
nfsu_case("--view-far 100001" 1
          "error: --view-far needs a number from 1e-04 to 1e+05" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-far 100001)
nfsu_case("--view-target out of range" 1
          "error: --view-target needs three numbers from -10000 to 10000" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}"
          --view-target 0 10001 0)
nfsu_case("--view-target with two numbers" 1
          "error: --view-target needs three numbers from -10000 to 10000" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --view-target 0 0)
# The accepting side of each bound, which runs on to the missing files.
nfsu_case("the render bounds at their limits" 3 "table: not valid" ""
          "${missing_stream}" "${missing_trackb}" 1003 --out "${out}" --width 8192
          --height 1 --view-pitch 89 --view-fov 170 --view-margin 100 --view-yaw -36000
          --view-distance 10000 --view-near 10 --view-far 100000
          --view-target -10000 10000 0)
nfsu_case("--out naming an input" 1 "error: --out must differ from every input file"
          "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --out "${missing_trackb}")
if(EXISTS "${out}")
    nfsu_fail("a refused run wrote ${out}")
endif()

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(trackb "${work}/fixture_trackb.lzc")
set(stream "${work}/fixture_stream.bun")
set(texture "${work}/fixture_texture.bin")
file(REMOVE "${trackb}" "${stream}" "${texture}")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${trackb}" "${stream}" "${texture}"
    OUTPUT_VARIABLE fixture_text
    ERROR_VARIABLE fixture_error
    RESULT_VARIABLE fixture_status)
if(NOT fixture_status EQUAL 0 OR NOT EXISTS "${trackb}" OR NOT EXISTS "${stream}"
   OR NOT EXISTS "${texture}")
    message(FATAL_ERROR
        "the fixture could not be written: ${fixture_status}\n"
        "${fixture_text}${fixture_error}")
endif()

# The permanent set: the four fixed sections, Z3 from the extra list keyed
# on 1003, and J3 from its track byte, in that order. Every number here is
# one tests/track_stream_tests.cpp asserts for the same bytes.
nfsu_case("the permanent set" 0
          "trackb: fixture_trackb.lzc packed 1820 image 1820\n;table: offset 40 records 9 stream 8 pack 0 other 1 track-flagged 1\n;table: file sizes 64 16384 0 at 552\n;table: streaming pool 2101088 (0x00444AF0 plus 2097152)\n;track: 1003 extra list record 1: 2603\n;set: permanent requests 6 selected 6 without record 0 repeated 0 excluded 0 stopped at limit no\n;route: zones 1 missing 0 section numbers 3 with a record 1 outside the set 0\n;files: stream size 16384 pack none read 5888\n;texture source 1: fixture_texture.bin size 896 packs 1 complete 1\n;textures: sources 2 from sections 1 wanted 3 decoded 3 bytes 48 shadowed 1 unusable claims 0 blocked 0 not wanted 0\n;texture source 1: retained 1 bytes 16\n;sections: listed 6 of 6\n;section 0 record 1 name Y0 number 2500 reason fixed file 1 offset 0 size 912 resident 40 loaded yes\n;section 4 record 5 name Z3 number 2603 reason extra-list file 1 offset 8192 size 1080 resident 1080 loaded yes\n;section 5 record 6 name J3 number 1003 reason track-flag file 1 offset 10240 size 1336 resident 1336 loaded yes\n  chunks 4 roots 1 scenery 1 packs 0 padding 2 other 0\n  meshes 1 valid 1 empty 0 refused 0 vertices 3 triangles 1 scenery sections 1 instances 4 placed 3 pack records 0 textures 0 texture bytes 0\n;totals: sections 6 loaded 6 bytes read 5888 resident declared 5016\n;totals: scenery sections 4 valid 4 objects 8 instances 8 placed 7 unplaced empty slot 0 skipped hash 0 unresolved 1 distinct 1\n;totals: vertex buffer bytes 432 index buffer bytes 36 texture bytes 48 instance bytes 1008 resident bytes 1524\n;draw list: order material group entries 7 triangles 9 groups 4 meshes 4 texture binds 3\n;unplaced: listed 1 of 1 mesh hashes\n  unplaced 0x00002999 instances 1\n;unresolved: listed 0 of 0 texture hashes\n;gpu: buffers 468 extent 474 images 48 extent 80 staging largest 16 (linear layout, a lower bound)\n;budget: switch streaming.region 1524 of 16777216 within\n;budget: rows 12 over 0\n;world: drawn x 10 .. 32 y 0.25 .. 5.25 z -5.5 .. -0.5 points 27\n"
          "issue;OVER;render: ;wrote: "
          "${stream}" "${trackb}" 1003 --texture "${texture}")

# The whole listing, pinned: a line reworded, reordered or dropped anywhere
# changes this, where the needles above each watch one line. The listing
# names its files without their directories, so it is the same in every
# build directory.
string(SHA256 listing_hash "${last_stdout}")
string(LENGTH "${last_stdout}" listing_length)
set(expected_hash "36ecbf575e00d02ed5c828a530c24b09eda6a5a7757625e04c60d190f8701673")
set(expected_length 4253)
if(NOT listing_hash STREQUAL expected_hash OR NOT listing_length EQUAL expected_length)
    nfsu_fail("the fixture listing is ${listing_length} bytes with SHA-256 ${listing_hash}\; expected ${expected_length} bytes with ${expected_hash}")
endif()

nfsu_case("--list 2" 0 "sections: listed 2 of 6\n;section 1 record 2 name X0"
          "section 2 record" "${stream}" "${trackb}" 1003 --list 2)
nfsu_case("--list 0" 0 "sections: listed 0 of 6\n;totals: sections 6" "section 0 record"
          "${stream}" "${trackb}" 1003 --list 0)
# Without the texture file T3 is unresolved, and no placed instance draws
# SIGN, which is the only mesh that names it.
nfsu_case("no texture file" 0
          "textures: sources 1 from sections 1 wanted 3 decoded 2 bytes 32;unresolved: listed 1 of 1 texture hashes\n  unresolved 0xAAAA0003 entries 0\n"
          "texture source 1:" "${stream}" "${trackb}" 1003 --list 0)
nfsu_case("the zones set" 0
          "set: zones requests 11 selected 7 without record 2 repeated 2 excluded 0 stopped at limit no;section 6 record 7 name J9 number 1009 reason zone-list"
          "W1" "${stream}" "${trackb}" 1003 --set zones)
nfsu_case("the stream set" 3
          "set: stream requests 8 selected 8 without record 0 repeated 0 excluded 1 stopped at limit no;section 7 record 8 name W1 number 2301 reason stream-bound;stride of 60 bytes;refused 1"
          "" "${stream}" "${trackb}" 1003 --set stream --list 8)
nfsu_case("a track no list is keyed on" 0
          "track: 1999 extra list none;route: no 0x00034153 record is keyed on 1999;set: permanent requests 5 selected 5"
          "Z3" "${stream}" "${trackb}" 1999)
nfsu_case("another track's lists" 0
          "track: 1004 extra list record 0: 2604;route: zones 1 missing 0 section numbers 2 with a record 2 outside the set 1;without record 1"
          "" "${stream}" "${trackb}" 1004 --list 0)
nfsu_case("a pack that does not exist" 3
          "stream issue at 0: file index 2: cannot size the file;files: stream size 16384 pack 0 read 5888"
          "" "${stream}" "${trackb}" 1003 --pack "${work}/missing_pack.bun" --list 0)
nfsu_case("the stream missing, the table present" 3
          "stream issue at 0: file index 1: cannot size the file;section issue at 0: section 'Y0' lives in file index 1, which this load was not given;files: stream size 0 pack none read 0;loaded no"
          "" "${missing_stream}" "${trackb}" 1003)

# A frame: the only cases that write, into this script's own directory.
set(frame "${work}/whole.tga")
file(REMOVE "${frame}" "${frame}.partial")
nfsu_case("the whole track rendered" 0
          "render: size 64x32 camera framed;render: entries 7 drawn 7 resolved 7 unresolved 0 unusable 0 binds 3;wrote: "
          "" "${stream}" "${trackb}" 1003 --texture "${texture}" --list 0 --out "${frame}"
          --width 64 --height 32)
if(NOT EXISTS "${frame}")
    nfsu_fail("the render case wrote no frame")
endif()
nfsu_case("the frame is never replaced" 1
          "already exists\; this command never replaces a file" "${ran}"
          "${stream}" "${trackb}" 1003 --out "${frame}")
set(position "${work}/position.tga")
nfsu_case("a view at a given position" 0
          "render: camera yaw 0 pitch 10 distance 30 target 20,2,-3 fov_y 60 near 0.5 far 3000;wrote: "
          "" "${stream}" "${trackb}" 1003 --list 0 --out "${position}" --width 64
          --height 32 --view-target 20 2 -3 --view-distance 30 --view-yaw 0
          --view-pitch 10 --view-fov 60 --view-near 0.5 --view-far 3000)
if(NOT EXISTS "${position}")
    nfsu_fail("the position case wrote no frame")
endif()

# --write-fixture never replaces a file.
set(protected "${work}/protected.bin")
file(WRITE "${protected}" "keep-me-untouched")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${work}/new_a.bin" "${protected}" "${work}/new_b.bin"
    OUTPUT_VARIABLE protect_text
    ERROR_VARIABLE protect_error
    RESULT_VARIABLE protect_status)
file(READ "${protected}" protected_after)
if(protect_status EQUAL 0 OR NOT protected_after STREQUAL "keep-me-untouched"
   OR EXISTS "${work}/new_a.bin" OR EXISTS "${work}/new_b.bin")
    nfsu_fail("--write-fixture over an existing file must refuse and write "
              "nothing: status ${protect_status}\n${protect_text}${protect_error}")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} track-whole guard failure(s) over ${cases} cases")
endif()
message(STATUS "track-whole guards: all ${cases} cases passed")
