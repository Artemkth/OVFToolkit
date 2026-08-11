cmake_minimum_required(VERSION 3.30)

#versioning
project(OVFToolkit VERSION 0.2.1 DESCRIPTION "Vector field files interface library" LANGUAGES CXX C)

#TODO: add some more checks
# C++ standard
set(CMAKE_CXX_STANDARD 23)              #a lot of c++17 features used in core
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXSourceCompiles)
include(GNUInstallDirs)

function(ovftoolkit_install_executable target)
    install(TARGETS ${target}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    if(WIN32)
        # vcpkg's app-local deployment places the complete runtime DLL closure
        # beside each executable after it is built.  Install that closure rather
        # than scanning the host system or relying on incomplete imported-target
        # metadata from some packages.
        install(DIRECTORY "$<TARGET_FILE_DIR:${target}>/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            FILES_MATCHING PATTERN "*.dll")
    endif()
endfunction()

macro(ovftoolkit_tristate_option name description)
    set(${name} AUTO CACHE STRING "${description}: AUTO, ON, or OFF")
    set_property(CACHE ${name} PROPERTY STRINGS AUTO ON OFF)
    string(TOUPPER "${${name}}" ${name})
    if(NOT ${name} MATCHES "^(AUTO|ON|OFF)$")
        message(FATAL_ERROR "${name} must be AUTO, ON, or OFF")
    endif()
endmacro()

ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_TOOLS
    "Build OVFToolkit command-line tools")
ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_DOCS
    "Configure the Doxygen documentation target")
ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_WOLFRAM_BINDING
    "Build the Wolfram Language WSTP binding")
ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_MUMAX_RUNNER
    "Build the GPL-licensed interactive mumax3 runner")
ovftoolkit_tristate_option(OVFTOOLKIT_USE_FFTW
    "Use the FFTW CPU backend for Fourier analysis tools")
ovftoolkit_tristate_option(OVFTOOLKIT_USE_CUFFT
    "Use the NVIDIA cuFFT backend for Fourier analysis tools")

check_cxx_source_compiles([=[
    #include <cstddef>
    #include <mdspan>

    #ifndef __cpp_lib_mdspan
    #  error "Native mdspan feature macro is missing"
    #elif __cpp_lib_mdspan < 202207L
    #  error "Native mdspan implementation is too old"
    #endif

    int main()
    {
        int storage[6]{};

        std::mdspan<
            int,
            std::extents<std::size_t, 2, 3>
        > view{storage};

        return static_cast<int>(view.extent(0));
    }
]=] OVFTOOLKIT_HAS_STD_MDSPAN)

if(OVFTOOLKIT_HAS_STD_MDSPAN)
  message(STATUS "OVFToolkit: using native std::mdspan")
else()
    set(CMAKE_REQUIRED_INCLUDES
        "${CMAKE_SOURCE_DIR}/externals/mdspan/include")
    check_cxx_source_compiles([=[
        #include <cstddef>
        #include <mdspan/mdspan.hpp>

        int main()
        {
            int storage[6]{};

            Kokkos::mdspan<
                int,
                Kokkos::extents<std::size_t, 2, 3>
            > view{storage};

            return static_cast<int>(view.extent(0));
        }
    ]=] OVFTOOLKIT_HAS_KOKKOS_MDSPAN)
    unset(CMAKE_REQUIRED_INCLUDES)

    if(NOT OVFTOOLKIT_HAS_KOKKOS_MDSPAN)
        message(FATAL_ERROR
            "Neither native std::mdspan nor the Kokkos reference "
            "implementation is usable."
        )
    endif()

    message(STATUS "OVFToolkit: using Kokkos mdspan fallback")
endif()

# also need some parallelization libs
find_package(Threads REQUIRED)
# The parser only needs Boost headers. Command-line tools additionally use
# program_options, which should not be a build dependency of Python wheels.
cmake_policy(SET CMP0167 NEW)
find_package(Boost 1.58.0 REQUIRED)
set(OVFTOOLKIT_BUILD_TOOLS_ENABLED OFF)
if(NOT OVFTOOLKIT_BUILD_TOOLS STREQUAL "OFF")
    find_package(Boost 1.58.0 COMPONENTS program_options QUIET)
    if(Boost_PROGRAM_OPTIONS_FOUND)
        set(OVFTOOLKIT_BUILD_TOOLS_ENABLED ON)
    elseif(OVFTOOLKIT_BUILD_TOOLS STREQUAL "ON")
        message(FATAL_ERROR
            "OVFTOOLKIT_BUILD_TOOLS=ON requires Boost.Program_options")
    else()
        message(STATUS
            "Command-line tools disabled: Boost.Program_options was not found")
    endif()
endif()
if(OVFTOOLKIT_BUILD_MUMAX_RUNNER STREQUAL "ON" AND
   NOT OVFTOOLKIT_BUILD_TOOLS_ENABLED)
    message(FATAL_ERROR
        "OVFTOOLKIT_BUILD_MUMAX_RUNNER=ON requires command-line tools")
endif()

#library build file
include(GenerateExportHeader)
#like in windows make only certain function exported
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

