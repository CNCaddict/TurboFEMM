# FEMM Unit Tests
cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Test)

# Enable Qt test framework
enable_testing()

# Test sources need access to the GUI source files
set(GUI_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../gui)

# Build a library from the core GUI sources (no main.cpp) for testing
set(TESTABLE_SOURCES
    ${GUI_SRC_DIR}/document.cpp
    ${GUI_SRC_DIR}/meshgen.cpp
    ${GUI_SRC_DIR}/resultsdoc.cpp
    ${GUI_SRC_DIR}/inprocesssolver.cpp
    ${GUI_SRC_DIR}/adaptiverefine.cpp
)

set(TESTABLE_HEADERS
    ${GUI_SRC_DIR}/document.h
    ${GUI_SRC_DIR}/meshgen.h
    ${GUI_SRC_DIR}/resultsdoc.h
    ${GUI_SRC_DIR}/inprocesssolver.h
    ${GUI_SRC_DIR}/adaptiverefine.h
    ${GUI_SRC_DIR}/femm_types.h
)

# Build the test executable
set(CMAKE_AUTOMOC ON)

add_executable(femm-tests
    test_document.cpp
    test_geometry.cpp
    test_mesh.cpp
    test_solver.cpp
    test_main.cpp
    ${TESTABLE_SOURCES}
    ${TESTABLE_HEADERS}
)

target_include_directories(femm-tests PRIVATE
    ${GUI_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/triangle
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/fkn
)

target_compile_definitions(femm-tests PRIVATE REAL=double VOID=int ANSI_DECLARATORS)

target_link_libraries(femm-tests PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    triangle_lib
    fkn_core
    liblua
)

# Define test data path
target_compile_definitions(femm-tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../test_problems"
)

# Register tests with CTest
add_test(NAME FemmDocumentTests COMMAND femm-tests)

# ---------------------------------------------------------------
# Performance Benchmark
# ---------------------------------------------------------------
set(BENCH_SOURCES
    ${GUI_SRC_DIR}/document.cpp
    ${GUI_SRC_DIR}/meshgen.cpp
    ${GUI_SRC_DIR}/resultsdoc.cpp
    ${GUI_SRC_DIR}/resultsoverlay.cpp
    ${GUI_SRC_DIR}/inprocesssolver.cpp
)

set(BENCH_HEADERS
    ${GUI_SRC_DIR}/document.h
    ${GUI_SRC_DIR}/meshgen.h
    ${GUI_SRC_DIR}/resultsdoc.h
    ${GUI_SRC_DIR}/resultsoverlay.h
    ${GUI_SRC_DIR}/inprocesssolver.h
    ${GUI_SRC_DIR}/femm_types.h
)

add_executable(femm-bench
    benchmark.cpp
    ${BENCH_SOURCES}
    ${BENCH_HEADERS}
)

target_include_directories(femm-bench PRIVATE
    ${GUI_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FEMM_COMPAT_INCLUDE_DIR}
    ${FEMM_GPU_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/triangle
    ${CMAKE_SOURCE_DIR}/fkn
)

target_compile_definitions(femm-bench PRIVATE REAL=double VOID=int ANSI_DECLARATORS)

target_link_libraries(femm-bench PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    triangle_lib
    fkn_core
    liblua
)

if(FEMM_HAS_METAL)
    target_link_libraries(femm-bench PRIVATE femm_gpu)
endif()

target_compile_definitions(femm-bench PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../test_problems"
)
