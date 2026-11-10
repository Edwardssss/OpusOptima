cmake_minimum_required(VERSION 3.20)

project(OpusOptima VERSION 0.1.0 LANGUAGES CXX C)

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
# Static spdlog can be embedded in the shared project library through the verification core.
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
    "${OPUSOPTIMA_SRC_DIR}/Verification"
    "${OPUSOPTIMA_SRC_DIR}/Solvers"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Core"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Planning"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Chemistry"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Execution"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Layout"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Production"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Optimization"
    "${OPUSOPTIMA_SRC_DIR}/Solvers/Legacy"
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
set(OPUSOPTIMA_VERIFICATION_CORE_SOURCES
    "${OPUSOPTIMA_SRC_DIR}/Core/LogManager.cpp"
    "${OPUSOPTIMA_SRC_DIR}/IO/BinaryParser.cpp"
    "${OPUSOPTIMA_SRC_DIR}/IO/BinaryWriter.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/FiniteTapeExpansion.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/FiniteRepeatingGraph.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/FiniteRepeatingProgress.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/StaticProductionVerifier.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/IndependentMetrics.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/ContinuousCollision.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/ReferenceMotion.cpp"
    "${OPUSOPTIMA_SRC_DIR}/Verification/WorkerProtocol.cpp"
)
list(REMOVE_ITEM OPUSOPTIMA_LIBRARY_SOURCES ${OPUSOPTIMA_VERIFICATION_CORE_SOURCES})
list(FILTER OPUSOPTIMA_LIBRARY_SOURCES EXCLUDE REGEX "/Verification/(ReferenceBackend|ReferenceWorkerMain|IndependentWorkerMain)\\.cpp$")
add_library(OpusOptimaVerificationCore STATIC ${OPUSOPTIMA_VERIFICATION_CORE_SOURCES})
set_target_properties(OpusOptimaVerificationCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(OpusOptimaVerificationCore PUBLIC
    "${OPUSOPTIMA_SRC_DIR}" "${OPUSOPTIMA_SRC_DIR}/Core"
    "${OPUSOPTIMA_SRC_DIR}/IO" "${OPUSOPTIMA_SRC_DIR}/Verification")
target_compile_options(OpusOptimaVerificationCore PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
target_link_libraries(OpusOptimaVerificationCore PUBLIC spdlog::spdlog)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(OpusOptimaVerifyWorker "${OPUSOPTIMA_SRC_DIR}/Verification/IndependentWorkerMain.cpp")
    target_link_libraries(OpusOptimaVerifyWorker PRIVATE OpusOptimaVerificationCore)
    target_compile_options(OpusOptimaVerifyWorker PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
endif()

add_library(OpusOptimaLib ${OPUSOPTIMA_LIBRARY_SOURCES})
target_include_directories(OpusOptimaLib PUBLIC ${OPUSOPTIMA_INCLUDE_DIRS})
target_include_directories(OpusOptimaLib SYSTEM PUBLIC ${OPUSOPTIMA_THIRD_PARTY_INCLUDE_DIRS})
target_compile_options(OpusOptimaLib PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
target_link_libraries(OpusOptimaLib PUBLIC highs OpusOptimaVerificationCore)
if(TARGET OpusOptimaVerifyWorker)
    add_dependencies(OpusOptimaLib OpusOptimaVerifyWorker)
    target_compile_definitions(OpusOptimaLib PRIVATE
        "OPUSOPTIMA_INDEPENDENT_WORKER_PATH=\"${CMAKE_CURRENT_BINARY_DIR}/OpusOptimaVerifyWorker\"")
endif()

add_executable(OpusOptima "${OPUSOPTIMA_SRC_DIR}/main.cpp")
target_compile_options(OpusOptima PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
target_link_libraries(OpusOptima
    PRIVATE
        OpusOptimaLib
        spdlog::spdlog
        highs
)

# ── External verifier: ianh/omsim (third_party submodule) ──
#
# The native authority is the upstream omsim verifier FFI, built from the pinned
# submodule exactly as its own Makefile does:
#   $(CC) -O2 -std=c11 -g -shared -fpic -o libverify.so <SOURCE> -lm
# Nothing is vendored as a prebuilt binary; the pinned submodule commit is the
# reproducible anchor (see third_party/omsim and docs/IndependentVerifier.md).
option(OPUSOPTIMA_ENABLE_LIBVERIFY "Enable external solution verification (Linux)" ON)
set(OPUSOPTIMA_OMSIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/omsim")
set(OPUSOPTIMA_OMSIM_SOURCES
    collision.c
    decode.c
    parse.c
    sim.c
    steady-state.c
    verifier.c
)
if(OPUSOPTIMA_ENABLE_LIBVERIFY AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(OMSIM_SOURCE ${OPUSOPTIMA_OMSIM_SOURCES})
        if(NOT EXISTS "${OPUSOPTIMA_OMSIM_DIR}/${OMSIM_SOURCE}")
            message(FATAL_ERROR
                "The ianh/omsim verifier sources are missing "
                "(${OPUSOPTIMA_OMSIM_DIR}/${OMSIM_SOURCE}).\n"
                "Initialise the pinned submodule first:\n"
                "  git submodule update --init third_party/omsim\n"
                "Then reconfigure. Verification cannot be disabled silently: "
                "every published solution is judged by this gate.")
        endif()
    endforeach()
    set(OPUSOPTIMA_OMSIM_PATHS "")
    foreach(OMSIM_SOURCE ${OPUSOPTIMA_OMSIM_SOURCES})
        list(APPEND OPUSOPTIMA_OMSIM_PATHS "${OPUSOPTIMA_OMSIM_DIR}/${OMSIM_SOURCE}")
    endforeach()
    add_library(opusoptima_verify SHARED ${OPUSOPTIMA_OMSIM_PATHS})
    set_target_properties(opusoptima_verify PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "verify"
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(opusoptima_verify SYSTEM PUBLIC "${OPUSOPTIMA_OMSIM_DIR}")
    # Upstream builds this C11 file set with -O2 and _DEFAULT_SOURCE.
    target_compile_options(opusoptima_verify PRIVATE -O2 -D_DEFAULT_SOURCE -Wno-missing-braces)
    target_link_libraries(opusoptima_verify PRIVATE m)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${OPUSOPTIMA_OMSIM_DIR}/verifier.h")
    message(STATUS "Verifier: building libverify from third_party/omsim")

    add_executable(OpusOptimaReferenceWorker
        "${OPUSOPTIMA_SRC_DIR}/Verification/ReferenceWorkerMain.cpp"
        "${OPUSOPTIMA_SRC_DIR}/Verification/ReferenceBackend.cpp")
    target_link_libraries(OpusOptimaReferenceWorker PRIVATE OpusOptimaVerificationCore opusoptima_verify)
    target_compile_options(OpusOptimaReferenceWorker PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
    add_dependencies(OpusOptimaLib OpusOptimaReferenceWorker)
    target_compile_definitions(OpusOptimaLib PRIVATE
        "OPUSOPTIMA_REFERENCE_WORKER_PATH=\"${CMAKE_CURRENT_BINARY_DIR}/OpusOptimaReferenceWorker\"")
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    enable_testing()

    # Allocation failure injection lives in its own process, never the shared test runner.
    add_executable(FiniteTapeAllocationTest tests/worker/FiniteTapeAllocationTest.cpp)
    target_link_libraries(FiniteTapeAllocationTest PRIVATE OpusOptimaVerificationCore)
    target_compile_options(FiniteTapeAllocationTest PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
    add_test(NAME FiniteTapeAllocationTest COMMAND FiniteTapeAllocationTest)

    add_subdirectory(third_party/googletest)

    file(GLOB_RECURSE UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_UNIT_TEST_DIR}/*.cpp")
    file(GLOB_RECURSE FUNCTION_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_FUNCTION_TEST_DIR}/*.cpp")
    list(FILTER FUNCTION_TEST_SOURCES EXCLUDE REGEX "/ReferenceMotionDifferential\\.cpp$")
    file(GLOB_RECURSE BENCHMARK_TEST_SOURCES CONFIGURE_DEPENDS "${OPUSOPTIMA_BENCHMARK_TEST_DIR}/*.cpp")

    set(OPUSOPTIMA_GTEST_SOURCES ${UNIT_TEST_SOURCES} ${FUNCTION_TEST_SOURCES})
    if(OPUSOPTIMA_GTEST_SOURCES)
        add_executable(OpusOptimaTests ${OPUSOPTIMA_GTEST_SOURCES})
        # Shared test helpers (repo-root discovery, scratch directories).
        target_include_directories(OpusOptimaTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")
        target_compile_options(OpusOptimaTests PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
        target_link_libraries(OpusOptimaTests PRIVATE gtest gtest_main OpusOptimaLib)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(VerificationResourceProbe tests/worker/VerificationResourceProbe.cpp)
            target_link_libraries(VerificationResourceProbe PRIVATE OpusOptimaLib)
            target_compile_options(VerificationResourceProbe PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
            add_executable(OpusOptimaFaultVerifierWorker tests/worker/FaultWorker.cpp)
            target_link_libraries(OpusOptimaFaultVerifierWorker PRIVATE OpusOptimaVerificationCore)
            add_dependencies(OpusOptimaTests OpusOptimaFaultVerifierWorker)
            target_compile_definitions(OpusOptimaTests PRIVATE
                "OPUSOPTIMA_FAULT_WORKER_PATH=\"${CMAKE_CURRENT_BINARY_DIR}/OpusOptimaFaultVerifierWorker\"")
        endif()
        if(TARGET opusoptima_verify)
            target_link_libraries(OpusOptimaTests PRIVATE opusoptima_verify dl)
            target_compile_definitions(OpusOptimaTests PRIVATE HAS_LIBVERIFY=1)
        endif()

        include(GoogleTest)
        gtest_discover_tests(OpusOptimaTests)
        if(TARGET opusoptima_verify)
            add_test(NAME CliDefaultSelection
                COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:OpusOptima>
                    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/functional_test/CliDefaultSelection.cmake)
            add_test(NAME CliAcceptance
                COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:OpusOptima>
                    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/functional_test/CliAcceptance.cmake)
            function(add_cli_acceptance name puzzle output)
                add_test(NAME ${name}
                    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:OpusOptima>
                        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -DPUZZLE=${CMAKE_CURRENT_SOURCE_DIR}/${puzzle}
                        -DSOLVER=assembly -DOUTPUT_NAME=${output}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/functional_test/CliAcceptance.cmake)
            endfunction()
            add_cli_acceptance(CliBondedProductionAcceptance
                asset/puzzle/easy/double-conduit.puzzle cli-bonded-production.solution)
            add_cli_acceptance(CliMixedFeedCampaignAcceptance
                asset/puzzle/campaign/ch1-and-prologue/P011.puzzle cli-mixed-feed-campaign.solution)
            foreach(target IN ITEMS w2868339730 w2946687073 w2946687209)
                add_cli_acceptance(CliRealTarget_${target}
                    asset/puzzle/workshop-versions/${target}.puzzle cli-real-target-${target}.solution)
            endforeach()
            foreach(target IN ITEMS
                    edge-cases/c654382114940518
                    tourney-2020/OM2020_W7_HighGlossFinish
                    workshop-versions/w2501728349)
                string(REPLACE "/" "_" target_name "${target}")
                add_cli_acceptance(CliMaterialCapabilityRegression_${target_name}
                    asset/puzzle/${target}.puzzle cli-material-capability-${target_name}.solution)
            endforeach()
            add_cli_acceptance(CliDistributedPurificationOverloadedAcceptance
                asset/puzzle/tourney-2020/OM2020_W5_Overloaded.puzzle cli-distributed-purification-overloaded.solution)
            add_cli_acceptance(CliDistributedPurificationWorkshopOverloadedAcceptance
                asset/puzzle/workshop-versions/w2501728107.puzzle cli-distributed-purification-workshop-overloaded.solution)
            add_cli_acceptance(CliPetalAssemblyAntidoteAcceptance
                asset/puzzle/tourney-2021/OM2021_W4.puzzle cli-petal-assembly-antidote.solution)
            add_cli_acceptance(CliPetalAssemblyWorkshopAntidoteAcceptance
                asset/puzzle/workshop-versions/w2450512232.puzzle cli-petal-assembly-workshop-antidote.solution)
            add_cli_acceptance(CliDirectFeedWineAcceptance
                asset/puzzle/tourney-2019/week5.puzzle cli-direct-feed-wine.solution)
            add_cli_acceptance(CliDirectFeedWorkshopWineAcceptance
                asset/puzzle/workshop-versions/w1698786588.puzzle cli-direct-feed-workshop-wine.solution)
            add_cli_acceptance(CliCompactChamberOreAcceptance
                asset/puzzle/workshop-versions/w2591419339.puzzle cli-compact-chamber-ore.solution)
            add_cli_acceptance(CliRingProcessingProbeAcceptance
                asset/puzzle/workshop-versions/w2946684660.puzzle cli-ring-processing-probe.solution)
            add_cli_acceptance(CliFiniteStockReactorAcceptance
                asset/puzzle/workshop-versions/w2788067624.puzzle cli-finite-stock-reactor.solution)
            add_cli_acceptance(CliRoomAssemblyConduitBugAcceptance
                asset/puzzle/edge-cases/conduit-bug.puzzle cli-room-assembly-conduit-bug.solution)
            add_cli_acceptance(CliStaticVialConduitAcceptance
                asset/puzzle/edge-cases/c544897394273089.puzzle cli-static-vial-conduit.solution)
        endif()
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME GameProductionEvidenceReader
                COMMAND ${Python3_EXECUTABLE} -I ${OPUSOPTIMA_FUNCTION_TEST_DIR}/GameProductionEvidence.test.py)
            if(TARGET OpusOptimaVerifyWorker)
                set(OPUSOPTIMA_TEST_REFERENCE_AVAILABLE 0)
                if(TARGET OpusOptimaReferenceWorker)
                    set(OPUSOPTIMA_TEST_REFERENCE_AVAILABLE 1)
                endif()
                add_test(NAME IndependentVerifierCli
                    COMMAND ${Python3_EXECUTABLE} -I ${OPUSOPTIMA_FUNCTION_TEST_DIR}/IndependentVerifierCli.test.py
                        $<TARGET_FILE:OpusOptima> ${CMAKE_CURRENT_SOURCE_DIR} ${OPUSOPTIMA_TEST_REFERENCE_AVAILABLE})
            endif()
        endif()
    endif()

    if(BENCHMARK_TEST_SOURCES)
        add_executable(OpusOptimaBenchmarks ${BENCHMARK_TEST_SOURCES})
        target_compile_options(OpusOptimaBenchmarks PRIVATE ${OPUSOPTIMA_WARN_FLAGS})
        target_include_directories(OpusOptimaBenchmarks PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")
        target_link_libraries(OpusOptimaBenchmarks PRIVATE benchmark::benchmark_main OpusOptimaLib)

        add_test(
            NAME OpusOptimaBenchmarks
            COMMAND OpusOptimaBenchmarks --benchmark_min_time=0.01
        )
    endif()
endif()
