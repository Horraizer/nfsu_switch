# Applied before any project target is declared, inherited by subdirectories.
# The order matters: disabling fast math first, then contraction explicitly,
# also overrides a fast-math option supplied in CMAKE_CXX_FLAGS. Do not override
# this policy on an individual target; the Switch object census checks it.
if(MSVC)
    add_compile_options(/fp:strict)
else()
    add_compile_options(-fno-fast-math -ffp-contract=off)
endif()