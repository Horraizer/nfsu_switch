# The shader manifests against what uam compiles, and that check made to
# fail. It runs in the Switch build, which is where uam is.
#
# cmake/shader_manifest.cmake, MODE=blob, reads the shader program header
# inside each .dksh and the TGSI uam wrote beside it, and compares them with
# the manifest. The build runs that check over the blobs it compiles; this
# script compiles its own, into a directory no other run shares, so that it
# can also compile the stages with one declaration changed and leave the
# manifest as it is - which is what someone editing a shader and not its
# manifest does - and require the check to fail with the finding that names
# the change.
#
# First every committed manifest must pass against freshly compiled,
# unchanged stages. Then each case changes one stage source, compiles all
# three stages, and checks the unchanged manifest against them. Runs are
# classified by what they printed, as in tests/shader_source_guards.cmake:
# a case whose changed source uam refuses to compile is reported as unknown,
# because it says nothing about the check, and the test fails.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS UAM CHECKER SHADERS)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_shader_blob_guards_${token}")
set(report "")
set(caught 0)
set(failed 0)
set(stages placed.vert textured.frag fallback.frag)

# Compiles the three stages in <directory> with uam, beside their sources.
# Sets <out> to "" or to what uam said about the first stage it refused.
function(nfsu_compile directory out)
    set(${out} "" PARENT_SCOPE)
    foreach(stem IN LISTS stages)
        if(stem MATCHES "[.]vert$")
            set(kind vert)
        else()
            set(kind frag)
        endif()
        file(REMOVE "${directory}/${stem}.dksh" "${directory}/${stem}.tgsi")
        execute_process(
            COMMAND ${UAM} -s ${kind} -o ${directory}/${stem}.dksh
                    -t ${directory}/${stem}.tgsi ${directory}/${stem}.glsl
            RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0 OR NOT EXISTS "${directory}/${stem}.dksh")
            set(${out} "uam refused ${stem}.glsl (exit ${status}): ${stdout}${stderr}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# Runs the blob check of <manifest> over the stages compiled in <directory>.
function(nfsu_check_blob manifest directory out_status out_text)
    file(READ "${manifest}" text)
    string(REGEX MATCH "stage kind=vertex source=([^ \n]+)[.]glsl" match "${text}")
    set(vertex "${CMAKE_MATCH_1}")
    string(REGEX MATCH "stage kind=fragment source=([^ \n]+)[.]glsl" match "${text}")
    set(fragment "${CMAKE_MATCH_1}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DMODE=blob -DMANIFEST=${manifest}
                -DVERTEX_DKSH=${directory}/${vertex}.dksh
                -DVERTEX_TGSI=${directory}/${vertex}.tgsi
                -DFRAGMENT_DKSH=${directory}/${fragment}.dksh
                -DFRAGMENT_TGSI=${directory}/${fragment}.tgsi
                -P ${CHECKER}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(${out_status} "${status}" PARENT_SCOPE)
    set(${out_text} "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

function(nfsu_fresh_copy)
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    file(GLOB sources "${SHADERS}/*.glsl")
    file(COPY ${sources} DESTINATION "${work}")
endfunction()

# The committed stages, compiled here, against every committed manifest.
nfsu_fresh_copy()
nfsu_compile("${work}" refused)
if(NOT refused STREQUAL "")
    message(FATAL_ERROR "the committed stages do not compile: ${refused}")
endif()
file(GLOB manifests "${SHADERS}/*.manifest")
list(LENGTH manifests manifest_count)
if(manifest_count EQUAL 0)
    message(FATAL_ERROR "no manifest in ${SHADERS}")
endif()
foreach(manifest IN LISTS manifests)
    get_filename_component(name "${manifest}" NAME)
    nfsu_check_blob("${manifest}" "${work}" status text)
    if(NOT status EQUAL 0 OR NOT text MATCHES "no drift")
        string(APPEND report "FAILED ${name}: the committed manifest does not pass\n${text}\n")
        math(EXPR failed "${failed} + 1")
    endif()
endforeach()

# nfsu_case(<label> <file> <manifest> <expected> <from> <to> [<from> <to>])
#
# Replaces <from> with <to> in stage source <file> of a fresh copy (and the
# second pair too, when given), compiles the three stages, and requires the
# committed <manifest> to fail against them with <expected> in the output.
function(nfsu_case label file manifest expected from to)
    nfsu_fresh_copy()
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

    nfsu_compile("${work}" refused)
    if(NOT refused STREQUAL "")
        set(verdict "UNKNOWN")
        set(output "${refused}")
    else()
        nfsu_check_blob("${SHADERS}/${manifest}" "${work}" status output)
        if(status EQUAL 0)
            set(verdict "MISSED")
        elseif(NOT output MATCHES "drift finding\\(s\\) against")
            set(verdict "UNKNOWN")
        else()
            string(FIND "${output}" "${expected}" found)
            if(found EQUAL -1)
                set(verdict "WRONG")
            else()
                set(verdict "caught")
            endif()
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

nfsu_case("a vertex attribute moved"
    placed.vert.glsl car_textured.manifest
    "placed.vert.dksh fetches vertex attribute location 3 (components xy), which the manifest does not declare"
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 3) in vec2 inTexcoord")

nfsu_case("a vertex attribute widened and read"
    placed.vert.glsl car_fallback.manifest
    "vertex attribute location 1 (color): the manifest says vec3 (xyz), placed.vert.dksh fetches xyzw"
    "layout (location = 1) in vec3 inColor"
    "layout (location = 1) in vec4 inColor"
    "    outColor = inColor;"
    "    outColor = inColor.rgb * inColor.a;")

nfsu_case("a texture binding moved"
    textured.frag.glsl car_textured.manifest
    "the compiled fragment shader samples a texture at binding 1 (textured.frag.tgsi: SAMP[1]), which the manifest does not give that stage"
    "layout (binding = 0) uniform sampler2D baseTexture"
    "layout (binding = 1) uniform sampler2D baseTexture")

nfsu_case("a fragment uniform block binding moved"
    textured.frag.glsl track_textured.manifest
    "the compiled fragment shader reads a 80-byte uniform block at binding 3 (textured.frag.tgsi: CONST[4]), which the manifest does not give that stage"
    "layout (std140, binding = 2) uniform Material"
    "layout (std140, binding = 3) uniform Material")

nfsu_case("a vertex uniform block binding moved"
    placed.vert.glsl car_textured.manifest
    "uniform block Instance is in the manifest at vertex binding 1, but the compiled vertex shader has no uniform block there"
    "layout (std140, binding = 1) uniform Instance"
    "layout (std140, binding = 4) uniform Instance")

nfsu_case("a member added to a uniform block"
    fallback.frag.glsl car_fallback.manifest
    "uniform block Material at fragment binding 2: the manifest says 80 bytes, the compiled shader declares 96"
    "    vec4 environment_color;\n} material;"
    "    vec4 environment_color;\n    vec4 extra;\n} material;")

nfsu_case("a varying's interpolation changed"
    textured.frag.glsl car_textured.manifest
    "varying location 2 (texcoord): the manifest says smooth, textured.frag.dksh interpolates it noperspective"
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 2) noperspective in vec2 inTexcoord")

nfsu_case("a varying moved in the vertex stage"
    placed.vert.glsl track_fallback.manifest
    "the compiled vertex shader writes varying location 3 (xyz), which the manifest does not declare"
    "layout (location = 1) out vec3 outColor"
    "layout (location = 3) out vec3 outColor")

nfsu_case("a colour output moved"
    fallback.frag.glsl track_fallback.manifest
    "fallback.frag.dksh writes render target 1 (xyzw), which the manifest does not declare"
    "layout (location = 0) out vec4 outColor"
    "layout (location = 1) out vec4 outColor")

file(REMOVE_RECURSE "${work}")
if(NOT failed EQUAL 0)
    message(FATAL_ERROR "shader blob guards: ${failed} failed, ${caught} caught\n${report}")
endif()
message(STATUS "${report}shader blob guards: ${manifest_count} manifests pass against "
    "the compiled stages, ${caught} changes caught, none missed")