#find interfaces for Mathematica
set(OVFTOOLKIT_BUILD_WOLFRAM_BINDING_ENABLED OFF)
if(NOT OVFTOOLKIT_BUILD_WOLFRAM_BINDING STREQUAL "OFF")
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/FindMathematica/CMake/Mathematica" ${CMAKE_MODULE_PATH})
    find_package(Mathematica COMPONENTS WSTP QUIET)
    if(Mathematica_WSTP_FOUND)
        set(OVFTOOLKIT_BUILD_WOLFRAM_BINDING_ENABLED ON)
    elseif(OVFTOOLKIT_BUILD_WOLFRAM_BINDING STREQUAL "ON")
        message(FATAL_ERROR
            "OVFTOOLKIT_BUILD_WOLFRAM_BINDING=ON requires Mathematica WSTP")
    else()
        message(STATUS "Wolfram binding disabled: Mathematica WSTP was not found")
    endif()
endif()

#find fftw frameworks and other components for time fourier of magnetization files
if(OVFTOOLKIT_BUILD_TOOLS_ENABLED)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/findFFTW" ${CMAKE_MODULE_PATH})
# Try to find either CPU FFTW or CUDA/cuFFT for Fourier analysis.  The
# threaded FFTW companion library is optional; the serial float library is
# sufficient for the CPU backend.
set(OVFTOOLKIT_FFTW_FLOAT_TARGET FFTW::Float)
set(FFTW_FLOAT_LIB_FOUND OFF)
set(FFTW_FLOAT_THREADS_LIB_FOUND OFF)
if(NOT OVFTOOLKIT_USE_FFTW STREQUAL "OFF")
    find_package(FFTW COMPONENTS FLOAT_LIB
        OPTIONAL_COMPONENTS FLOAT_THREADS_LIB)

    # Prefer a configuration-aware package target when one is available.  The
    # legacy FindFFTW module exposes a single library path, which can make a
    # multi-config generator link a Debug archive into Release targets.
    find_package(FFTW3f CONFIG QUIET)
    if(TARGET FFTW3::fftw3f)
        set(OVFTOOLKIT_FFTW_FLOAT_TARGET FFTW3::fftw3f)
    endif()

    if(OVFTOOLKIT_USE_FFTW STREQUAL "ON" AND NOT FFTW_FLOAT_LIB_FOUND)
        message(FATAL_ERROR
            "OVFTOOLKIT_USE_FFTW=ON requires the single-precision FFTW library")
    endif()
endif()

if(FFTW_VERSION)
    set(OVFTOOLKIT_FFTW_VERSION "${FFTW_VERSION}")
endif()
if(NOT OVFTOOLKIT_FFTW_VERSION AND FFTW_FLOAT_LIB_FOUND AND FFTW_INCLUDE_DIRS)
    file(STRINGS "${FFTW_INCLUDE_DIRS}/fftw3.h" OVFTOOLKIT_FFTW_VERSION_LINE
        REGEX "^#define[ \t]+FFTW_VERSION[ \t]+\"")
    if(OVFTOOLKIT_FFTW_VERSION_LINE)
        string(REGEX REPLACE ".*\"fftw-([^\"]+)\".*" "\\1"
            OVFTOOLKIT_FFTW_VERSION "${OVFTOOLKIT_FFTW_VERSION_LINE}")
    endif()
endif()
include(CheckLanguage)
set(OVFTOOLKIT_CUFFT_FOUND OFF)
if(NOT OVFTOOLKIT_USE_CUFFT STREQUAL "OFF")
    check_language(CUDA)
