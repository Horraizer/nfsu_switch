# The shader manifests against the GLSL of their stages, and that check made
# to fail.
#
# cmake/shader_manifest.cmake, MODE=source, reads each manifest and the two
# stage sources it names and reports every declaration they disagree on. The
# first half of this script runs it over every committed manifest and
# requires each to pass.
#
# The second half is why this is a script and not one add_test per manifest.
# A check that has only ever been seen to pass is not evidence, so each case
# copies shaders/ into a directory of its own, changes one thing there - a
# declaration in a stage source, or a record in a manifest, as someone
# editing one and not the other would - and requires the check to fail with
# the finding that names the change. Every run is classified by what it
# printed, not only by its exit status (CLAUDE.md, Required validation):
#
#   caught   non-zero exit, the checker's closing line, and the finding
#   wrong    non-zero exit and the closing line, but not that finding
#   missed   exit 0: the change went through
#   unknown  non-zero exit without the closing line: the checker crashed or
#            refused to run, which says nothing about the change
#   stale    the text a case changes is no longer in the file
#
# A few cases make a change that must pass instead - valid GLSL the reader
# has to read the way the compiler does, such as a // inside a block
# comment - and are classified the same way: passed on exit 0 with the
# closing line, refused on a non-zero exit with it, unknown otherwise. The
# test passes only when every case is caught or passed.
#
# The same changes against the compiled shaders are
# tests/shader_blob_guards.cmake, which runs in the Switch build.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS CHECKER SHADERS)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

# TGSI omits the mask for a full vec4. Optional unmatched regex captures
# must not turn that default into an empty component mask (#184).
include(${CHECKER})
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" tgsi_token)
set(tgsi_test "${CMAKE_CURRENT_BINARY_DIR}/nfsu_tgsi_${tgsi_token}.txt")
file(WRITE "${tgsi_test}" "VERT\nDCL IN[0], GENERIC[1]\nDCL OUT[0], GENERIC[1]\nDCL OUT[1].xy, GENERIC[2]\n")
_nfsu_sm_read_tgsi("${tgsi_test}" tgsi)
file(REMOVE "${tgsi_test}")
list(GET tgsi_ins 0 full_in)
list(GET tgsi_outs 0 full_out)
list(GET tgsi_outs 1 partial_out)
if(NOT tgsi_error STREQUAL "" OR NOT full_in MATCHES "mask=xyzw" OR
   NOT full_out MATCHES "mask=xyzw" OR NOT partial_out MATCHES "mask=xy[|]")
    message(FATAL_ERROR "TGSI omitted/explicit component mask regression")
endif()

# A directory no other run shares: several sessions run ctest at once here.
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_shader_source_guards_${token}")
set(report "")
set(caught 0)
set(passed 0)
set(failed 0)
# The bytes some cases write. A copy is written with LF line ends unless
# newline_style says CRLF.
string(ASCII 11 vt)
string(ASCII 12 ff)
string(ASCII 13 cr)
set(newline_style LF)

