# Decode UTF-8 path diagnostics explicitly instead of using the Windows
# console code page selected by AUTO.
# What `nfsu_probe track-stream-path` refuses, prints and exits with.
#
# The first half needs NO INPUT FILES AT ALL: the argument count, the track
# number, every option refusal and the pairs of options that need each
# other, a path file that is missing or malformed, and the built-in
# hand-given path over two game files that do not exist. The second half
# drives the command over the synthetic stream nfsu_track_stream_tests writes
# on request - a TRACKB image carrying a streaming table and two zones, 1003
# over slot 0 from 0 to 100 and 1009 from 100 to 200, and a stream of eight
# sections - along a path this script writes: out from zone 1003 into zone
# 1009 and back. That is the only way to reach the streamer's steps, the
# measured sets, the budget refusals and the frames without retail data.
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

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_stream_path_guards")
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
# after them is the command line after "track-stream-path". The standard
# output, with CR LF folded to LF, is left in ${last_stdout}.
function(nfsu_case label code needles banned)
    math(EXPR next_case "${cases}+1")
    set(cases "${next_case}" PARENT_SCOPE)
    execute_process(
        ENCODING UTF-8
        COMMAND "${PROBE}" track-stream-path ${ARGN}
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

set(usage_line "nfsu_probe track-stream-path <STREAM.BUN> <TRACKB####.lzc> <track-number>")
set(missing_stream "${work}/missing_stream.bun")
set(missing_trackb "${work}/missing_trackb.lzc")
set(missing_path "${work}/missing_path.txt")
# What only a run that got past its argument checks prints.
set(ran "stream: ;trackb: ;path: ")
set(hand --hand-path 1003)

# ---------------------------------------------------------------------------
# No input files
# ---------------------------------------------------------------------------

nfsu_case("two arguments are a usage error" 1 "${usage_line}" "${ran}"
          "${missing_stream}" "${missing_trackb}")
nfsu_case("a track number that is not a number" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" abc ${hand})
nfsu_case("track number 0" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" 0 ${hand})
nfsu_case("track number 32768" 1
          "error: track-number must be a track number from 1 to 32767" "${ran}"
          "${missing_stream}" "${missing_trackb}" 32768 ${hand})
nfsu_case("neither path option" 1
          "error: give exactly one of --path and --hand-path" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003)
nfsu_case("both path options" 1
          "error: give exactly one of --path and --hand-path" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --path "${missing_path}")
nfsu_case("a hand path it does not know" 1
          "error: --hand-path knows one path, 1003" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --hand-path 1004)
nfsu_case("--hand-path with no value" 1
          "error: --hand-path knows one path, 1003" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 --hand-path)
nfsu_case("the hand path on another track" 1
          "error: --hand-path 1003 is drawn for track 1003" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1004 ${hand})
nfsu_case("an unknown option" 1 "error: unknown option --bogus" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --bogus)
nfsu_case("an option given twice" 1 "error: --policy is given more than once" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --policy retail
          --policy retail)
nfsu_case("--hand-path given twice" 1 "error: --hand-path is given more than once"
          "${ran}" "${missing_stream}" "${missing_trackb}" 1003 ${hand} ${hand})
foreach(bad 0 -1 1000.5 abc 1e400)
    nfsu_case("--speed ${bad}" 1
              "error: --speed needs a number above 0 and at most 1000" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} --speed ${bad})
endforeach()
nfsu_case("--speed with no value" 1 "error: --speed needs a number" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --speed)
nfsu_case("--policy with a wrong word" 1 "error: --policy must be retail or zone-only"
          "${ran}" "${missing_stream}" "${missing_trackb}" 1003 ${hand} --policy lazy)
nfsu_case("--cost with a wrong word" 1 "error: --cost must be table or prepared"
          "${ran}" "${missing_stream}" "${missing_trackb}" 1003 ${hand} --cost bytes)
foreach(bad arena -1 12x)
    nfsu_case("--budget ${bad}" 1
              "error: --budget must be pool, switch-region, switch-arena or a byte count"
              "${ran}" "${missing_stream}" "${missing_trackb}" 1003 ${hand} --budget ${bad})
endforeach()
nfsu_case("--budget with no value" 1 "error: --budget must be" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --budget)
nfsu_case("--list with a wrong word" 1 "error: --list must be events, all or none"
          "${ran}" "${missing_stream}" "${missing_trackb}" 1003 ${hand} --list some)
foreach(bad 0 10000001 x)
    nfsu_case("--frame-every ${bad}" 1
              "error: --frame-every needs a sample count from 1 to 10000000" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} --frame-every ${bad}
              --out "${work}/never")
endforeach()
nfsu_case("--frame-every without --out" 1
          "error: --frame-every and --out are given together" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --frame-every 5)
nfsu_case("--out without --frame-every" 1
          "error: --frame-every and --out are given together" "${ran}"
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --out "${work}/never")
foreach(flag --width --height)
    nfsu_case("${flag} 0" 1 "error: ${flag} needs a size from 1 to 8192" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} ${flag} 0)
    nfsu_case("${flag} 8193" 1 "error: ${flag} needs a size from 1 to 8192" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} ${flag} 8193)
    nfsu_case("${flag} without --out" 1
              "error: --width and --height describe the frames, which need --out" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} ${flag} 8192)
