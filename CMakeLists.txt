cmake_minimum_required(VERSION 3.10)

#versioning
project(OVFToolkit VERSION 0.1 DESCRIPTION "Vector field files interface library" LANGUAGES CXX C)

#TODO: add some more checks
# C++ standard
set(CMAKE_CXX_STANDARD 17)              #a lot of c++17 features used in core
set(CMAKE_CXX_STANDARD_REQUIRED True)
# also need some parallelization libs
find_package(Threads REQUIRED)
# using boost for endianess, need to check if it is available!
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
set(BUILD_FFUTIL (FFTW_FOUND AND CUDA_FOUND) AND GSL_FOUND AND Boost_PROGRAM_OPTIONS_FOUND)
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
    PRIVATE
    #boost for building internals
    "${Boost_INCLUDE_DIRS}"
    #config header
    "${PROJECT_BINARY_DIR}")

target_compile_options(ovfparser PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors -Wno-strict-aliasing>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

#build recepy for fft util
IF(BUILD_FFUTIL)
    add_executable(ovf-batchfft
        util/fft-util.cpp
        util/cuda-backend.cu
        $<IF:$<BOOL:${WIN32}>,util/win-console.cpp,util/console.cpp>)

    set_property(TARGET ovf-batchfft PROPERTY CUDA_ARCHITECTURES all)

    target_link_libraries(ovf-batchfft PRIVATE ovfparser Threads::Threads Boost::program_options
        $<$<BOOL:${FFTW_FOUND}>:FFTW::FloatThreads>
        "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")

    target_include_directories(ovf-batchfft PRIVATE 
        "${PROJECT_SOURCE_DIR}/util"
        "${PROJECT_BINARY_DIR}"
        $<$<VERSION_LESS:CMAKE_VERSION,3.17>:${CUDA_INCLUDE_DIRS}>)

    set_property(TARGET ovf-batchfft PROPERTY CUDA_STANDARD 11)

    IF(WIN32)
        set_target_properties(ovf-batchfft PROPERTIES LINK_FLAGS "setargv.obj")
    ENDIF()

    if(MSVC)
        #microsoft trolling by misreporting c++ standard
        #https://learn.microsoft.com/en-us/cpp/build/reference/zc-cplusplus?view=msvc-170&viewFallbackFrom=msvc-190
        target_compile_options(ovf-batchfft PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:
                                -Xcompiler "/Zc:__cplusplus" 
                                --std c++17>)
    ENDIF()
ENDIF()

#and install information for library
set(INCLUDE_INSTALL_DIR "${INSTALL_DIR}/include/OVFParser")
install(TARGETS ovfparser DESTINATION "${LIBRARY_INSTALL_DIR}")
install(FILES
    OVFParser/OVFHeader.h
    OVFParser/VField.h
    OVFParser/OVFDictionary.h
    OVFParser/OVFParser.h
    OVFParser/Slice.h
    OVFParser/OVFWriter.h
    "${PROJECT_BINARY_DIR}/OVFParser/ovfparser_export.h" DESTINATION "${INCLUDE_INSTALL_DIR}"
)

#include tests
IF(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
ENDIF()

IF(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
ENDIF()

#interfaces CMakeLists.txt
add_subdirectory(Interfaces)
