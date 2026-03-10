set(HSOLV_SOURCES
    ${CMAKE_SOURCE_DIR}/solvers/common/femm_complex.cpp
    CUTHILL.CPP
    MAIN.CPP
    SPARS.CPP
    hsolvdoc.cpp
    prob1big.cpp
)

if(WIN32)
    list(APPEND HSOLV_SOURCES
        hsolv.cpp
        hsolvDlg.cpp
        STDAFX.CPP
    )
endif()

add_executable(hsolv ${HSOLV_SOURCES})

target_include_directories(hsolv PRIVATE
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
)

if(FEMM_HAS_BLAS)
    target_link_libraries(hsolv PRIVATE ${FEMM_BLAS_LIBS})
endif()

if(FEMM_HAS_METAL)
    target_link_libraries(hsolv PRIVATE femm_gpu)
endif()

if(NOT WIN32)
    target_compile_options(hsolv PRIVATE
        -Wno-writable-strings
        -Wno-format
        -Wno-dangling-else
        -Wno-logical-op-parentheses
        -Wno-sometimes-uninitialized
    )
endif()
