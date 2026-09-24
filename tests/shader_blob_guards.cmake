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
#
# Two cases check a changed stage's blob against the TGSI of the unchanged
# compile instead of its own, which is how a blob and a TGSI that are not
# the output of one uam run look to the check.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS UAM CHECKER SHADERS)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

# The checker is also a module: its manifest parser names the stages each
# manifest compiles, so this script reads no manifest itself.
include(${CHECKER})

string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_shader_blob_guards_${token}")
set(base "${work}_unchanged")
set(report "")
set(caught 0)
set(failed 0)

file(GLOB manifests "${SHADERS}/*.manifest")
list(LENGTH manifests manifest_count)
if(manifest_count EQUAL 0)
    message(FATAL_ERROR "no manifest in ${SHADERS}")
endif()
# Every stage any manifest names, as stem:vert or stem:frag.
set(stages "")
foreach(manifest IN LISTS manifests)
    nfsu_shader_manifest_stages("${manifest}" program vertex fragment)
    foreach(stage IN ITEMS vertex fragment)
        get_filename_component(stem "${${stage}}" NAME)
        string(REGEX REPLACE "[.]glsl$" "" stem "${stem}")
        set(kind vert)
        if(stage STREQUAL "fragment")
            set(kind frag)
        endif()
        if(NOT "${stem}:${kind}" IN_LIST stages)
            list(APPEND stages "${stem}:${kind}")
        endif()
    endforeach()
endforeach()

# Compiles every stage in <directory> with uam, beside its source. Sets
# <out> to "" or to what uam said about the first stage it refused.
function(nfsu_compile directory out)
    set(${out} "" PARENT_SCOPE)
    foreach(entry IN LISTS stages)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 stem)
        list(GET parts 1 kind)
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

# Runs the blob check of <manifest> over the blobs compiled in <directory>,
# each with the TGSI of the same stem in <tgsi_directory>.
function(nfsu_check_blob manifest directory tgsi_directory out_status out_text)
    nfsu_shader_manifest_stages("${manifest}" program vertex fragment)
    foreach(stage IN ITEMS vertex fragment)
        get_filename_component(stem "${${stage}}" NAME)
        string(REGEX REPLACE "[.]glsl$" "" ${stage} "${stem}")
    endforeach()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DMODE=blob -DMANIFEST=${manifest}
                -DVERTEX_DKSH=${directory}/${vertex}.dksh
                -DVERTEX_TGSI=${tgsi_directory}/${vertex}.tgsi
                -DFRAGMENT_DKSH=${directory}/${fragment}.dksh
                -DFRAGMENT_TGSI=${tgsi_directory}/${fragment}.tgsi
                -P ${CHECKER}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(${out_status} "${status}" PARENT_SCOPE)
    set(${out_text} "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

function(nfsu_fresh_copy directory)
    file(REMOVE_RECURSE "${directory}")
    file(MAKE_DIRECTORY "${directory}")
    file(GLOB sources "${SHADERS}/*.glsl")
    file(COPY ${sources} DESTINATION "${directory}")
endfunction()

# The committed stages, compiled once, against every committed manifest.
# The blobs stay for the cases that pair a changed blob with this TGSI.
nfsu_fresh_copy("${base}")
nfsu_compile("${base}" refused)
if(NOT refused STREQUAL "")
    message(FATAL_ERROR "the committed stages do not compile: ${refused}")
endif()
foreach(manifest IN LISTS manifests)
    get_filename_component(name "${manifest}" NAME)
    nfsu_check_blob("${manifest}" "${base}" "${base}" status text)
    if(NOT status EQUAL 0 OR NOT text MATCHES "no drift")
        string(APPEND report "FAILED ${name}: the committed manifest does not pass\n${text}\n")
        math(EXPR failed "${failed} + 1")
    endif()
endforeach()

# nfsu_case(<label> <file> <manifest> <expected> <tgsi> <from> <to> [<from> <to>]...)
#
# Replaces each <from> with its <to> in stage source <file> of a fresh copy,
# compiles every stage, and requires the committed <manifest> to fail
# against the blobs with <expected> in the output. <tgsi> is OWN to check
# each blob with its own TGSI, or UNCHANGED to check it with the TGSI of the
# committed stages. The pairs are read one argument at a time, because a
# CMake list would split the semicolons they hold.
function(nfsu_case label file manifest expected tgsi)
    nfsu_fresh_copy("${work}")
    # file(READ) drops the CR of each CR LF, so a checkout with CRLF line
    # endings is compared and changed with LF.
    file(READ "${work}/${file}" text)
    math(EXPR last "${ARGC} - 1")
    foreach(from_index RANGE 5 ${last} 2)
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
    # The same bytes on every host; see tests/shader_source_guards.cmake.
    file(CONFIGURE OUTPUT "${work}/${file}" CONTENT "${text}" @ONLY NEWLINE_STYLE LF)

    set(tgsi_directory "${work}")
    if(tgsi STREQUAL "UNCHANGED")
        set(tgsi_directory "${base}")
    endif()
    nfsu_compile("${work}" refused)
    if(NOT refused STREQUAL "")
        set(verdict "UNKNOWN")
        set(output "${refused}")
    else()
        nfsu_check_blob("${SHADERS}/${manifest}" "${work}" "${tgsi_directory}" status output)
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
    OWN
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 3) in vec2 inTexcoord")

