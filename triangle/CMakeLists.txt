add_executable(triangle triangle.c)
target_compile_definitions(triangle PRIVATE
    ANSI_DECLARATORS
    NO_TIMER
)

# Suppress warnings in third-party code
target_compile_options(triangle PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/W0>
)

# Library version for in-process use (TRILIBRARY disables main(), enables triangulate() API)
add_library(triangle_lib STATIC triangle.c)
target_compile_definitions(triangle_lib PUBLIC TRILIBRARY ANSI_DECLARATORS NO_TIMER REAL=double VOID=int)
target_compile_options(triangle_lib PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/W0>
)
target_include_directories(triangle_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
