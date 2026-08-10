if(NOT BUILD_FFUTIL)
    return()
endif()

set(OVF_BATCHFFT_SOURCES
    main.cpp
    $<IF:$<BOOL:${WIN32}>,win-console.cpp,console.cpp>)

if(OVFTOOLKIT_CUFFT_FOUND)
    list(APPEND OVF_BATCHFFT_SOURCES cuda-backend.cu)
endif()
if(FFTW_FLOAT_LIB_FOUND)
    list(APPEND OVF_BATCHFFT_SOURCES fftw-backend.cpp)
endif()
add_executable(ovf-batchfft ${OVF_BATCHFFT_SOURCES})

if(OVFTOOLKIT_CUFFT_FOUND AND FFTW_FLOAT_LIB_FOUND)
    target_compile_definitions(ovf-batchfft PRIVATE OVFTOOLKIT_HAS_MULTIPLE_FFT_ENGINES=1)
endif()

if(OVFTOOLKIT_CUFFT_FOUND)
    set_property(TARGET ovf-batchfft PROPERTY CUDA_ARCHITECTURES all)
    set_property(TARGET ovf-batchfft PROPERTY CUDA_STANDARD 17)
endif()

target_link_libraries(ovf-batchfft PRIVATE
    ovfparser
    Threads::Threads
    Boost::program_options
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:tbb>)

if(OVFTOOLKIT_CUFFT_FOUND)
    target_link_libraries(ovf-batchfft PRIVATE
        "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")
    target_compile_definitions(ovf-batchfft PRIVATE OVFTOOLKIT_HAS_CUFFT=1)
endif()
if(FFTW_FLOAT_LIB_FOUND)
    target_link_libraries(ovf-batchfft PRIVATE FFTW::Float)
    target_compile_definitions(ovf-batchfft PRIVATE OVFTOOLKIT_HAS_FFTW=1)

    if(WIN32)
        set(_ovf_batchfft_fftw_type_target FFTW::Float)
        find_package(FFTW3f CONFIG QUIET)
        if(TARGET FFTW3::fftw3f)
            set(_ovf_batchfft_fftw_type_target FFTW3::fftw3f)
        endif()
        get_target_property(_ovf_batchfft_fftw_type
            ${_ovf_batchfft_fftw_type_target} TYPE)
        if(_ovf_batchfft_fftw_type STREQUAL "SHARED_LIBRARY")
            message(FATAL_ERROR
                "Dynamic FFTW3 builds on Windows are unsupported because "
                "FFTW3 does not export fftwf_version from its DLL. Use a "
                "static FFTW3 build, such as the x64-windows-static vcpkg "
                "triplet. See https://github.com/FFTW/fftw3/issues/242")
        endif()
    endif()

    if(FFTW_FLOAT_THREADS_LIB_FOUND)
        target_link_libraries(ovf-batchfft PRIVATE FFTW::FloatThreads)
        target_compile_definitions(ovf-batchfft PRIVATE OVFTOOLKIT_HAS_FFTW_THREADS=1)
    endif()
endif()

target_include_directories(ovf-batchfft PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
    $<$<VERSION_LESS:CMAKE_VERSION,3.17>:${CUDA_INCLUDE_DIRS}>)

if(WIN32)
    # Wide argv preserves Unicode paths; wsetargv also expands wildcards.
    set_target_properties(ovf-batchfft PROPERTIES LINK_FLAGS "wsetargv.obj")
endif()

if(MSVC)
    # nvcc otherwise receives an incorrect value for __cplusplus from MSVC.
    target_compile_options(ovf-batchfft PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:
        -Xcompiler "/Zc:__cplusplus"
        --std c++17>)
endif()

install(TARGETS ovf-batchfft RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
