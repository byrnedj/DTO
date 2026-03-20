cmake_minimum_required(VERSION 3.5...3.31)
project(DTO VERSION 1.0 LANGUAGES C)

include(GNUInstallDirs)
set(CMAKE_INSTALL_LIBDIR lib)

# Options to enable/disable library support (on by default)
option(DTO_ACCEL_CONFIG_SUPPORT "Enable accel-config for automatic DSA discovery (requires libaccel-config)" ON)
option(DTO_NUMA_SUPPORT "Enable NUMA awareness support (requires libnuma)" ON)

# Options to statically link dependencies into libdto.so
option(DTO_STATIC_ACCEL_CONFIG "Statically link accel-config into libdto.so" OFF)
option(DTO_STATIC_NUMA "Statically link numa into libdto.so (requires PIC-compiled libnuma.a)" OFF)

# Build the shared library
add_library(dto SHARED dto.c)
add_library(DTO::dto ALIAS dto)
target_include_directories(dto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# set gnu source everywhere
add_compile_definitions(_GNU_SOURCE)

# Add the -DDTO_STATS_SUPPORT preprocessor definition
target_compile_definitions(dto PRIVATE DTO_STATS_SUPPORT)

# Link libraries
set(DTO_STATIC_LIBS "")
set(DTO_DYNAMIC_LIBS dl)

# Handle accel-config support
if(DTO_ACCEL_CONFIG_SUPPORT)
    target_compile_definitions(dto PRIVATE DTO_ACCEL_CONFIG_SUPPORT)
    message(STATUS "accel-config support: enabled")

    if(DTO_STATIC_ACCEL_CONFIG)
        find_library(ACCEL_CONFIG_STATIC NAMES libaccel-config.a PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
        if(NOT ACCEL_CONFIG_STATIC)
            message(FATAL_ERROR "libaccel-config.a not found. Install static accel-config or disable DTO_STATIC_ACCEL_CONFIG.")
        endif()
        message(STATUS "Statically linking accel-config: ${ACCEL_CONFIG_STATIC}")
        list(APPEND DTO_STATIC_LIBS ${ACCEL_CONFIG_STATIC})
    else()
        list(APPEND DTO_DYNAMIC_LIBS accel-config)
    endif()
else()
    message(STATUS "accel-config support: disabled (DTO_WQ_LIST env var required at runtime)")
endif()

# Handle numa support
if(DTO_NUMA_SUPPORT)
    target_compile_definitions(dto PRIVATE DTO_NUMA_SUPPORT)
    message(STATUS "NUMA support: enabled")

    if(DTO_STATIC_NUMA)
        find_library(NUMA_STATIC NAMES libnuma.a PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
        if(NOT NUMA_STATIC)
            message(FATAL_ERROR "libnuma.a not found. Install static numa or disable DTO_STATIC_NUMA.")
        endif()
        message(STATUS "Statically linking numa: ${NUMA_STATIC}")
        message(STATUS "  (Note: libnuma.a must be compiled with -fPIC)")
        list(APPEND DTO_STATIC_LIBS ${NUMA_STATIC})
    else()
        list(APPEND DTO_DYNAMIC_LIBS numa)
    endif()
else()
    message(STATUS "NUMA support: disabled")
endif()

# Link everything together
if(DTO_STATIC_LIBS)
    target_link_libraries(dto
        -Wl,--whole-archive
        ${DTO_STATIC_LIBS}
        -Wl,--no-whole-archive
        ${DTO_DYNAMIC_LIBS}
    )
else()
    target_link_libraries(dto ${DTO_DYNAMIC_LIBS})
endif()

include(CheckCCompilerFlag)
check_c_compiler_flag("-mwaitpkg" HAS_WAITPKG)
if (HAS_WAITPKG)
  target_compile_options(dto PRIVATE -mwaitpkg -march=native)
endif()

# Build dto-test and dto-test-wodto
add_executable(dto-test dto-test.c)
target_link_libraries(dto-test PRIVATE DTO::dto pthread)

add_executable(dto-test-wodto dto-test.c)
target_link_libraries(dto-test-wodto PRIVATE pthread)

# ---- Test Suite ----
option(DTO_BUILD_TESTS "Build the DTO test suite" ON)

if(DTO_BUILD_TESTS)
    enable_testing()

    # Functional correctness tests (safe to run in CI without DSA hardware)
    add_executable(dto-test-functional tests/test_functional.c)
    target_link_libraries(dto-test-functional PRIVATE DTO::dto pthread)

    add_test(NAME functional COMMAND dto-test-functional)
    set_tests_properties(functional PROPERTIES
        LABELS "ci"
        TIMEOUT 120
    )

    # Performance test binaries
    add_executable(dto-test-perf tests/test_perf.c)
    target_link_libraries(dto-test-perf PRIVATE DTO::dto m)

    add_executable(dto-test-perf-cpu tests/test_perf.c)
    target_link_libraries(dto-test-perf-cpu PRIVATE m)

    add_executable(dto-test-perf-summary tests/perf_summary.c)

    # Common env vars for all perf tests
    # PERF_PASSES defaults to 3 in test_perf.c; override from shell env if needed
    set(PERF_COMMON_ENV
        "BASELINE_DIR=${CMAKE_SOURCE_DIR}/tests/baselines"
        "RESULTS_DIR=${CMAKE_BINARY_DIR}/perf_results"
        "PERF_TOLERANCE=5"
    )

    # --- Raw CPU (no DTO linked) ---
    add_test(NAME perf_cpu_4k COMMAND dto-test-perf-cpu)
    set_tests_properties(perf_cpu_4k PROPERTIES
        LABELS "ci;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=cpu"
    )
    add_test(NAME perf_cpu_2m COMMAND dto-test-perf-cpu)
    set_tests_properties(perf_cpu_2m PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=cpu;DTO_PERF_HUGE=1"
    )

    # --- DTO linked, CPU path only (DTO_USESTDC_CALLS=1) ---
    add_test(NAME perf_stdc_4k COMMAND dto-test-perf)
    set_tests_properties(perf_stdc_4k PROPERTIES
        LABELS "ci;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=stdc;DTO_USESTDC_CALLS=1"
    )
    add_test(NAME perf_stdc_2m COMMAND dto-test-perf)
    set_tests_properties(perf_stdc_2m PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=stdc;DTO_USESTDC_CALLS=1;DTO_PERF_HUGE=1"
    )

    # --- DTO + DSA pure (CSF=0, no auto-tuning) ---
    add_test(NAME perf_dsa_4k COMMAND dto-test-perf)
    set_tests_properties(perf_dsa_4k PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=dsa;DTO_CPU_SIZE_FRACTION=0;DTO_AUTO_ADJUST_KNOBS=0;DTO_MIN_BYTES=4096"
    )
    add_test(NAME perf_dsa_2m COMMAND dto-test-perf)
    set_tests_properties(perf_dsa_2m PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=dsa;DTO_PERF_HUGE=1;DTO_CPU_SIZE_FRACTION=0;DTO_AUTO_ADJUST_KNOBS=0;DTO_MIN_BYTES=4096"
    )

    # --- DTO + DSA with auto-tuning (CSF=0.33, auto=1) ---
    add_test(NAME perf_dsa_auto_4k COMMAND dto-test-perf)
    set_tests_properties(perf_dsa_auto_4k PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=dsa_auto;DTO_CPU_SIZE_FRACTION=0.33;DTO_AUTO_ADJUST_KNOBS=1;DTO_MIN_BYTES=4096"
    )
    add_test(NAME perf_dsa_auto_2m COMMAND dto-test-perf)
    set_tests_properties(perf_dsa_auto_2m PROPERTIES
        LABELS "hardware;perf"
        TIMEOUT 600
        ENVIRONMENT "${PERF_COMMON_ENV};PERF_LABEL=dsa_auto;DTO_PERF_HUGE=1;DTO_CPU_SIZE_FRACTION=0.33;DTO_AUTO_ADJUST_KNOBS=1;DTO_MIN_BYTES=4096"
    )

    # --- Summary (runs after all perf tests via fixture) ---
    set_tests_properties(
        perf_cpu_4k perf_cpu_2m
        perf_stdc_4k perf_stdc_2m
        perf_dsa_4k perf_dsa_2m
        perf_dsa_auto_4k perf_dsa_auto_2m
        PROPERTIES FIXTURES_SETUP perf_data
    )

    add_test(NAME perf_summary COMMAND dto-test-perf-summary)
    set_tests_properties(perf_summary PROPERTIES
        LABELS "perf"
        TIMEOUT 10
        FIXTURES_CLEANUP perf_data
        ENVIRONMENT "RESULTS_DIR=${CMAKE_BINARY_DIR}/perf_results"
    )
endif()

# Install and export the library
install(TARGETS dto
        EXPORT DTOTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES dto.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT DTOTargets
        FILE DTOTargets.cmake
        NAMESPACE DTO::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DTO)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DTOConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/DTOConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DTOConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DTO)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DTOConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DTOConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DTO)

