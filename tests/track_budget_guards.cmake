# What `nfsu_probe track-budget` refuses, what it reports, and what it exits
# with. Every case below needs NO INPUT FILES AT ALL.
#
# WHY THIS FILE EXISTS. tests/track_budget_tests.cpp covers the library - the
# counter table, the four gatherers and check_track_budget - and reaches none
# of the command. docs/TRACK_RENDER.md records what that gap cost the last
# time it was left open: an independent mutation pass over the track-render
# block of src/main.cpp scored 7 caught out of 34, because nothing in the
# repository ran the command's body. This command's body is reachable with no
# input at all, exactly as that one turned out to be, so the same gap is not
# reopened here.
#
# WHY A SCRIPT RATHER THAN MORE add_test ENTRIES. PASS_REGULAR_EXPRESSION
# makes CTest IGNORE the exit status, and for this command the status is the
# whole point of several cases: exit 4 and exit 3 differ in ONE printed line
# and mean opposite things about a budget.
#
# WHAT EACH CASE ASSERTS. The exact exit status and a diagnostic that names
# the thing refused. A case that only required "some refusal" would pass with
# any refusal, and an argument parser has many.
#
# WHAT NO CASE HERE REACHES, stated rather than left to be discovered: exit
# 0, and only exit 0. It needs geometry.ok() && objects.ok() && cull.ok(),
# which needs a region that parses cleanly, and this script cannot build
# one - a minimal chunk header is eight bytes of which at least three are
# NUL, and CMake's file(WRITE) cannot write a NUL. The retail acceptance of
# docs/TRACK_BUDGET.md is where exit 0 is checked, and it is a run someone
# performs rather than a gate.
#
# Exits 1, 2, 3 and 4 are all driven below, and so is the ABSENCE of 5 -
# every multi-repetition case here would exit 5 if the counts were not
# deterministic.
#
# Exit 2 is worth a word because it nearly was not driven. It is the
# library-error path, and most of its refusals are refusals over CONTENT - a
# failed self-check, a box that cannot be framed - which no run without a
# region can reach. One is not: the frame is allocated from the caller's
# size rather than from the scene, so a size past RenderLimits::max_pixels
# is refused with nothing loaded at all. An earlier draft of this paragraph
# declared exit 2 unreachable here on the strength of the first half and
# never looked for the second.
#
# Run from the build directory. PROBE is the built program.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE must be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_budget_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# An input that does not exist. Deliberately not created: the pipeline
# reports "failed to read file size" and carries on, which is what lets every
# case below run the whole command body.
set(missing "${work}/missing_stream.bun")