endforeach()
foreach(flag --path --pack --texture --out)
    nfsu_case("${flag} with no value" 1 "error: ${flag} needs a path" "${ran}"
              "${missing_stream}" "${missing_trackb}" 1003 ${flag})
endforeach()

# Past the argument checks. The built-in path parses and samples before any
# game file is read; the missing TRACKB then stops the run with 3.
nfsu_case("the hand path over missing files" 3
          "zone issue at 0: cannot size the file;table: not valid\; nothing is streamed"
          "path: ;budget: ;step "
          "${missing_stream}" "${missing_trackb}" 1003 ${hand})
nfsu_case("every accepted word, over missing files" 3 "table: not valid" ""
          "${missing_stream}" "${missing_trackb}" 1003 ${hand} --speed 1000
          --policy zone-only --cost prepared --budget 0 --list all --texture a.bin
          --texture b.bin --pack c.bin)
foreach(word pool switch-region switch-arena 123456)
    nfsu_case("--budget ${word} is accepted" 3 "table: not valid" ""
              "${missing_stream}" "${missing_trackb}" 1003 ${hand} --budget ${word})
endforeach()
nfsu_case("a missing path file" 2 "error: cannot size the path file missing_path.txt"
          "stream: ;zones: " "${missing_stream}" "${missing_trackb}" 1003
          --path "${missing_path}")
file(WRITE "${work}/bad_path.txt" "0 0 0\n1 1\n")
nfsu_case("a malformed path file" 2 "error: path line 2 has 2 fields" "stream: ;zones: "
          "${missing_stream}" "${missing_trackb}" 1003 --path "${work}/bad_path.txt")
