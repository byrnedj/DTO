cmake_minimum_required(VERSION 3.0)
project(DTO VERSION 1.0 LANGUAGES C)

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

# Link libraries
target_link_libraries(dto accel-config dl numa)

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

