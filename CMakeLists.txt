cmake_minimum_required(VERSION 3.20)

project(nfsu_switch LANGUAGES CXX)

option(NFSU_BUILD_TESTS "Build unit tests" ON)

add_library(nfsu_core
    src/chunk.cpp
)

target_include_directories(nfsu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nfsu_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(nfsu_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(nfsu_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(nfsu_probe
    src/main.cpp
)

target_link_libraries(nfsu_probe PRIVATE nfsu_core)

if(NFSU_BUILD_TESTS)
    include(CTest)

    add_executable(nfsu_chunk_tests
        tests/chunk_tests.cpp
    )

    target_link_libraries(nfsu_chunk_tests PRIVATE nfsu_core)
    add_test(NAME nfsu_chunk_tests COMMAND nfsu_chunk_tests)
endif()

