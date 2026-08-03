cmake_minimum_required(VERSION 3.30)

#versioning
project(OVFToolkit VERSION 0.2 DESCRIPTION "Vector field files interface library" LANGUAGES CXX C)

#TODO: add some more checks
# C++ standard
set(CMAKE_CXX_STANDARD 23)              #a lot of c++17 features used in core
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXSourceCompiles)
include(GNUInstallDirs)

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
    check_cxx_source_compiles([=[
        #include <cstddef>
        #include <kokkos/mdspan/mdspan.hpp>

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
# using boost for endianess, need to check if it is available!
cmake_policy(SET CMP0167 NEW)
find_package(Boost 1.58.0 REQUIRED COMPONENTS program_options)

#library build file
include(GenerateExportHeader)
#like in windows make only certain function exported
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

#find interfaces for Mathematica
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/external/FindMathematica/CMake/Mathematica" ${CMAKE_MODULE_PATH})
find_package(Mathematica COMPONENTS WSTP)

#find fftw frameworks and other components for time fourier of magnetization files
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake/findFFTW" ${CMAKE_MODULE_PATH})
#try to find FFTW(3.0), cuFFTW and GSL for fourier analysis
find_package(FFTW COMPONENTS FLOAT_THREADS_LIB)
include(CheckLanguage)
CHECK_LANGUAGE(CUDA)
IF(CMAKE_CUDA_COMPILER)
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
    ELSE()
        find_package(CUDA COMPONENTS cuFFT)
    ENDIF()
ELSE()
    MESSAGE(STATUS "Cuda was not found!")
ENDIF()
#TODO: implement backend switching later
set(BUILD_FFUTIL OFF)
if(FFTW_FOUND AND CUDA_FOUND AND Boost_PROGRAM_OPTIONS_FOUND)
    set(BUILD_FFUTIL ON)
endif()
IF(BUILD_FFUTIL)
    SET(FFMESSAGE "Building fourier analysis utility with following fftw engines: ")
    IF(FFTW_FOUND)
        STRING(APPEND FFMESSAGE "FFTW, ")
    ENDIF()
    IF(CUDA_FOUND)
        STRING(APPEND FFMESSAGE "cuFFTW v." ${CMAKE_CUDA_COMPILER_VERSION})
    ENDIF()
    MESSAGE(STATUS ${FFMESSAGE})
ENDIF()

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

target_compile_options(ovfparser PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors -Wno-strict-aliasing>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

add_subdirectory(tools)

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
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")

add_subdirectory(docs)

#include tests
IF(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
ENDIF()

IF(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
ENDIF()

option(OVFTOOLKIT_BUILD_WOLFRAM_BINDING
    "Build the Wolfram Language WSTP binding when Mathematica is available" ON)
add_subdirectory(bindings)
