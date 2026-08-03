if(NOT BUILD_FFUTIL)
    return()
endif()

add_executable(ovf-batchfft
    main.cpp
    cuda-backend.cu
    $<IF:$<BOOL:${WIN32}>,win-console.cpp,console.cpp>)

set_property(TARGET ovf-batchfft PROPERTY CUDA_ARCHITECTURES all)
set_property(TARGET ovf-batchfft PROPERTY CUDA_STANDARD 17)

target_link_libraries(ovf-batchfft PRIVATE
    ovfparser
    Threads::Threads
    Boost::program_options
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:tbb>
    "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")

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