endif()
IF(NOT OVFTOOLKIT_USE_CUFFT STREQUAL "OFF" AND CMAKE_CUDA_COMPILER)
    # CUDA 12 rejects GCC 15 by default. Using an older G++ only for nvcc can
    # mix incompatible C++ runtimes at the final G++ 15 link, so keep one host
    # toolchain throughout and explicitly allow the unsupported combination.
    execute_process(
        COMMAND "${CMAKE_CUDA_COMPILER}" --version
        OUTPUT_VARIABLE OVFTOOLKIT_NVCC_VERSION_OUTPUT
        ERROR_QUIET)
    string(REGEX MATCH "release ([0-9]+)\\."
        OVFTOOLKIT_NVCC_VERSION_MATCH "${OVFTOOLKIT_NVCC_VERSION_OUTPUT}")
    set(OVFTOOLKIT_CUDA_MAJOR "${CMAKE_MATCH_1}")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15 AND
       OVFTOOLKIT_CUDA_MAJOR AND OVFTOOLKIT_CUDA_MAJOR LESS 13)
        set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}" CACHE FILEPATH
            "Host compiler used by nvcc" FORCE)
        if(NOT CMAKE_CUDA_FLAGS MATCHES "(^| )--allow-unsupported-compiler($| )")
            set(CMAKE_CUDA_FLAGS
                "${CMAKE_CUDA_FLAGS} --allow-unsupported-compiler"
                CACHE STRING "Flags used by the CUDA compiler" FORCE)
        endif()
        message(WARNING
            "CUDA ${OVFTOOLKIT_CUDA_MAJOR} with GCC ${CMAKE_CXX_COMPILER_VERSION}: "
            "enabling --allow-unsupported-compiler")
    endif()

    ENABLE_LANGUAGE(CUDA)
    SET(CUDA_FOUND On)
    IF(CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
        find_package(CUDAToolkit COMPONENTS cuFFT)
        if(TARGET CUDA::cufft)
            set(OVFTOOLKIT_CUFFT_FOUND ON)
        endif()
    ELSE()
        find_package(CUDA COMPONENTS cuFFT)
        if(CUDA_CUFFT_LIBRARIES)
            set(OVFTOOLKIT_CUFFT_FOUND ON)
        endif()
    ENDIF()
ELSEIF(NOT OVFTOOLKIT_USE_CUFFT STREQUAL "OFF")
    MESSAGE(STATUS "Cuda was not found!")
ENDIF()
if(OVFTOOLKIT_USE_CUFFT STREQUAL "ON" AND NOT OVFTOOLKIT_CUFFT_FOUND)
    message(FATAL_ERROR
        "OVFTOOLKIT_USE_CUFFT=ON requires a CUDA compiler and cuFFT")
endif()
set(BUILD_FFUTIL OFF)
if((FFTW_FLOAT_LIB_FOUND OR OVFTOOLKIT_CUFFT_FOUND) AND Boost_PROGRAM_OPTIONS_FOUND)
    set(BUILD_FFUTIL ON)
endif()
IF(BUILD_FFUTIL)
    SET(FFMESSAGE "Building Fourier analysis utility with engines: ")
    IF(FFTW_FLOAT_LIB_FOUND)
        if(OVFTOOLKIT_FFTW_VERSION)
            STRING(APPEND FFMESSAGE "FFTW ${OVFTOOLKIT_FFTW_VERSION}")
        else()
            STRING(APPEND FFMESSAGE "FFTW (version unknown)")
        endif()
        if(FFTW_FLOAT_THREADS_LIB_FOUND)
            STRING(APPEND FFMESSAGE " [threads]")
        endif()
    ENDIF()
    IF(OVFTOOLKIT_CUFFT_FOUND)
        if(FFTW_FLOAT_LIB_FOUND)
            STRING(APPEND FFMESSAGE ", ")
        endif()
        STRING(APPEND FFMESSAGE "cuFFT (CUDA ${CMAKE_CUDA_COMPILER_VERSION})")
    ENDIF()
    MESSAGE(STATUS "${FFMESSAGE}")
ENDIF()
else()
    set(BUILD_FFUTIL OFF)
    set(OVFTOOLKIT_CUFFT_FOUND OFF)
endif()

IF(MSVC)
	add_definitions(-DNOMINMAX)
ENDIF()
SET(CMAKE_INSTALL_RPATH $ORIGIN)
#ovfparser library
add_library(ovfparser
    OVFParser/OVFHeader.cpp
    OVFParser/VField.cpp
    OVFParser/OVFGrammar.cpp
    OVFParser/VField.cpp
    OVFParser/OVFReader.cpp
    OVFParser/OVFWriter.cpp
)
set_target_properties(ovfparser PROPERTIES POSITION_INDEPENDENT_CODE ON)
generate_export_header(ovfparser EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/OVFParser/ovfparser_export.h")

configure_file(OVFToolkitConfig.h.in OVFToolkitConfig.h)
target_include_directories(ovfparser PUBLIC 
    $<INSTALL_INTERFACE:OVFParser>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/OVFParser>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/OVFParser>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
    PRIVATE
    #boost for building internals
    "${Boost_INCLUDE_DIRS}"
    )

if(NOT OVFTOOLKIT_HAS_STD_MDSPAN)
    target_include_directories(ovfparser PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/externals/mdspan/include>)
endif()

target_compile_options(ovfparser PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors -Wno-strict-aliasing>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
endif()

if(OVFTOOLKIT_BUILD_TOOLS_ENABLED)
    add_subdirectory(tools)
endif()

#and install information for library
set(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}/OVFParser")
install(TARGETS ovfparser
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES
    OVFParser/OVFHeader.h
    OVFParser/VField.h
    OVFParser/OVFDictionary.h
    OVFParser/OVFParser.h
    OVFParser/OVFWriter.h
    "${PROJECT_BINARY_DIR}/OVFToolkitConfig.h"
    "${PROJECT_BINARY_DIR}/OVFParser/ovfparser_export.h" DESTINATION "${INCLUDE_INSTALL_DIR}"
)
if(NOT OVFTOOLKIT_HAS_STD_MDSPAN)
    install(DIRECTORY externals/mdspan/include/
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES externals/mdspan/LICENSE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        RENAME LICENSE-mdspan)
endif()
install(FILES
    LICENSE
    LICENSING.md
    THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY LICENSES/
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")

if(NOT OVFTOOLKIT_BUILD_DOCS STREQUAL "OFF")
    add_subdirectory(docs)
endif()

IF(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
ENDIF()

add_subdirectory(bindings)