nfsu_case("a vertex attribute widened and read"
    placed.vert.glsl car_fallback.manifest
    "vertex attribute location 1 (color): the manifest says vec3 (xyz), placed.vert.dksh fetches xyzw"
    OWN
    "layout (location = 1) in vec3 inColor"
    "layout (location = 1) in vec4 inColor"
    "    outColor = inColor;"
    "    outColor = inColor.rgb * inColor.a;")

nfsu_case("a texture binding moved"
    textured.frag.glsl car_textured.manifest
    "the compiled fragment shader samples a texture at binding 1 (textured.frag.tgsi: SAMP[1]), which the manifest does not give that stage"
    OWN
    "layout (binding = 0) uniform sampler2D baseTexture"
    "layout (binding = 1) uniform sampler2D baseTexture")

nfsu_case("a fragment uniform block binding moved"
    textured.frag.glsl track_textured.manifest
    "the compiled fragment shader reads a 80-byte uniform block at binding 3 (textured.frag.tgsi: CONST[4]), which the manifest does not give that stage"
    OWN
    "layout (std140, binding = 2) uniform Material"
    "layout (std140, binding = 3) uniform Material")

nfsu_case("a vertex uniform block binding moved"
    placed.vert.glsl car_textured.manifest
    "uniform block Instance is in the manifest at vertex binding 1, but the compiled vertex shader has no uniform block there"
    OWN
    "layout (std140, binding = 1) uniform Instance"
    "layout (std140, binding = 4) uniform Instance")

nfsu_case("a member added to a uniform block"
    fallback.frag.glsl car_fallback.manifest
    "uniform block Material at fragment binding 2: the manifest says 80 bytes, the compiled shader declares 96"
    OWN
    "    vec4 environment_color;\n} material;"
    "    vec4 environment_color;\n    vec4 extra;\n} material;")

nfsu_case("a varying's interpolation changed"
    textured.frag.glsl car_textured.manifest
    "varying location 2 (texcoord): the manifest says smooth, textured.frag.dksh interpolates it noperspective"
    OWN
    "layout (location = 2) in vec2 inTexcoord"
    "layout (location = 2) noperspective in vec2 inTexcoord")

nfsu_case("a varying moved in the vertex stage"
    placed.vert.glsl track_fallback.manifest
    "placed.vert.dksh writes varying location 3 (xyz), which the manifest does not declare"
    OWN
    "layout (location = 1) out vec3 outColor"
    "layout (location = 3) out vec3 outColor")

nfsu_case("a colour output moved"
    fallback.frag.glsl track_fallback.manifest
    "fallback.frag.dksh writes render target 1 (xyzw), which the manifest does not declare"
    OWN
    "layout (location = 0) out vec4 outColor"
    "layout (location = 1) out vec4 outColor")

# A blob and a TGSI from different compiles. The varyings are where the
# shader program header and the TGSI overlap, so that is where the check
# can tell; bindings and block sizes are in the TGSI alone
# (docs/SHADER_PIPELINE.md).
nfsu_case("a vertex blob with the TGSI of another compile"
    placed.vert.glsl track_fallback.manifest
    "placed.vert.tgsi and placed.vert.dksh disagree on varying location 3: they are not one compile"
    UNCHANGED
    "layout (location = 1) out vec3 outColor"
    "layout (location = 3) out vec3 outColor")

nfsu_case("a fragment blob with the TGSI of another compile"
    fallback.frag.glsl track_fallback.manifest
    "fallback.frag.tgsi and fallback.frag.dksh disagree on varying location 3: they are not one compile"
    UNCHANGED
    "layout (location = 1) in vec3 inColor"
    "layout (location = 3) in vec3 inColor")

file(REMOVE_RECURSE "${work}" "${base}")
if(NOT failed EQUAL 0)
    message(FATAL_ERROR "shader blob guards: ${failed} failed, ${caught} caught\n${report}")
endif()
message(STATUS "${report}shader blob guards: ${manifest_count} manifests pass against "
    "the compiled stages, ${caught} changes caught, none missed")
