# Retail ABK/AST parsing and PCM decode use only the C++ standard library.
# Both desktop and Switch configure this SDK-free library from one source list.
add_library(nfsu_audio src/audio.cpp)
target_include_directories(nfsu_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nfsu_audio PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_audio)
