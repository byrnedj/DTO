cmake_minimum_required(VERSION 3.0)
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