function(nfsu_check_source manifest out_status out_text)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DMODE=source -DMANIFEST=${manifest} -P ${CHECKER}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(${out_status} "${status}" PARENT_SCOPE)
    set(${out_text} "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

file(GLOB manifests "${SHADERS}/*.manifest")
list(LENGTH manifests manifest_count)
if(manifest_count EQUAL 0)
    message(FATAL_ERROR "no manifest in ${SHADERS}")
endif()
foreach(manifest IN LISTS manifests)
    get_filename_component(name "${manifest}" NAME)
    nfsu_check_source("${manifest}" status text)
    if(NOT status EQUAL 0 OR NOT text MATCHES "no drift")
        string(APPEND report "FAILED ${name}: the committed manifest does not pass\n${text}\n")
        math(EXPR failed "${failed} + 1")
    endif()
endforeach()

# The regular textured shader must allow early depth testing. The cutout
# shader remains available for material alpha tests and diagnostic cutoffs.
file(READ "${SHADERS}/textured_opaque.frag.glsl" opaque_source)
file(READ "${SHADERS}/textured.frag.glsl" cutout_source)
if(opaque_source MATCHES "discard[ \t]*;" OR
   NOT cutout_source MATCHES "discard[ \t]*;")
    message(FATAL_ERROR "textured shader discard split is missing")
endif()

# nfsu_case(<label> <file> <manifest> <expected> [<from> <to>]...)
#
# Replaces each <from> with its <to> in <file> of a fresh copy of shaders/,
# runs the check over <manifest> of that copy, and requires it to fail with
# <expected> in its output, or to pass when <expected> is PASS. The pairs
# are read one argument at a time, because a CMake list would split the
# semicolons they hold.
function(nfsu_case label file manifest expected)
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    file(GLOB sources "${SHADERS}/*")
    file(COPY ${sources} DESTINATION "${work}")
    # file(READ) drops the CR of each CR LF, so a checkout with CRLF line
    # endings is compared and changed with LF, and every pattern below that
    # spans a line still matches.
    file(READ "${work}/${file}" text)
    math(EXPR last "${ARGC} - 1")
    if(ARGC GREATER 4)
        foreach(from_index RANGE 4 ${last} 2)
            math(EXPR to_index "${from_index} + 1")
            set(old "${ARGV${from_index}}")
            set(new "${ARGV${to_index}}")
            string(FIND "${text}" "${old}" at)
            if(at EQUAL -1)
                set(report "${report}STALE  ${label}: `${old}` is not in ${file}\n" PARENT_SCOPE)
                math(EXPR count "${failed} + 1")
                set(failed ${count} PARENT_SCOPE)
                return()
            endif()
            string(REPLACE "${old}" "${new}" text "${text}")
        endforeach()
    endif()
    # file(WRITE) writes text mode on Windows, where every LF would gain a
    # CR. file(CONFIGURE) with a newline style writes the same bytes on every
    # host; @ONLY leaves the sources' text alone, since none holds an @VAR@
    # or a #cmakedefine.
    file(CONFIGURE OUTPUT "${work}/${file}" CONTENT "${text}" @ONLY
        NEWLINE_STYLE ${newline_style})

    nfsu_check_source("${work}/${manifest}" status output)
    set(closing FALSE)
    if(output MATCHES "drift finding\\(s\\) against|manifest error\\(s\\)|no drift")
        set(closing TRUE)
    endif()
    if(expected STREQUAL "PASS")
        if(NOT closing)
            set(verdict "UNKNOWN")
        elseif(status EQUAL 0)
            set(verdict "passed")
        else()
            set(verdict "REFUSED")
        endif()
    elseif(status EQUAL 0)
        set(verdict "MISSED")
    elseif(NOT closing)
        set(verdict "UNKNOWN")
    else()
        string(FIND "${output}" "${expected}" found)
        if(found EQUAL -1)
            set(verdict "WRONG")
        else()
            set(verdict "caught")
        endif()
    endif()
    if(verdict STREQUAL "caught" OR verdict STREQUAL "passed")
        math(EXPR count "${${verdict}} + 1")
        set(${verdict} ${count} PARENT_SCOPE)
        set(report "${report}${verdict} ${label}\n" PARENT_SCOPE)
    else()
        math(EXPR count "${failed} + 1")
        set(failed ${count} PARENT_SCOPE)
        set(report "${report}${verdict} ${label}: expected `${expected}`\n${output}\n" PARENT_SCOPE)
    endif()
endfunction()

# A vertex attribute moved in the shader and not in the manifest.
nfsu_case("a vertex attribute moved in the stage source"
    placed.vert.glsl car_textured.manifest
    "placed.vert.glsl declares vertex attribute location 3 (vec2 inTexcoord), which the manifest does not"
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 3) in vec2 inTexcoord")

nfsu_case("a vertex attribute widened in the stage source"
    placed.vert.glsl car_fallback.manifest
    "vertex attribute location 2: the manifest says vec2, placed.vert.glsl declares vec3"
    "in vec2 inTexcoord"
    "in vec3 inTexcoord")

# Two attributes of one type swapped: the locations and types still agree,
# and only the names say what moved.
nfsu_case("two vertex attributes of one type swapped"
    placed.vert.glsl track_textured.manifest
    "vertex attribute location 0: the manifest names it inPosition, placed.vert.glsl declares inColor"
    "layout (location = 0) in vec3 inPosition"
    "layout (location = 0) in vec3 inColor"
    "layout (location = 1) in vec3 inColor"
    "layout (location = 1) in vec3 inPosition")

nfsu_case("a texture binding moved in the stage source"
    textured.frag.glsl car_textured.manifest
    "texture baseTexture: the manifest binds it at 0, textured.frag.glsl at 1"
    "layout (binding = 0) uniform sampler2D baseTexture"
    "layout (binding = 1) uniform sampler2D baseTexture")

nfsu_case("a uniform block binding moved in the stage source"
    textured.frag.glsl car_textured.manifest
    "uniform block Material: the manifest binds it at 2, textured.frag.glsl at 3"
    "layout (std140, binding = 2) uniform Material"
    "layout (std140, binding = 3) uniform Material")

nfsu_case("a member added to a uniform block"
    textured_opaque.frag.glsl track_textured.manifest
    "uniform block Material: the manifest lists 5 members, textured_opaque.frag.glsl declares 6"
    "    vec4 environment_color;\n} material;"
    "    vec4 environment_color;\n    vec4 extra;\n} material;")

nfsu_case("two uniform block members reordered"
    placed.vert.glsl car_fallback.manifest
    "uniform block Frame member 1: the manifest says vec4 right, placed.vert.glsl declares vec4 up"
    "    vec4 right;\n    vec4 up;"
    "    vec4 up;\n    vec4 right;")

nfsu_case("a varying's interpolation changed in the stage source"
    placed.vert.glsl track_textured.manifest
    "varying location 2 (outTexcoord): the manifest says smooth, placed.vert.glsl declares noperspective"
    "layout (location = 2) out vec2 outTexcoord"
    "layout (location = 2) noperspective out vec2 outTexcoord")

# GLSL 4.60 allows an interpolation qualifier before layout(...) as well as
# after it, and uam reads both orders the same way (review comment 2856,
# F1). The declaration match in cmake/shader_manifest.cmake starts at the
# word layout, so each of these left the qualifier silently dropped before
# the fix: the declaration parsed as unqualified (smooth), matched the
# manifest by accident, and the checker printed "no drift" while the
# compiled shader carried the qualifier that was actually written.
nfsu_case("a flat qualifier written before layout in the stage source"
    textured.frag.glsl car_textured.manifest
    "qualifies a declaration before its layout(...)"
    "layout (location = 1) in vec3 inColor"
    "flat layout (location = 1) in vec3 inColor")

nfsu_case("a noperspective qualifier written before layout in the stage source"
    placed.vert.glsl track_textured.manifest
    "qualifies a declaration before its layout(...)"
    "layout (location = 2) out vec2 outTexcoord"
    "noperspective layout (location = 2) out vec2 outTexcoord")

# centroid has no representation in shaders/*.manifest at all (no manifest
# here declares it), so this is refused the same way as flat and
# noperspective rather than read and compared.
nfsu_case("a centroid qualifier written before layout in the stage source"
    textured.frag.glsl car_textured.manifest
    "qualifies a declaration before its layout(...)"
    "layout (location = 2) in vec2 inTexcoord"
    "centroid layout (location = 2) in vec2 inTexcoord")

# The prefix check used to list only flat, smooth, noperspective and
# centroid, so `sample` - GLSL's other auxiliary storage qualifier, which
# also changes interpolation - was silently dropped when written before
# layout(...) (review comment 2942, R1): the declaration parsed as
# unqualified and the checker printed "no drift" while the compiled shader
# carried `sample`. The fix refuses ANY identifier between a statement
# boundary and a matched layout(...), not a fixed list of four.
nfsu_case("a sample qualifier written before layout in the stage source"
    textured.frag.glsl car_textured.manifest
    "qualifies a declaration before its layout(...)"
    "layout (location = 1) in vec3 inColor"
    "sample layout (location = 1) in vec3 inColor")

# invariant is a third kind of qualifier the old fixed list never named
# either - not an interpolation qualifier at all, but the same leading
# position before layout(...) - on an output this time, since invariant
# only applies to outputs.
nfsu_case("an invariant qualifier written before layout in the stage source"
    placed.vert.glsl car_textured.manifest
    "qualifies a declaration before its layout(...)"
    "layout (location = 0) out vec3 outWorld"
    "invariant layout (location = 0) out vec3 outWorld")

nfsu_case("a colour output moved in the stage source"
    textured.frag.glsl car_textured.manifest
    "textured.frag.glsl declares output location 1 (vec4 outColor), which the manifest does not"
    "layout (location = 0) out vec4 outColor"
    "layout (location = 1) out vec4 outColor")

nfsu_case("an output with no layout qualifier"
    fallback.frag.glsl car_fallback.manifest
    "declares an interface variable without a layout qualifier the check reads"
    "layout (location = 0) out vec4 outColor"
    "out vec4 outColor")

nfsu_case("a preprocessor directive that could hide a declaration"
    textured.frag.glsl car_textured.manifest
    "`#define BASE_BINDING 0` is a preprocessor directive the interface check does not follow"
    "layout (binding = 0) uniform sampler2D baseTexture"
    "#define BASE_BINDING 0\nlayout (binding = 0) uniform sampler2D baseTexture")

# The other direction: the manifest edited and the shader not.
nfsu_case("a vertex attribute moved in the manifest"
    car_textured.manifest car_textured.manifest
    "placed.vert.glsl declares vertex attribute location 2 (vec2 inTexcoord), which the manifest does not"
    "attribute location=2 name=texcoord"
    "attribute location=3 name=texcoord")

nfsu_case("a texture binding moved in the manifest"
    car_textured.manifest car_textured.manifest
    "texture baseTexture: the manifest binds it at 1, textured.frag.glsl at 0"
    "texture name=baseTexture binding=0"
    "texture name=baseTexture binding=1")

nfsu_case("a varying's interpolation changed in the manifest"
    track_fallback.manifest track_fallback.manifest
    "varying location 1 (outColor): the manifest says flat, placed.vert.glsl declares smooth"
    "varying location=1 name=color type=vec3 interpolation=smooth"
    "varying location=1 name=color type=vec3 interpolation=flat")

nfsu_case("a block member offset the std140 layout does not produce"
    car_textured.manifest car_textured.manifest
    "Frame.light is at offset 84, but std140 puts a vec4 after the previous member at 80"
    "member block=Frame name=light type=vec4 offset=80"
    "member block=Frame name=light type=vec4 offset=84")

# The reader has to split comments, lines and directives exactly as uam's
# preprocessor does; each case below is a way a reader that did not, the
# one this check had before, was shown one declaration while the compiler
# read another.

# glcpp looks for the */ that closes a block after its /*, so /*/ opens a
# comment, and it opens whichever of // and /* comes first.
nfsu_case("a declaration two /*/ comments hide, and a // inside a block comment"
    placed.vert.glsl car_textured.manifest
    "placed.vert.glsl declares vertex attribute location 3 (vec2 inTexcoord), which the manifest does not"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "/* see // below */ layout (location = 3) in vec2 inTexcoord;\n/*/ layout (location = 2) in vec2 inTexcoord; /*/\n")

nfsu_case("a uniform block after a block comment holding a URL"
    textured.frag.glsl car_textured.manifest
    "textured.frag.glsl declares uniform block Fog at binding 3, which the manifest does not give the fragment stage"
    "layout (binding = 0) uniform sampler2D baseTexture;\n"
    "layout (binding = 0) uniform sampler2D baseTexture;\n/* Fog, as https://example.org/fog describes it. */\nlayout (std140, binding = 3) uniform Fog {\n    vec4 fog_color;\n} fog;\n")

nfsu_case("a block comment holding a URL"
    placed.vert.glsl car_textured.manifest PASS
    "void main()\n"
    "/* Placement as https://example.org/placement describes it. */\nvoid main()\n")

# A backslash at the end of a line joins the next line to it before
# comments are seen, so a // comment ending in one takes the next line too.
nfsu_case("a // comment a backslash joins to the declaration below it"
    placed.vert.glsl car_textured.manifest
    "vertex attribute location 2 (vec2 inTexcoord) is in the manifest, but placed.vert.glsl does not declare it"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "// was: \\\nlayout (location = 2) in vec2 inTexcoord;\nlayout (location = 3) in vec2 inTexcoord;\n")

# glcpp ends a line at a lone CR too; this reader reads LF and CR LF only,
# and refuses any other CR rather than read the line differently.
nfsu_case("a lone CR that starts a directive"
    placed.vert.glsl car_textured.manifest
    "placed.vert.glsl: has a CR that does not begin a CR LF"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "// texcoord${cr}#define vec2 ivec2\nlayout (location = 2) in vec2 inTexcoord;\n")

set(newline_style CRLF)
nfsu_case("CR LF line ends" placed.vert.glsl car_textured.manifest PASS)
set(newline_style LF)

# A directive may follow a vertical tab, a form feed or a block comment.
nfsu_case("a form feed before a directive"
    placed.vert.glsl car_textured.manifest
    "`#define vec2 ivec2` is a preprocessor directive the interface check does not follow"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "${ff}#define vec2 ivec2\nlayout (location = 2) in vec2 inTexcoord;\n")

nfsu_case("a vertical tab before a directive"
    placed.vert.glsl car_textured.manifest
    "`#define vec2 ivec2` is a preprocessor directive the interface check does not follow"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "${vt}#define vec2 ivec2\nlayout (location = 2) in vec2 inTexcoord;\n")

nfsu_case("a block comment before a directive"
    placed.vert.glsl car_textured.manifest
    "`#define vec2 ivec2` is a preprocessor directive the interface check does not follow"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "/* note */ #define vec2 ivec2\nlayout (location = 2) in vec2 inTexcoord;\n")

nfsu_case("a # that does not begin a line"
    placed.vert.glsl car_textured.manifest
    "placed.vert.glsl: has a # that does not begin a line"
    "layout (location = 2) in vec2 inTexcoord;\n"
    "layout (location = 2) in vec2 inTexcoord; #define X\n")

nfsu_case("an @ in a comment"
    placed.vert.glsl car_textured.manifest PASS
    "in that memory order.\n"
    "in that memory order. Questions: shaders@example.org\n")

# GLSL, C++ and CMake read a leading zero three different ways, so neither
# side may write one.
nfsu_case("a location written with a leading zero in the stage source"
    placed.vert.glsl car_textured.manifest
    "placed.vert.glsl: `location = 02` is not a decimal without a leading zero"
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 02) in vec2 inTexcoord")

nfsu_case("an array size written with a leading zero in the stage source"
    placed.vert.glsl car_textured.manifest
    "has an array size that is not a decimal above zero without a leading zero"
    "vec4 rows[4];"
    "vec4 rows[04];")

nfsu_case("a location written with a leading zero in the manifest"
    car_textured.manifest car_textured.manifest
    "`02` is not a valid location"
    "attribute location=2 name=texcoord"
    "attribute location=02 name=texcoord")

nfsu_case("program moved out of the second record"
    car_textured.manifest car_textured.manifest
    "the second record must be `program <name>`"
    "format 1\nprogram car_textured\n"
    "format 1\n"
    "\nvariant name=opaque "
    "\nprogram car_textured\nvariant name=opaque ")

file(REMOVE_RECURSE "${work}")
if(NOT failed EQUAL 0)
    message(FATAL_ERROR "shader source guards: ${failed} failed, ${caught} caught, ${passed} passed\n${report}")
endif()
message(STATUS "${report}shader source guards: ${manifest_count} manifests pass, "
    "${caught} changes caught and ${passed} passed, none missed")
