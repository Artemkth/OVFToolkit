project(test LANGUAGES CXX)
#test targets

#let the tests begin :D
add_executable(header-test header.cpp)
target_link_libraries(header-test PUBLIC ovfparser)
add_test(HeaderTest header-test)

