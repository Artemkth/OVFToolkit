project(test LANGUAGES CXX)
#test targets

#let the tests begin :D
add_executable(header-test header.cpp)
target_link_libraries(header-test PRIVATE ovfparser)
add_test(HeaderTest header-test)

add_executable(header-typed-test header_typed.cpp)
target_link_libraries(header-typed-test PRIVATE ovfparser)
add_test(HeaderTypedTest header-typed-test)

#test for VField container
add_executable(vfield-test field.cpp)
target_link_libraries(vfield-test PRIVATE ovfparser)
add_test(VFieldDataTest vfield-test)

#I/O testing
add_executable(io-test ovf_io.cpp)
target_link_libraries(io-test PRIVATE ovfparser)
add_test(IOTests io-test "${PROJECT_BINARY_DIR}")

add_executable(frequency-test frequency.cpp)
target_include_directories(frequency-test PRIVATE "${CMAKE_SOURCE_DIR}/tools/batchfft")
add_test(FrequencyArithmeticTest frequency-test)

if(TARGET ovf-batchfft)
    add_test(NAME NoInputShutdown COMMAND ovf-batchfft)
    set_tests_properties(NoInputShutdown PROPERTIES
        WILL_FAIL TRUE
        FAIL_REGULAR_EXPRESSION "CUFFT_INVALID_PLAN;destroying the plan got unexpected result")
endif()

if(FFTW_FLOAT_LIB_FOUND)
    add_executable(fftw-backend-test
        fftw_backend.cpp
        "${CMAKE_SOURCE_DIR}/tools/batchfft/fftw-backend.cpp")
    target_include_directories(fftw-backend-test PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/batchfft")
    target_link_libraries(fftw-backend-test PRIVATE ${OVFTOOLKIT_FFTW_FLOAT_TARGET}
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:tbb>)
    if(FFTW_FLOAT_THREADS_LIB_FOUND)
        target_link_libraries(fftw-backend-test PRIVATE FFTW::FloatThreads)
        target_compile_definitions(fftw-backend-test PRIVATE OVFTOOLKIT_HAS_FFTW_THREADS=1)
    endif()
    add_test(FFTWBackendTest fftw-backend-test)
endif()

if(OVFTOOLKIT_CUFFT_FOUND AND FFTW_FLOAT_LIB_FOUND)
    function(add_cuda_fft_test target source test_name)
        add_executable(${target} ${source} "${CMAKE_SOURCE_DIR}/tools/batchfft/cuda-backend.cu")
        target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tools/batchfft")
        target_link_libraries(${target} PRIVATE ${OVFTOOLKIT_FFTW_FLOAT_TARGET}
            "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")
        set_target_properties(${target} PROPERTIES
            CUDA_ARCHITECTURES all
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED ON)

        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES
            LABELS gpu
            SKIP_RETURN_CODE 77)
    endfunction()

    add_cuda_fft_test(cuda-inplace-test cuda_inplace.cu CUDAInPlaceFFT)
    add_cuda_fft_test(cuda-interpolation-test cuda_interpolation.cu CUDAInterpolationFFT)

    add_executable(cross-engine-fft-test
        cross_engine_fft.cu
        "${CMAKE_SOURCE_DIR}/tools/batchfft/cuda-backend.cu"
        "${CMAKE_SOURCE_DIR}/tools/batchfft/fftw-backend.cpp")
    target_include_directories(cross-engine-fft-test PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/batchfft")
    target_link_libraries(cross-engine-fft-test PRIVATE ${OVFTOOLKIT_FFTW_FLOAT_TARGET}
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:tbb>
        "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")
    if(FFTW_FLOAT_THREADS_LIB_FOUND)
        target_link_libraries(cross-engine-fft-test PRIVATE FFTW::FloatThreads)
        target_compile_definitions(cross-engine-fft-test PRIVATE OVFTOOLKIT_HAS_FFTW_THREADS=1)
    endif()
    set_target_properties(cross-engine-fft-test PROPERTIES
        CUDA_ARCHITECTURES all
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON)
    add_test(NAME CrossEngineFFT COMMAND cross-engine-fft-test)
    set_tests_properties(CrossEngineFFT PROPERTIES
        LABELS gpu
        SKIP_RETURN_CODE 77)
endif()
