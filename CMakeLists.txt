cmake_minimum_required(VERSION 3.20)
project(femm42 VERSION 4.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Common include path for portable compatibility headers
set(FEMM_COMPAT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/solvers/common")

# BLAS integration: Apple Accelerate on macOS, OpenBLAS/MKL on others
if(APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    if(ACCELERATE_FRAMEWORK)
        set(FEMM_BLAS_LIBS ${ACCELERATE_FRAMEWORK})
        set(FEMM_HAS_BLAS TRUE)
        message(STATUS "Using Apple Accelerate for BLAS")
    endif()
else()
    find_package(BLAS QUIET)
    if(BLAS_FOUND)
        set(FEMM_BLAS_LIBS ${BLAS_LIBRARIES})
        set(FEMM_HAS_BLAS TRUE)
        message(STATUS "Using BLAS: ${BLAS_LIBRARIES}")
    endif()
endif()

if(FEMM_HAS_BLAS)
    add_compile_definitions(FEMM_USE_BLAS)
endif()

# GPU backend: Metal on macOS, CUDA on NVIDIA (future)
set(FEMM_GPU_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/solvers/gpu")
if(APPLE)
    enable_language(OBJCXX)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if(METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
        set(FEMM_HAS_METAL TRUE)
        set(FEMM_METAL_SHADER_PATH "${CMAKE_SOURCE_DIR}/solvers/gpu/kernels.metal")
        add_compile_definitions(FEMM_USE_METAL)
        add_compile_definitions(FEMM_METAL_SHADER_PATH="${FEMM_METAL_SHADER_PATH}")
        message(STATUS "Metal GPU backend enabled")

        # Precompile Metal shaders to .metallib if Xcode metal compiler available
        set(FEMM_METALLIB_PATH "${CMAKE_BINARY_DIR}/kernels.metallib")
        execute_process(
            COMMAND xcrun --find metal
            RESULT_VARIABLE METAL_COMPILER_FOUND
            OUTPUT_QUIET ERROR_QUIET
        )
        if(METAL_COMPILER_FOUND EQUAL 0)
            add_custom_command(
                OUTPUT ${FEMM_METALLIB_PATH}
                COMMAND xcrun -sdk macosx metal -c
                        "${CMAKE_SOURCE_DIR}/solvers/gpu/kernels.metal"
                        -o "${CMAKE_BINARY_DIR}/kernels.air"
                COMMAND xcrun -sdk macosx metallib
                        "${CMAKE_BINARY_DIR}/kernels.air"
                        -o ${FEMM_METALLIB_PATH}
                DEPENDS "${CMAKE_SOURCE_DIR}/solvers/gpu/kernels.metal"
                COMMENT "Compiling Metal shaders to kernels.metallib"
            )
            add_custom_target(metal_shaders ALL DEPENDS ${FEMM_METALLIB_PATH})
            message(STATUS "Metal shader precompilation enabled")
        else()
            message(STATUS "Metal compiler not found (install Xcode for precompiled shaders)")
            add_custom_target(metal_shaders)  # dummy target
        endif()

        # Build GPU backend as a static library
        add_library(femm_gpu STATIC
            solvers/gpu/metal_backend.mm
        )
        add_dependencies(femm_gpu metal_shaders)
        target_include_directories(femm_gpu PUBLIC
            ${FEMM_COMPAT_INCLUDE_DIR}
            ${FEMM_GPU_INCLUDE_DIR}
        )
        target_compile_definitions(femm_gpu PUBLIC
            FEMM_METALLIB_PATH="${FEMM_METALLIB_PATH}"
        )
        target_link_libraries(femm_gpu PUBLIC
            ${METAL_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
        )
        set_source_files_properties(solvers/gpu/metal_backend.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()
endif()

# Libraries
add_subdirectory(liblua)
add_subdirectory(triangle)

# Solvers
add_subdirectory(fkn)
add_subdirectory(belasolv)
add_subdirectory(hsolv)
add_subdirectory(csolv)

# Qt 6 GUI (optional — requires Qt 6 to be installed)
find_package(Qt6 QUIET COMPONENTS Widgets Core Gui)
if(Qt6_FOUND)
    message(STATUS "Qt 6 found — building femm-gui")
    add_subdirectory(gui)
else()
    message(STATUS "Qt 6 not found — skipping GUI (install with: brew install qt@6)")
endif()

# Unit Tests (requires Qt 6 Test module)
find_package(Qt6 QUIET COMPONENTS Test)
if(Qt6_FOUND AND Qt6Test_FOUND)
    enable_testing()
    message(STATUS "Qt 6 Test found — building unit tests")
    add_subdirectory(tests)
else()
    message(STATUS "Qt 6 Test not found — skipping unit tests")
endif()
