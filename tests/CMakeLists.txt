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

