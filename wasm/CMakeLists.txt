cmake_minimum_required(VERSION 3.20)
project(OpusOptimaWasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(OPUSOPTIMA_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(OPUSOPTIMA_THIRD "${OPUSOPTIMA_ROOT}/third_party")

set(HIGHS_NO_DEFAULT_THREADS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory("${OPUSOPTIMA_THIRD}/spdlog" spdlog)
add_subdirectory("${OPUSOPTIMA_THIRD}/HiGHS" highs)

set(OPUSOPTIMA_INCLUDE_DIRS
    "${OPUSOPTIMA_ROOT}/src"
    "${OPUSOPTIMA_ROOT}/src/Core"
    "${OPUSOPTIMA_ROOT}/src/IO"
    "${OPUSOPTIMA_ROOT}/src/Solvers"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Core"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Planning"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Chemistry"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Execution"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Layout"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Compose"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Production"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Optimization"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Legacy"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Architecture"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Standard"
)
set(OPUSOPTIMA_THIRD_INCLUDE_DIRS
    "${OPUSOPTIMA_THIRD}/spdlog/include"
    "${OPUSOPTIMA_THIRD}/HiGHS"
    "${OPUSOPTIMA_THIRD}/HiGHS/highs"
    "${OPUSOPTIMA_THIRD}/omsim"
)

file(GLOB_RECURSE OPUSOPTIMA_STANDARD_SOURCES CONFIGURE_DEPENDS
    "${OPUSOPTIMA_ROOT}/src/Solvers/Standard/*.cpp")
set(OPUSOPTIMA_CORE_SOURCES
    "${OPUSOPTIMA_ROOT}/src/Core/LogManager.cpp"
    "${OPUSOPTIMA_ROOT}/src/IO/BinaryParser.cpp"
    "${OPUSOPTIMA_ROOT}/src/IO/BinaryWriter.cpp"
    "${OPUSOPTIMA_ROOT}/src/Solvers/Core/BaseSolver.cpp"
)

add_library(opusoptima_wasm_core STATIC
    ${OPUSOPTIMA_STANDARD_SOURCES}
    ${OPUSOPTIMA_CORE_SOURCES}
)
target_include_directories(opusoptima_wasm_core PUBLIC
    ${OPUSOPTIMA_INCLUDE_DIRS}
    ${OPUSOPTIMA_THIRD_INCLUDE_DIRS}
)
target_link_libraries(opusoptima_wasm_core PUBLIC highs spdlog::spdlog)
target_compile_options(opusoptima_wasm_core PRIVATE
    -O3 -fexceptions -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-sign-compare
)

set(OPUSOPTIMA_OMSIM_SOURCES
    "${OPUSOPTIMA_THIRD}/omsim/collision.c"
    "${OPUSOPTIMA_THIRD}/omsim/decode.c"
    "${OPUSOPTIMA_THIRD}/omsim/parse.c"
    "${OPUSOPTIMA_THIRD}/omsim/sim.c"
    "${OPUSOPTIMA_THIRD}/omsim/steady-state.c"
    "${OPUSOPTIMA_THIRD}/omsim/verifier.c"
)
add_library(opusoptima_omsim_wasm STATIC ${OPUSOPTIMA_OMSIM_SOURCES})
target_include_directories(opusoptima_omsim_wasm PUBLIC "${OPUSOPTIMA_THIRD}/omsim")
target_compile_options(opusoptima_omsim_wasm PRIVATE -O2 -D_DEFAULT_SOURCE -Wno-missing-braces)

add_executable(opusoptima "${CMAKE_CURRENT_LIST_DIR}/Api.cpp")
target_compile_options(opusoptima PRIVATE -fexceptions)
target_link_libraries(opusoptima PRIVATE opusoptima_wasm_core opusoptima_omsim_wasm)
set_target_properties(opusoptima PROPERTIES OUTPUT_NAME "opusoptima" SUFFIX ".js")

target_link_options(opusoptima PRIVATE
    "--no-entry"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=OpusOptimaModule"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=67108864"
    "-sSTACK_SIZE=8388608"
    "-sFILESYSTEM=1"
    "-sEXPORTED_FUNCTIONS=_malloc,_free,_opusoptima_solve,_opusoptima_verify,_opusoptima_describe,_opusoptima_free"
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8,HEAPU32"
    "-sASSERTIONS=0"
    "-fexceptions"
    "-sDISABLE_EXCEPTION_CATCHING=0"
)
