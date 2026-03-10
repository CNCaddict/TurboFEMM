set(CSOLV_SOURCES
    ${CMAKE_SOURCE_DIR}/solvers/common/femm_complex.cpp
    CUTHILL.CPP
    MAIN.CPP
    cspars.cpp
    femmedoccore.cpp
    PROB1BIG.CPP
)

if(WIN32)
    list(APPEND CSOLV_SOURCES
        csolv.cpp
        CSOLVDLG.CPP
        STDAFX.CPP
    )
endif()

add_executable(csolv ${CSOLV_SOURCES})

target_include_directories(csolv PRIVATE
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(FEMM_HAS_BLAS)
    target_link_libraries(csolv PRIVATE ${FEMM_BLAS_LIBS})
endif()

if(FEMM_HAS_METAL)
    target_link_libraries(csolv PRIVATE femm_gpu)
endif()

if(NOT WIN32)
    target_compile_options(csolv PRIVATE
        -Wno-writable-strings
        -Wno-format
        -Wno-dangling-else
        -Wno-logical-op-parentheses
        -Wno-sometimes-uninitialized
    )
endif()
