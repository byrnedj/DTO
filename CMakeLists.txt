cmake_minimum_required(VERSION 3.5...3.31)
project(DTO VERSION 1.0 LANGUAGES C)

# Default to RelWithDebInfo when no build type is specified.
# (Skip for multi-config generators, which ignore CMAKE_BUILD_TYPE.)
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
        "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "No build type specified, defaulting to RelWithDebInfo")
endif()

include(GNUInstallDirs)
set(CMAKE_INSTALL_LIBDIR lib)

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

# Link libraries. Use PRIVATE so accel-config/numa/dl are the shared library's
# own dependencies (resolved via its DT_NEEDED) rather than INTERFACE
# requirements that would be forced onto every downstream consumer of DTO::dto.
target_link_libraries(dto PRIVATE dl accel-config numa)

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
