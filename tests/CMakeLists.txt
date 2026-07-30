project(test LANGUAGES CXX)
#test targets

#let the tests begin :D
add_executable(header-test header.cpp)
target_link_libraries(header-test PRIVATE ovfparser)
add_test(HeaderTest header-test)

#test for VField container
add_executable(vfield-test field.cpp)
target_link_libraries(vfield-test PRIVATE ovfparser)
add_test(VFieldDataTest vfield-test)

#I/O testing
add_executable(io-test ovf_io.cpp)
target_link_libraries(io-test PRIVATE ovfparser)
file(TO_NATIVE_PATH "${PROJECT_BINARY_DIR}" native_bin_dir)
add_test(IOTests io-test "${native_bin_dir}")

add_executable(frequency-test frequency.cpp)
target_include_directories(frequency-test PRIVATE "${CMAKE_SOURCE_DIR}/util")
add_test(FrequencyArithmeticTest frequency-test)

if(CUDA_FOUND AND FFTW_FLOAT_LIB_FOUND)
    function(add_cuda_fft_test target source test_name)
        add_executable(${target} ${source} "${CMAKE_SOURCE_DIR}/util/cuda-backend.cu")
        target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/util")
        target_link_libraries(${target} PRIVATE FFTW::Float
            "$<IF:$<VERSION_LESS:${CMAKE_VERSION},3.17>,${CUDA_CUFFT_LIBRARIES},CUDA::cufft;CUDA::cudart>")
        set_target_properties(${target} PROPERTIES
            CUDA_ARCHITECTURES all
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED ON)

        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endfunction()

    add_cuda_fft_test(cuda-inplace-test cuda_inplace.cu CUDAInPlaceFFT)
    add_cuda_fft_test(cuda-interpolation-test cuda_interpolation.cu CUDAInterpolationFFT)
endif()
