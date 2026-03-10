set(LUA_SOURCES
    lapi.cpp
    lauxlib.cpp
    lbaselib.cpp
    lcode.cpp
    ldblib.cpp
    ldebug.cpp
    ldo.cpp
    lfunc.cpp
    lgc.cpp
    liolib.cpp
    llex.cpp
    lmathlib.cpp
    lmem.cpp
    lobject.cpp
    lparser.cpp
    lstate.cpp
    lstring.cpp
    lstrlib.cpp
    ltable.cpp
    ltests.cpp
    ltm.cpp
    lundump.cpp
    lvm.cpp
    lzio.cpp
)

add_library(liblua STATIC ${LUA_SOURCES})
target_include_directories(liblua PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FEMM_COMPAT_INCLUDE_DIR}
)

# Suppress warnings in third-party code
target_compile_options(liblua PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)