file(WRITE "${work}/short_path.txt" "# one vertex\n5 5 0\n")
nfsu_case("a path of one vertex" 2 "a path needs at least two vertices, and this one has 1"
          "stream: " "${missing_stream}" "${missing_trackb}" 1003
          --path "${work}/short_path.txt")

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(trackb "${work}/fixture_trackb.lzc")
set(stream "${work}/fixture_stream.bun")
set(texture "${work}/fixture_texture.bin")
execute_process(
    ENCODING UTF-8
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
# Out of zone 1003 into zone 1009 and back, 200 units at 60 a second: one
# unit a step, 201 samples.
set(path "${work}/out_and_back.txt")
file(WRITE "${path}" "# out and back\r\n50 50 0\r\n150 50 0\r\n50 50 0\r\n")
set(run "${stream}" "${trackb}" 1003 --path "${path}" --speed 60 --texture "${texture}")

# The retail rule. The start set is the fixed four, 2603 and the track byte's
# J3 (1003): three passes, a step between each, and a pass that finds
# nothing, so the car is placed on step 7. The first sample commits 1003 at
# once. The car's own point is in 1009 from sample 50 and the route names
# only 1003, so 1009 commits on the third sample in it, 52, and J9 (1009)
# loads. On the way back the look-ahead points reach 1003 at sample 106,
# while the car is still 44 units inside 1009, and 1003 commits on 108. J9 is
# no longer requested and stays: nothing needs its room.
nfsu_case("the retail rule over the fixture" 0
          "path: out_and_back.txt vertices 3 length 200;path: speed 60 steps per second 60 samples 201;route: 0x00034153 record 1 keyed on 1003 names 1003;cost: table (place the byte count read, hold the bytes kept);budget: pool 2101088 streamer 2101088;policy: retail;start: steps 7 loaded yes;step 0 start sample 0 key -1 zone -1 rebuild 1 placed 1 loads 2500,2400 releases - unplaced 0 over 0 capped 0 loaded 0 requested 6 resident 2 held 1376 set 0 resident bytes 266;step 4 start sample 0 key -1 zone -1 rebuild 0 placed 1 loads 2603,1003 releases - unplaced 0 over 0 capped 0 loaded 0 requested 6 resident 6 held 5016 set 2 resident bytes 1524;step 7 path sample 0 key 1003 zone 1003 rebuild 1 placed 1 loads - releases - unplaced 0 over 0 capped 0 loaded 1 requested 6 resident 6 held 5016 set 2 resident bytes 1524;step 59 path sample 52 key 1009 zone 1009 rebuild 1 placed 1 loads 1009 releases - unplaced 0 over 0 capped 0 loaded 0 requested 7 resident 7 held 5968 set 3 resident bytes 1746;step 115 path sample 108 key 1003 zone 1003 rebuild 1 placed 1 loads - releases - unplaced 0 over 0 capped 0 loaded 1 requested 6 resident 7 held 5968 set 3 resident bytes 1746;summary: steps 208 start 7 path 201 rebuilds 4 loads 7 releases 0 over-budget steps 0 unplaced steps 0 capped steps 0;summary: resident at the end 7 distinct sets 4 frames 0;peak: held 5968 at step 59 of streamer budget 2101088;peak: resident bytes 1746 at step 59 set 3 sections 7;  budget: switch streaming.arena 1746 of 75497472 within;sequence: sha256 1D1441D0731AA82D1150705745181C62B507739113256207A656DD76E181B960 steps 208;costs: sha256 D68B1349DA4E1D8C16CF8D8E45A0BC2FC1E9DE90112B193E0C57A624B8836509 steps 208"
          "issue at;error" ${run})
set(first_run "${last_stdout}")
nfsu_case("the same run again" 0 "sequence: sha256 1D1441D0" "" ${run})
if(NOT last_stdout STREQUAL first_run)
    nfsu_fail("two runs of the same path printed different listings")
endif()

# zone-only: no track byte at the start, so the start set is the fixed four
# and 2603 - two, two and the last one, which walks to the end of the list
# and is not deferred - and J3 loads on the first path step. Every rebuild
# releases what the new request does not name: J9 goes on the way back.
nfsu_case("zone-only over the fixture" 0
          "policy: zone-only;start: steps 6 loaded yes;step 4 start sample 0 key -1 zone -1 rebuild 0 placed 1 loads 2603 releases -;step 6 path sample 0 key 1003 zone 1003 rebuild 1 placed 1 loads 1003 releases -;step 58 path sample 52 key 1009 zone 1009 rebuild 1 placed 1 loads 1009 releases -;step 114 path sample 108 key 1003 zone 1003 rebuild 1 placed 1 loads - releases 1009 unplaced 0 over 0 capped 0 loaded 1 requested 6 resident 6 held 5016;summary: steps 207 start 6 path 201 rebuilds 4 loads 7 releases 1 over-budget steps 0;sequence: sha256 1A5013C131B0BBB2198A7D023937C7C2136BFF143D4A7A2B490960AACF91ADCD steps 207"
          "" ${run} --policy zone-only)

# A budget the start set does not fit: Y0 and Z0 go in the first pass, X0
# does not, nothing is left to release, and the step is reported rather than
# failed. The run goes on to the end of the path.
nfsu_case("a budget the start set does not fit" 0
          "budget: given 3000 streamer 3000;step 0 start sample 0 key -1 zone -1 rebuild 1 placed 1 loads 2500,2600 releases - unplaced 1336 over 1 capped 0 loaded 0 requested 6 resident 2 held 992;summary: steps 205 start 4 path 201 rebuilds 4 loads 3 releases 0 over-budget steps 5 unplaced steps 5 capped steps 0;sequence: sha256 AE7C9A409009BF5E40E02411FF4B8536C15CA26F33FEFFFD029E98A263BC2F10 steps 205"
          "" ${run} --budget 3000)

# The project's own bytes as the currency, against the Switch arena. Each
# section is charged an upper bound on what it can add to any resident set
# (review comment 3630, F4), and every step's measured bytes are checked
# against the budget. The table currency says it does not compare them.
nfsu_case("the prepared currency" 0
          "cost: prepared (for each of the 7 sections any zone lists, an upper bound on what it adds to any resident set: its vertex and index bytes, all its instances, and every wanted texture its packs decode);cost: shares 1874 texture files 32 candidate scene measured 1746;budget: switch-arena 75497472 streamer 75497440;summary: steps 208 start 7 path 201 rebuilds 4 loads 7 releases 0 over-budget steps 0;measured: every step against the budget 75497472: largest 1746, steps over 0"
          "MEASURED OVER" ${run} --cost prepared)
nfsu_case("the prepared currency under a budget the start set does not fit" 0
          "budget: given 1400 streamer 1368;step 0 start sample 0 key -1 zone -1 rebuild 1 placed 1 loads 2500,2600 releases - unplaced 234 over 1;summary: steps 204 start 3 path 201 rebuilds 4 loads 2 releases 0 over-budget steps 5;measured: every step against the budget 1400: largest 94, steps over 0"
          "MEASURED OVER" ${run} --cost prepared --budget 1400)
nfsu_case("the table currency does not compare measured bytes" 0
          "measured: not compared with the budget, which is in the table's bytes" ""
          ${run})

# Review comment 3630, F1: a segment of 1000 by 69 at this speed gave 401
# samples where MinGW's libm rounded its length down and 402 where the
# compiler inlined sqrtsd. The root is now correctly rounded in every build.
set(review_path "${work}/review_f1.txt")
file(WRITE "${review_path}" "0 0 0\n1000 69 0\n")
nfsu_case("the review's path gives 402 samples in every build" 0
          "path: review_f1.txt vertices 2 length 1002.3776733347567;path: speed 150.35665100021347 steps per second 60 samples 402"
          "" "${stream}" "${trackb}" 1003 --path "${review_path}"
          --speed 150.35665100021347 --list none)

# A load that raises issues names each section once, however many resident
# sets load it again, and the run exits 3 (review comment 3630, N5). Here the
# texture file stands in for the stream, so every range passes its end.
nfsu_case("a wrong stream file names each section once" 3
          "section Y0 (2500) issue at 0: section 'Y0' reaches 912 in a file of 896 bytes;section J9 (1009) issue at 12288: section 'J9' reaches 13240 in a file of 896 bytes;summary: steps 208"
          "" "${texture}" "${trackb}" 1003 --path "${path}" --speed 60 --list none)
# The issues go to standard error; count them there.
execute_process(
    ENCODING UTF-8
    COMMAND "${PROBE}" track-stream-path "${texture}" "${trackb}" 1003 --path "${path}"
            --speed 60 --list none
    OUTPUT_VARIABLE ignored_out
    ERROR_VARIABLE issue_text
    RESULT_VARIABLE ignored_status)
string(REGEX MATCHALL "section Y0 \\(2500\\) issue" y0_issues "${issue_text}")
list(LENGTH y0_issues y0_count)
if(NOT y0_count EQUAL 1)
    nfsu_fail("the Y0 issue was printed ${y0_count} times, expected once")
endif()

# --list all prints every step; --list none prints no step and no set rows.
nfsu_case("--list all" 0 "step 207 path sample 200 key 1003 zone 1003 rebuild 0 placed 0" ""
          ${run} --list all)
string(REGEX MATCHALL "\nstep [0-9]+ " all_steps "${last_stdout}")
list(LENGTH all_steps all_count)
if(NOT all_count EQUAL 208)
    nfsu_fail("--list all printed ${all_count} step lines, expected 208")
endif()
nfsu_case("--list none" 0 "summary: steps 208;sequence: sha256 1D1441D0"
          "\nstep ;rows over 0\n  budget" ${run} --list none)

# Frames every 100 samples: samples 0, 100 and 200, each 64 by 36. The
# camera looks along the direction of travel: +slot 0 is yaw 180, and sample
# 100 lies on the vertex where the path turns back, which starts the second
# leg. The fixture's meshes are a few units across near the origin, so these
# frames draw nothing; what they pin is the writing, not the picture.
set(prefix "${work}/frame")
nfsu_case("three frames" 0
          "frame: sample 0 step 7 set 2 at 50,50,0 yaw 180 entries 7;wrote frame-000000.tga;frame: sample 100 step 107 set 3 at 150,50,0 yaw 0 entries 8;wrote frame-000100.tga;frame: sample 200 step 207 set 3 at 50,50,0 yaw 0 entries 8;wrote frame-000200.tga;summary: resident at the end 7 distinct sets 4 frames 3"
          "" ${run} --frame-every 100 --out "${prefix}" --width 64 --height 36)
foreach(sample 000000 000100 000200)
    set(frame "${prefix}-${sample}.tga")
    if(NOT EXISTS "${frame}")
        nfsu_fail("frame ${frame} was not written")
    else()
        file(SIZE "${frame}" frame_size)
        if(NOT frame_size EQUAL 9234)
            nfsu_fail("frame ${frame} is ${frame_size} bytes, expected 9234")
        endif()
    endif()
endforeach()
file(SHA256 "${prefix}-000100.tga" before)
nfsu_case("a frame that exists is never replaced" 1
          "already exists; this command never replaces a file" "stream: "
          ${run} --frame-every 100 --out "${prefix}" --width 64 --height 36)
file(SHA256 "${prefix}-000100.tga" after)
if(NOT before STREQUAL after)
    nfsu_fail("the refused run changed frame-000100.tga")
endif()
# A frame name that is one of the inputs: here the path file itself.
file(WRITE "${work}/alias-000000.tga" "50 50 0\n150 50 0\n")
file(SHA256 "${work}/alias-000000.tga" alias_before)
nfsu_case("a frame may not name an input" 1
          "error: frame alias-000000.tga would name an input file" "stream: "
          "${stream}" "${trackb}" 1003 --path "${work}/alias-000000.tga"
          --frame-every 1000 --out "${work}/alias")
file(SHA256 "${work}/alias-000000.tga" alias_after)
if(NOT alias_before STREQUAL alias_after)
    nfsu_fail("the refused run changed the path file")
endif()

# ---------------------------------------------------------------------------
# The F4 share guarantee (review comment 3689 on issue #157): a section's
# prepared share must be an upper bound on what it could add to any resident
# set on its own, decoded as if that section were the only one loaded. The
# fixture above cannot exercise this: only Y0 carries a texture pack, so
# nothing is ever shadowed. This second, minimal fixture (write-shares-
# fixture) gives two sections, SA (2500, fixed) and SB (a track-flagged
# record), each with its own pack for the same texture hash and its own mesh
# naming it. SA is requested first, so the real candidate scene decodes the
# texture through SA and SB's claim is shadowed; a correct share still
# charges SB the texture's full bytes, decoded alone. Mutants 53, 54 and 56
# of that review's sweep have no ctest: no case here ever produces a real
# MEASURED OVER line, because a section's share is always a safe upper
# bound. This is a separate, purpose-built fixture rather than a change to
# make_fixture() above: giving any of its zone-reachable sections a second
# texture pack would change its own byte size, which is also the "table"
# currency's per-record cost, and would re-pin every hash and byte count in
# every case above for a gap the issue calls non-blocking.
# ---------------------------------------------------------------------------

set(shares_trackb "${work}/shares_trackb.lzc")
set(shares_stream "${work}/shares_stream.bun")
execute_process(
    ENCODING UTF-8
    COMMAND "${TESTS}" --write-shares-fixture "${shares_trackb}" "${shares_stream}"
    OUTPUT_VARIABLE shares_fixture_text
    ERROR_VARIABLE shares_fixture_error
    RESULT_VARIABLE shares_fixture_status)
if(NOT shares_fixture_status EQUAL 0 OR NOT EXISTS "${shares_trackb}" OR
   NOT EXISTS "${shares_stream}")
    message(FATAL_ERROR
        "the shares fixture could not be written: ${shares_fixture_status}\n"
        "${shares_fixture_text}${shares_fixture_error}")
endif()
set(shares_path "${work}/shares_path.txt")
file(WRITE "${shares_path}" "0 0 0\n1 0 0\n")
set(shares_run "${shares_stream}" "${shares_trackb}" 1003 --path "${shares_path}"
    --speed 60 --cost prepared)

# At this budget SA (its own 4096-byte texture plus a 78-byte mesh, share
# 4174) never fits the retail placement slack (0x400) and block header
# (0x58), so it is never resident, and neither does SB decode-alone (share
# 4174 too, the same texture and an equally sized mesh): a correct share
# never lets a resident set's real bytes pass the budget. SB's share
# undercounted by mutant 56 (78, the mesh alone, the texture credited only
# to SA in the once-off candidate scene) fits easily, admits SB alone, and
# SB then decodes the texture for real (nothing shadows it when SA is not
# resident): 4174 real bytes against a budget of 2000.
nfsu_case("the shares fixture: a correct share never over-admits" 0
          "cost: shares 8348 texture files 0 candidate scene measured 4252;measured: every step against the budget 2000: largest 0, steps over 0"
          "MEASURED OVER" ${shares_run} --budget 2000 --list events)

# Mutant 54 (measured-over made inclusive, > to >=; review comment 3802 on
# issue #173, F2): the argument above (place_pass never admits anything
# within the retail placement slack and block header of the budget) only
# covers a NON-EMPTY resident set. At budget 0 nothing is ever admitted -
# every step's resident set is empty, measuring 0 bytes - and the correct
# comparison 0 > 0 is false, so no case above exercises the boundary at 0.
# Mutant 54's 0 >= 0 is true, and reports "MEASURED OVER the budget by 0"
# on every step of the empty resident set.
nfsu_case("the shares fixture: a zero budget measures zero, not over" 0
          "budget: given 0 streamer 0;measured: every step against the budget 0: largest 0, steps over 0"
          "MEASURED OVER" ${shares_run} --budget 0 --list events)

message(STATUS "track-stream-path guards: ${cases} cases, ${failures} failed")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} of ${cases} track-stream-path cases failed")
endif()
