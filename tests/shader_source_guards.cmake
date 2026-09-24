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
# and the test passes only when every case is caught.
#
# The same changes against the compiled shaders are
# tests/shader_blob_guards.cmake, which runs in the Switch build.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS CHECKER SHADERS)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

# A directory no other run shares: several sessions run ctest at once here.
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_shader_source_guards_${token}")
set(report "")
set(caught 0)
set(failed 0)

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

# nfsu_case(<label> <file> <manifest> <expected> <from> <to> [<from> <to>])
#
# Replaces <from> with <to> in <file> of a fresh copy of shaders/ (and the
# second pair too, when given), runs the check over <manifest> of that copy,
# and requires it to fail with <expected> in its output.
function(nfsu_case label file manifest expected from to)
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    file(GLOB sources "${SHADERS}/*")
    file(COPY ${sources} DESTINATION "${work}")
    file(READ "${work}/${file}" text)
    # A checkout with CRLF line endings would leave every pattern below that
    # spans a line unmatched, so the copy is compared, changed and written
    # with LF; both checkers read either.
    string(REPLACE "\r\n" "\n" text "${text}")
    set(pairs 1)
    if(ARGC GREATER 7)
        set(pairs 2)
    endif()
    foreach(pair RANGE 1 ${pairs})
        if(pair EQUAL 1)
            set(old "${from}")
            set(new "${to}")
        else()
            set(old "${ARGV6}")
            set(new "${ARGV7}")
        endif()
        string(FIND "${text}" "${old}" at)
        if(at EQUAL -1)
            set(report "${report}STALE  ${label}: `${old}` is not in ${file}\n" PARENT_SCOPE)
            math(EXPR count "${failed} + 1")
            set(failed ${count} PARENT_SCOPE)
            return()
        endif()
        string(REPLACE "${old}" "${new}" text "${text}")
    endforeach()
    file(WRITE "${work}/${file}" "${text}")

    nfsu_check_source("${work}/${manifest}" status output)
    if(status EQUAL 0)
        set(verdict "MISSED")
    elseif(NOT output MATCHES "drift finding\\(s\\) against|manifest error\\(s\\)")
        set(verdict "UNKNOWN")
    else()
        string(FIND "${output}" "${expected}" found)
        if(found EQUAL -1)
            set(verdict "WRONG")
        else()
            set(verdict "caught")
        endif()
    endif()
    if(verdict STREQUAL "caught")
        math(EXPR count "${caught} + 1")
        set(caught ${count} PARENT_SCOPE)
        set(report "${report}caught ${label}\n" PARENT_SCOPE)
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
    textured.frag.glsl track_textured.manifest
    "uniform block Material: the manifest lists 5 members, textured.frag.glsl declares 6"
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

file(REMOVE_RECURSE "${work}")
if(NOT failed EQUAL 0)
    message(FATAL_ERROR "shader source guards: ${failed} failed, ${caught} caught\n${report}")
endif()
message(STATUS "${report}shader source guards: ${manifest_count} manifests pass, "
    "${caught} changes caught, none missed")