# A file that EXISTS and is not a texture pack. It is small and its bytes are
# nonsense on purpose: what it is for is to make the texture loop RUN, which
# no other case here does, and the loop is where the input-size budget is
# computed. Passed as --texture, load_texture_pack_set reads all of it and
# then reports that it holds no pack root - and that diagnostic is what the
# cases below pin, because a budget computed wrongly refuses the file for its
# SIZE instead and the diagnostic changes.
set(not_a_pack "${work}/not_a_texture_pack.bun")
file(WRITE "${not_a_pack}" "not a texture pack at all, just bytes")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case. `label` names it, `code` is the exact exit status required,
# `needle` is text the combined output must contain, `banned` is text it must
# NOT contain (empty for none), and everything after that is the command line
# after "track-budget".
#
# `banned` is there because several faults here ADD a line rather than
# removing one - a budget that reports an exceedance it should have refused,
# or a time block under --no-time - and a case that can only require presence
# cannot see those.
function(nfsu_case label code needle banned)
    execute_process(
        COMMAND "${PROBE}" track-budget ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    set(text "${stdout_text}${stderr_text}")

    # The failure class issue #87 is about, and the one a bad argc guard
    # produces here: an exception escaping main(), or a structured-exception
    # exit code, before anything is printed.
    if(text MATCHES "terminate called")
        nfsu_fail("${label}: the run terminated on an exception:\n${text}")
        return()
    endif()
    if(NOT status MATCHES "^[0-9]+$" OR status GREATER 128)
        nfsu_fail("${label}: exit status ${status} is a crash, not a refusal")
        return()
    endif()
    if(NOT status EQUAL ${code})
        nfsu_fail("${label}: exit status ${status}, expected ${code}\n${text}")
        return()
    endif()
    if(NOT text MATCHES "${needle}")
        nfsu_fail("${label}: expected the output to contain\n  ${needle}\nand it reads\n${text}")
        return()
    endif()
    if(NOT banned STREQUAL "" AND text MATCHES "${banned}")
        nfsu_fail("${label}: the output must NOT contain\n  ${banned}\nand it reads\n${text}")
        return()
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The positional arguments. The argc guard first: issue #87's failure class
# is a std::logic_error escaping main() from a handler that indexed argv
# past the end, and every case above checks for it.
# ---------------------------------------------------------------------------

nfsu_case("no arguments at all" 1 "nfsu_probe track-budget" "")

nfsu_case("one positional short" 1 "nfsu_probe track-budget" ""
          "${missing}" 0 16 0)

nfsu_case("geometry-offset is not a number" 1
          "geometry-offset must be a decimal or 0x integer" ""
          "${missing}" zz 16 0 16)

nfsu_case("geometry-size is not a number" 1
          "geometry-size must be a decimal or 0x integer" ""
          "${missing}" 0 zz 0 16)

nfsu_case("object-offset is not a number" 1
          "object-offset must be a decimal or 0x integer" ""
          "${missing}" 0 16 zz 16)

nfsu_case("object-size is not a number" 1
          "object-size must be a decimal or 0x integer" ""
          "${missing}" 0 16 0 zz)

# ---------------------------------------------------------------------------
# The scene options this command shares with track-scene. They are a
# different copy of that parser, so they are driven here rather than assumed
# to behave because the other command's do.
# ---------------------------------------------------------------------------

nfsu_case("an unknown option is refused" 1 "unknown option --nope" ""
          "${missing}" 0 16 0 16 --nope)

nfsu_case("--texture with no value" 1 "--texture needs a path" ""
          "${missing}" 0 16 0 16 --texture)

nfsu_case("--lod-slot out of range" 1 "--lod-slot needs a slot from 0 to" ""
          "${missing}" 0 16 0 16 --lod-slot 6)

nfsu_case("--lod-slot is not a number" 1 "--lod-slot needs a slot from 0 to" ""
          "${missing}" 0 16 0 16 --lod-slot zz)

nfsu_case("--lod-slot at the top of the range is accepted" 3
          "budget: lod slot 5" ""
          "${missing}" 0 16 0 16 --lod-slot 5 --no-render --no-time)

nfsu_case("--order is checked" 1
          "--order must be visible-instance or material-group" ""
          "${missing}" 0 16 0 16 --order sideways)

nfsu_case("--order material-group is accepted" 3 "order material group" ""
          "${missing}" 0 16 0 16 --order material-group --no-render --no-time)

nfsu_case("--residency is checked" 1 "--residency must be every or no-zone" ""
          "${missing}" 0 16 0 16 --residency sometimes)

nfsu_case("--far is checked" 1 "--far needs a finite float" ""
          "${missing}" 0 16 0 16 --far nan)

nfsu_case("--eye without --target" 1 "--eye and --target are given together" ""
          "${missing}" 0 16 0 16 --eye 1 2 3)

nfsu_case("--target without --eye" 1 "--eye and --target are given together" ""
          "${missing}" 0 16 0 16 --target 1 2 3)

nfsu_case("--eye short of three floats" 1 "--eye needs three finite floats" ""
          "${missing}" 0 16 0 16 --eye 1 2)

nfsu_case("--eye and --target together select the tree policy" 3
          "camera given" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --target 4 5 6 --no-render
          --no-time)

# ---------------------------------------------------------------------------
# --repeat, and the two halves of the report it controls.
# ---------------------------------------------------------------------------

nfsu_case("--repeat rejects zero" 1 "--repeat needs a count from 1 to" ""
          "${missing}" 0 16 0 16 --repeat 0)

nfsu_case("--repeat rejects a count past the cap" 1
          "--repeat needs a count from 1 to" ""
          "${missing}" 0 16 0 16 --repeat 1001)

nfsu_case("--repeat rejects a non-number" 1 "--repeat needs a count from 1 to"
          "" "${missing}" 0 16 0 16 --repeat zz)

nfsu_case("--repeat 1 is accepted, and the warm-up is reported" 3
          "repetitions 1 after 1 discarded warm-up" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)

# The count of runs is repetitions plus the warm-up. A fault that dropped the
# warm-up, or that timed it, changes this line.
nfsu_case("the warm-up is one run beyond the repetitions" 3
          "counts identical in all 4 runs" ""
          "${missing}" 0 16 0 16 --repeat 3 --no-render --no-time)

# --no-time removes the whole time block and nothing else. Without a `banned`
# this case would pass with the times still printed.
nfsu_case("--no-time prints no time line" 3 "cost: frame.draw_calls" "time: "
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)

# And the default prints one, so the case above is not passing because the
# command never prints times at all.
nfsu_case("times are printed by default" 3 "time: parse samples 1" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render)

# Each of the four non-render phases is timed and named, AND CARRIES ITS
# SAMPLE COUNT. The count is the load-bearing half: a phase whose sample
# vector is never filled still prints its own name followed by "samples 0",
# so a needle that stops at the name is matched by a phase that collected
# nothing. An independent mutation pass ended the collection loop one phase
# early and survived on exactly that.
nfsu_case("the parse phase is timed over 2 samples" 3
          "time: parse samples 2" ""
          "${missing}" 0 16 0 16 --repeat 2 --no-render)
nfsu_case("the texture phase is timed over 2 samples" 3
          "time: texture samples 2" ""
          "${missing}" 0 16 0 16 --repeat 2 --no-render)
nfsu_case("the prepare phase is timed over 2 samples" 3
          "time: prepare samples 2" ""
          "${missing}" 0 16 0 16 --repeat 2 --no-render)
nfsu_case("the frame phase is timed over 2 samples" 3
          "time: frame samples 2" ""
          "${missing}" 0 16 0 16 --repeat 2 --no-render)

# The sample count is the repetitions and NOT the runs: the warm-up is
# discarded, so --repeat 2 gives two samples and not three. A warm-up that
# was timed rather than dropped changes this line and nothing else.
nfsu_case("the warm-up contributes no sample" 3 "time: parse samples 3"
          "time: parse samples 4"
          "${missing}" 0 16 0 16 --repeat 3 --no-render)

# --no-render suppresses the render phase's time and nothing else; with the
# render stage on, it is there with its own count. The pair is what pins
# that --no-render controls the render stage rather than merely the render
# counters.
nfsu_case("--no-render times no render phase" 3 "time: prepare samples 2"
          "time: render samples"
          "${missing}" 0 16 0 16 --repeat 2 --no-render)
nfsu_case("the render phase is timed by default" 3 "time: render samples 2" ""
          "${missing}" 0 16 0 16 --repeat 2)

# ---------------------------------------------------------------------------
# The frame size and the framing options.
# ---------------------------------------------------------------------------

nfsu_case("--width rejects zero" 1 "--width needs a size from 1 to 8192" ""
          "${missing}" 0 16 0 16 --width 0)

nfsu_case("--width rejects a size past the cap" 1
          "--width needs a size from 1 to 8192" ""
          "${missing}" 0 16 0 16 --width 8193)

nfsu_case("--height rejects zero" 1 "--height needs a size from 1 to 8192" ""
          "${missing}" 0 16 0 16 --height 0)

# A size each of whose halves is inside RenderLimits and whose PRODUCT is
# not. It is accepted by the option parser, which checks the two
# separately, and refused by render_track_draw_list, which checks the
# frame. This is the one exit-2 path a run with no region can reach: the
# frame is sized from the caller and not from the scene.
nfsu_case("a frame past the pixel limit exits 2" 2
          "frame has 67108864 pixels; the limit is 16777216" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --width 8192
          --height 8192)

# And the same run under --no-render does NOT fail, which is what pins that
# the refusal comes from the render stage rather than from the option.
nfsu_case("the same size is harmless with no render" 3
          "budget: limits 0 exceeded 0" "frame has 67108864 pixels"
          "${missing}" 0 16 0 16 --repeat 1 --no-time --no-render --width 8192
          --height 8192)

# The size reaches the frame: render.frame_bytes is the four frame buffers at
# 17 bytes a pixel, so 64 by 32 is 2048 pixels and 34816 bytes. A --width
# that parsed and was then ignored gives the default 15667200 here.
nfsu_case("the frame size reaches the frame" 4
          "cost: render.frame_bytes 34816" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --width 64 --height 32
          --limit render.frame_bytes 1)

# The echoed size carries the two in the order they were given. This is a
# smaller claim than it looks and the limit is worth stating: it pins the
# ECHO, not the frame.
#
# render.frame_bytes is a PRODUCT, so 64 by 32 and 32 by 64 are both 2048
# pixels and 34816 bytes, and the case above cannot tell them apart. An
# independent mutation pass swapped options.width and options.height inside
# measure_track_budget_once and survived both cases - correctly, because the
# echo is printed from the arguments and every counter of an EMPTY scene is
# symmetric in the two. Nothing driven from this script can see that swap.
# What can is a scene with geometry in it, where the aspect ratio changes
# what the camera frames: on the reference region 64x32 draws 65 pixels over
# 345 tests and 32x64 draws 76 over 422. That is acceptance item 7 of
# docs/TRACK_BUDGET.md, and it is a run someone performs rather than a gate.
nfsu_case("the echoed frame size is width then height" 4
          "budget: frame 64x32" "budget: frame 32x64"
          "${missing}" 0 16 0 16 --repeat 1 --no-time --width 64 --height 32
          --limit render.frame_bytes 1)

nfsu_case("--view-pitch is range checked" 1 "--view-pitch needs a number from"
          "" "${missing}" 0 16 0 16 --view-pitch 90)

nfsu_case("--view-fov is range checked at the bottom" 1
          "--view-fov needs a number from" ""
          "${missing}" 0 16 0 16 --view-fov 0.5)

nfsu_case("--view-fov is range checked at the top" 1
          "--view-fov needs a number from" ""
          "${missing}" 0 16 0 16 --view-fov 171)

nfsu_case("--view-margin is range checked" 1 "--view-margin needs a number from"
          "" "${missing}" 0 16 0 16 --view-margin 0.5)

nfsu_case("--view-yaw is range checked" 1 "--view-yaw needs a number from" ""
          "${missing}" 0 16 0 16 --view-yaw 40000)

# A NaN passes every `<` and `>` comparison, so a range gate written only
# with those admits it. This is the lane docs/TRACK_RENDER.md records as
# having been open in the track-render command.
nfsu_case("--view-pitch refuses a NaN" 1 "--view-pitch needs a number from" ""
          "${missing}" 0 16 0 16 --view-pitch nan)

nfsu_case("--view-margin at the boundary is accepted" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-margin 1)

# ---------------------------------------------------------------------------
# The empty-scene camera. With nothing drawn there is no box to fit, and the
# run still has to render and report - render.frame_bytes is the largest
# single item in the whole budget and it does not depend on geometry.
# ---------------------------------------------------------------------------

nfsu_case("nothing drawn still renders, and says which camera" 4
          "no box to frame; rendering with the documented empty-scene camera"
          "" "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 1)

nfsu_case("nothing drawn still measures the frame buffer" 4
          "cost: render.frame_bytes 15667200" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 1)

# ---------------------------------------------------------------------------
# --limit: the three refusals, and the two verdicts.
# ---------------------------------------------------------------------------

nfsu_case("--limit short of its two values" 1
          "--limit needs a counter name and a ceiling" ""
          "${missing}" 0 16 0 16 --limit frame.draw_calls)

nfsu_case("--limit with a ceiling that is not a number" 1
          "--limit needs a decimal or 0x ceiling" ""
          "${missing}" 0 16 0 16 --limit frame.draw_calls zz)

nfsu_case("--limit on an unknown counter names it" 1
          "no track budget counter is named 'frame.draw_call'" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit frame.draw_call 1)

nfsu_case("--limit twice on one counter is refused" 1
          "carries more than one limit" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit frame.draw_calls 1 --limit frame.draw_calls 2)

# The refusal that stops a budget reading as held when nothing measured the
# stage. --no-render leaves every render counter at zero, and a zero passes
# every ceiling.
nfsu_case("--limit on an unmeasured stage is refused" 1
          "which this cost did not measure" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit render.pixels_drawn 1)

# The same limit, with the stage measured, runs. Without this the refusal
# above could be a blanket refusal of every render limit.
nfsu_case("the same limit runs once the stage is measured" 3
          "budget: limits 1 exceeded 0" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.pixels_drawn 1)

# A REFUSED set reports no verdict at all. A fault that reported the
# exceedances it had collected before refusing would print this line.
nfsu_case("a refused limit set prints no verdict" 1
          "no track budget counter is named" "budget: limits"
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 1 --limit frame.nope 1)

nfsu_case("a limit that holds exits 3 here and reports nothing over" 3
          "budget: limits 1 exceeded 0" "budget: OVER"
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 15667200)

# EQUAL to the ceiling is within budget, which is the boundary a budget
# written from a measurement sits exactly on.
nfsu_case("a counter equal to its ceiling is within budget" 3
          "budget: limits 1 exceeded 0" "budget: OVER"
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 15667200)

# One below it is not.
nfsu_case("a counter one over its ceiling exits 4" 4
          "budget: OVER render.frame_bytes 15667200 exceeds 15667199" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 15667199)

nfsu_case("the exceeded count is reported" 4 "budget: limits 2 exceeded 1" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 1 --limit frame.draw_calls 10)

# With no limits there is nothing to exceed, and the verdict line still says
# so rather than being omitted.
nfsu_case("no limits still reports a verdict" 3 "budget: limits 0 exceeded 0"
          "budget: OVER"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)

# ---------------------------------------------------------------------------
# The report itself. Every stage's block has to appear, or a stage's cost is
# invisible and the whole point of separating them is lost.
# ---------------------------------------------------------------------------

nfsu_case("the parse block is reported" 3 "cost: parse.region_bytes" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)
nfsu_case("the prepare block is reported" 3 "cost: prepare.resident_bytes" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)
nfsu_case("the frame block is reported" 3 "cost: frame.draw_calls" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)
nfsu_case("the render block is reported" 4 "cost: render.pixels_drawn" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.frame_bytes 1)

nfsu_case("the inputs are echoed so a report names what produced it" 3
          "budget: geometry offset 0 size 16 objects offset 0 size 16" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time)

# A --limit ON EACH STAGE, which is what makes each stage's gatherer load
# bearing. Printing a block is not enough on its own: the counter table
# always emits every row, so a stage whose gatherer is never CALLED still
# prints its whole block at zero and every "the block is reported" case
# above passes. A limit is the only thing that reads a value, and an
# independent mutation pass deleted the parse and prepare gatherer calls
# from the command and survived until these four existed.
#
# Each ceiling is zero and each counter is zero here, so all four hold; what
# they prove is that check_track_budget did not REFUSE, which it would if
# the stage had not been measured.
nfsu_case("a parse limit is checked, so the parse gatherer ran" 3
          "budget: limits 1 exceeded 0" "did not measure"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit parse.region_bytes 0)
nfsu_case("a prepare limit is checked, so the prepare gatherer ran" 3
          "budget: limits 1 exceeded 0" "did not measure"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit prepare.resident_bytes 0)
nfsu_case("a frame limit is checked, so the frame gatherer ran" 3
          "budget: limits 1 exceeded 0" "did not measure"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --limit frame.draw_calls 0)
nfsu_case("a render limit is checked, so the render gatherer ran" 3
          "budget: limits 1 exceeded 0" "did not measure"
          "${missing}" 0 16 0 16 --repeat 1 --no-time
          --limit render.pixels_drawn 0)

# ---------------------------------------------------------------------------
# The ACCEPTING side of every bounded option, and every option literal.
#
# Each option below had its refusal driven above and its acceptance driven
# nowhere, which is the asymmetry an independent pass found: a bound moved
# inward by one rejects a documented-legal value and every rejection case
# still passes. --lod-slot already had both sides; these bring the rest into
# line.
# ---------------------------------------------------------------------------

nfsu_case("--repeat accepts its documented maximum" 3
          "repetitions 1000 after 1 discarded warm-up" ""
          "${missing}" 0 16 0 16 --repeat 1000 --no-render --no-time)

# 8192 is the documented maximum of both, and 8192 x 2048 is exactly
# RenderLimits::max_pixels, so this is the accepting boundary of the size
# options AND of the pixel limit the case above rejects one past.
nfsu_case("--width accepts its documented maximum" 4
          "cost: render.frame_bytes 285212672" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --width 8192
          --height 2048 --limit render.frame_bytes 1)
nfsu_case("--height accepts its documented maximum" 4
          "cost: render.frame_bytes 285212672" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --width 2048
          --height 8192 --limit render.frame_bytes 1)

nfsu_case("--view-pitch accepts its documented maximum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-pitch 89)
nfsu_case("--view-pitch accepts its documented minimum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-pitch -89)
nfsu_case("--view-fov accepts its documented maximum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-fov 170)
nfsu_case("--view-fov accepts its documented minimum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-fov 1)
nfsu_case("--view-margin accepts its documented maximum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-margin 100)
nfsu_case("--view-yaw accepts its documented maximum" 3 "budget: frame" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-time --view-yaw 36000)

# The option LITERALS. Every one of these had only its misspelling driven,
# so a handler that accepted the word and then did nothing with it, or that
# compared against a different spelling, passed everything.
nfsu_case("--order visible-instance is accepted and reported" 3
          "order visible instance" "order material group"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --order visible-instance)
nfsu_case("--residency every is accepted" 3 "budget: limits 0 exceeded 0" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --residency every)
nfsu_case("--residency no-zone is accepted" 3 "budget: limits 0 exceeded 0" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --residency no-zone)
nfsu_case("--far accepts a finite float" 3 "camera given" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --eye 1 2 3 --target 4 5 6 --far 250.5)
nfsu_case("--lod-slot 0 is accepted and reported" 3 "budget: lod slot 0" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --lod-slot 0)

# ---------------------------------------------------------------------------
# The texture loop, which is where the input-size budget lives.
#
# WHY A FILE AT ALL, when every other case here runs against one that does
# not exist. Without a --texture argument the loop body never executes, so
# the budget arithmetic inside it is unreachable and anything can be done to
# it. An independent mutation pass swapped the branches of the clamp on
# `remaining`: every iteration then computes a remaining of 0, every texture
# source is refused for its size, and `track-scene`, `track-render` and
# `track-budget` all stop decoding any texture at all - with the whole
# 30-test suite still green.
#
# This file is not a texture pack and is not meant to be. Decoding it has to
# get far enough to read its bytes and report that it holds no pack root,
# and THAT is the difference: a run whose size budget collapsed to zero
# refuses the file before parsing it and says so instead.
# ---------------------------------------------------------------------------

nfsu_case("a texture source is read, not refused for its size" 3
          "no 0xB3300000 texture pack root found" "exceeds the maximum"
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --texture "${not_a_pack}")

# The same file, decoded through the same function, with the per-source line
# SUPPRESSED. track-budget passes quiet=true because it times that call and
# a dozen formatted lines are not free; this is the only case that can see
# the flag, and without the `banned` needle a flag that did nothing would
# pass.
nfsu_case("track-budget decodes textures quietly" 3
          "no 0xB3300000 texture pack root found" "texture source 0: "
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --texture "${not_a_pack}")

# Two sources, so `read_bytes` is non-zero on the second iteration, which is
# the only iteration where the running total has ever been added to and so
# the only one where the subtraction it feeds can be got wrong.
nfsu_case("a second texture source is read too" 3
          "texture source 1 issue" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render --no-time
          --texture "${not_a_pack}" --texture "${not_a_pack}")

# ---------------------------------------------------------------------------
# One statistic that IS deterministic, and is therefore assertable.
#
# Every timing VALUE is a wall-clock measurement and no case can pin one.
# The spread of a single sample is not a measurement: one sample has no
# spread, so the standard deviation is exactly 0 whatever the clock said.
# That is the one timing figure this suite can hold to a value, and it
# catches a standard deviation that prints the mean, or that divides by n
# instead of n-1 and so reports something non-zero for one sample.
# ---------------------------------------------------------------------------

nfsu_case("one sample has no spread" 3 "samples 1 min .* stddev 0 max" ""
          "${missing}" 0 16 0 16 --repeat 1 --no-render)

# ---------------------------------------------------------------------------
# The OTHER side of the quiet flag, which is not a track-budget case at all.
#
# `decode_track_scene_textures` is shared with track-scene and track-render,
# and this file introduced the parameter that can silence them. Nothing in
# this repository asserts that either of those two prints its per-source
# line, so the argument at their call site could be flipped and every gate
# would stay green - the same hole that made the parameter's DEFAULT
# untestable before it was removed. The command under test here is the one
# that gained the risk, so the case belongs with it.
# ---------------------------------------------------------------------------

execute_process(
    COMMAND "${PROBE}" track-scene "${missing}" 0 16 0 16
            --texture "${not_a_pack}" --list 0
    OUTPUT_VARIABLE scene_out
    ERROR_VARIABLE scene_err
    RESULT_VARIABLE scene_code)
set(scene_text "${scene_out}${scene_err}")
if(NOT scene_text MATCHES "texture source 0: ")
    nfsu_fail("track-scene must report its texture sources; the quiet flag "
              "this file introduced is for track-budget alone. It reads:\n"
              "${scene_text}")
endif()
if(NOT scene_text MATCHES "no 0xB3300000 texture pack root found")
    nfsu_fail("track-scene must read the texture source rather than refuse "
              "it for its size. It reads:\n${scene_text}")
endif()

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} track-budget guard case(s) failed")
endif()

message(STATUS "track-budget guards: every case passed")
