cmake_minimum_required(VERSION 3.20)

project(OpusOptima VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Project settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Third-party dependencies
# -----------------------------------------------------------------------------
add_subdirectory(third_party/spdlog)
add_subdirectory(third_party/HiGHS)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/benchmark)

set(OPUSOPTIMA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(OPUSOPTIMA_UNIT_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_test")
set(OPUSOPTIMA_FUNCTION_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/functional_test")
set(OPUSOPTIMA_BENCHMARK_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_test")

# -----------------------------------------------------------------------------
# Warning flags for project code (third-party suppression handled via SYSTEM)
# -----------------------------------------------------------------------------
set(OPUSOPTIMA_WARN_FLAGS
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-invalid-offsetof
)

# -----------------------------------------------------------------------------
# Source layout
# -----------------------------------------------------------------------------
file(GLOB_RECURSE OPUSOPTIMA_LIBRARY_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_SRC_DIR}/*.cpp")
list(FILTER OPUSOPTIMA_LIBRARY_SOURCES EXCLUDE REGEX "/main\\.cpp$")

# Project include dirs (warnings enabled)
set(OPUSOPTIMA_INCLUDE_DIRS
    "${OPUSOPTIMA_SRC_DIR}"
    "${OPUSOPTIMA_SRC_DIR}/Core"
    "${OPUSOPTIMA_SRC_DIR}/Engine"
    "${OPUSOPTIMA_SRC_DIR}/IO"
    "${OPUSOPTIMA_SRC_DIR}/Render"
    "${OPUSOPTIMA_SRC_DIR}/Solvers"
)

# Third-party include dirs (warnings suppressed by SYSTEM)
set(OPUSOPTIMA_THIRD_PARTY_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/HiGHS"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/HiGHS/highs"
)

# -----------------------------------------------------------------------------
# Main targets
# -----------------------------------------------------------------------------
add_library(OpusOptimaLib ${OPUSOPTIMA_LIBRARY_SOURCES})
target_include_directories(OpusOptimaLib PUBLIC ${OPUSOPTIMA_INCLUDE_DIRS})
target_include_directories(OpusOptimaLib SYSTEM PUBLIC ${OPUSOPTIMA_THIRD_PARTY_INCLUDE_DIRS})
target_compile_options(OpusOptimaLib PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
target_link_libraries(OpusOptimaLib PUBLIC highs)

add_executable(OpusOptima "${OPUSOPTIMA_SRC_DIR}/main.cpp")
target_compile_options(OpusOptima PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
target_link_libraries(OpusOptima
    PRIVATE
        OpusOptimaLib
        spdlog::spdlog
        highs
)

# ── External verifier library ──
set(OPUSOPTIMA_LIBVERIFY "${CMAKE_CURRENT_SOURCE_DIR}/asset/libverify.so")
if(EXISTS ${OPUSOPTIMA_LIBVERIFY})
    add_library(opusoptima_verify SHARED IMPORTED)
    set_target_properties(opusoptima_verify PROPERTIES IMPORTED_LOCATION ${OPUSOPTIMA_LIBVERIFY})
    target_compile_definitions(OpusOptimaLib PRIVATE HAS_LIBVERIFY=1)
    target_link_libraries(OpusOptimaLib PRIVATE opusoptima_verify dl)
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    enable_testing()

    add_subdirectory(third_party/googletest)

    file(GLOB_RECURSE UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_UNIT_TEST_DIR}/*.cpp")
    file(GLOB_RECURSE FUNCTION_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_FUNCTION_TEST_DIR}/*.cpp")
    file(GLOB_RECURSE BENCHMARK_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_BENCHMARK_TEST_DIR}/*.cpp")

    set(OPUSOPTIMA_GTEST_SOURCES ${UNIT_TEST_SOURCES} ${FUNCTION_TEST_SOURCES})
    if(OPUSOPTIMA_GTEST_SOURCES)
        add_executable(OpusOptimaTests ${OPUSOPTIMA_GTEST_SOURCES})
        target_compile_options(OpusOptimaTests PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
        target_link_libraries(OpusOptimaTests PRIVATE gtest gtest_main OpusOptimaLib)
        if(TARGET opusoptima_verify)
            target_link_libraries(OpusOptimaTests PRIVATE opusoptima_verify dl)
            target_compile_definitions(OpusOptimaTests PRIVATE HAS_LIBVERIFY=1)
        endif()

        include(GoogleTest)
        gtest_discover_tests(OpusOptimaTests)
    endif()

    if(BENCHMARK_TEST_SOURCES)
        add_executable(OpusOptimaBenchmarks ${BENCHMARK_TEST_SOURCES})
        target_compile_options(OpusOptimaBenchmarks PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
        target_link_libraries(OpusOptimaBenchmarks PRIVATE benchmark::benchmark_main OpusOptimaLib)

        add_test(
            NAME OpusOptimaBenchmarks
            COMMAND OpusOptimaBenchmarks --benchmark_min_time=0.01
        )
    endif()
endif()
