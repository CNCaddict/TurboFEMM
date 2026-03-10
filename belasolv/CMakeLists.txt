set(BELASOLV_SOURCES
    cuthill.cpp
    femmedoccore.cpp
    main.cpp
    spars.cpp
    prob1big.cpp
)

if(WIN32)
    list(APPEND BELASOLV_SOURCES
        belasolv.cpp
        belasolvDlg.cpp
        StdAfx.cpp
    )
endif()

add_executable(belasolv ${BELASOLV_SOURCES})

target_include_directories(belasolv PRIVATE
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
)

if(FEMM_HAS_BLAS)
    target_link_libraries(belasolv PRIVATE ${FEMM_BLAS_LIBS})
endif()

if(FEMM_HAS_METAL)
    target_link_libraries(belasolv PRIVATE femm_gpu)
endif()

if(NOT WIN32)
    target_compile_options(belasolv PRIVATE
        -Wno-writable-strings
        -Wno-format
        -Wno-dangling-else
        -Wno-logical-op-parentheses
        -Wno-sometimes-uninitialized
    )
endif()
