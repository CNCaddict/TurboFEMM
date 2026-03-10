set(FKN_SOURCES
    ${CMAKE_SOURCE_DIR}/solvers/common/femm_complex.cpp
    spars.cpp
    cspars.cpp
    cuthill.cpp
    matprop.cpp
    prob1big.cpp
    prob2big.cpp
    prob3big.cpp
    prob4big.cpp
    femmedoccore.cpp
    fullmatrix.cpp
    main.cpp
)

if(WIN32)
    list(APPEND FKN_SOURCES
        fkn.cpp
        fknDlg.cpp
        StdAfx.cpp
    )
endif()

# Core solver as static library (no main.cpp) for in-process use
set(FKN_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/solvers/common/femm_complex.cpp
    spars.cpp
    cspars.cpp
    cuthill.cpp
    matprop.cpp
    prob1big.cpp
    prob2big.cpp
    prob3big.cpp
    prob4big.cpp
    femmedoccore.cpp
    fullmatrix.cpp
)
add_library(fkn_core STATIC ${FKN_CORE_SOURCES})
target_include_directories(fkn_core PUBLIC
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fkn_core PUBLIC liblua)
if(FEMM_HAS_BLAS)
    target_link_libraries(fkn_core PUBLIC ${FEMM_BLAS_LIBS})
endif()
if(FEMM_HAS_METAL)
    target_link_libraries(fkn_core PUBLIC femm_gpu)
endif()
if(NOT WIN32)
    target_compile_options(fkn_core PRIVATE
        -Wno-writable-strings
        -Wno-format
        -Wno-dangling-else
        -Wno-logical-op-parentheses
        -Wno-sometimes-uninitialized
    )
endif()

add_executable(fkn ${FKN_SOURCES})

target_include_directories(fkn PRIVATE
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fkn PRIVATE liblua)

if(FEMM_HAS_BLAS)
    target_link_libraries(fkn PRIVATE ${FEMM_BLAS_LIBS})
endif()

if(FEMM_HAS_METAL)
    target_link_libraries(fkn PRIVATE femm_gpu)
endif()

if(WIN32)
    # MFC settings for Windows
    set_target_properties(fkn PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
else()
    target_compile_options(fkn PRIVATE
        -Wno-writable-strings
        -Wno-format
        -Wno-dangling-else
        -Wno-logical-op-parentheses
        -Wno-sometimes-uninitialized
    )
endif()
