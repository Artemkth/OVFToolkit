find_package(HDF5 COMPONENTS C QUIET)
# VTK 9.3 ships a legacy Finddouble-conversion module that records only one
# library path.  Load the package's configuration-aware imported target first
# so multi-config builds select matching Debug and Release archives.
find_package(double-conversion CONFIG QUIET)
find_package(VTK COMPONENTS CommonCore CommonDataModel IOXML QUIET)

if(NOT HDF5_FOUND OR NOT VTK_FOUND OR NOT Boost_PROGRAM_OPTIONS_FOUND)
    set(_ovf_convert_reason "")
    if(NOT HDF5_FOUND)
        string(APPEND _ovf_convert_reason " HDF5")
    endif()
    if(NOT VTK_FOUND)
        string(APPEND _ovf_convert_reason " VTK")
    endif()
    if(NOT Boost_PROGRAM_OPTIONS_FOUND)
        string(APPEND _ovf_convert_reason " Boost.Program_options")
    endif()
    message(STATUS "ovf-convert disabled; missing:${_ovf_convert_reason}")
    return()
endif()

message(STATUS
    "Building ovf-convert with HDF5 ${HDF5_VERSION} and VTK ${VTK_VERSION}")

add_executable(ovf-convert main.cpp)
target_link_libraries(ovf-convert PRIVATE
    ovfparser
    Boost::program_options
    ${HDF5_LIBRARIES}
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::IOXML)
target_include_directories(ovf-convert PRIVATE ${HDF5_INCLUDE_DIRS})
target_compile_options(ovf-convert PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

vtk_module_autoinit(TARGETS ovf-convert MODULES ${VTK_LIBRARIES})

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_test(NAME OvfConvertSmoke
        COMMAND "${CMAKE_COMMAND}"
            "-DCONVERTER=$<TARGET_FILE:ovf-convert>"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/smoke"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke.cmake")
endif()

if(WIN32)
    set_target_properties(ovf-convert PROPERTIES LINK_FLAGS "wsetargv.obj")
endif()

install(TARGETS ovf-convert RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}/ovf-convert")
