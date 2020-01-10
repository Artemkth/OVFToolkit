cmake_minimum_required(VERSION 3.10)

#versioning
project(OVFToolkit VERSION 0.1)

#TODO: add some more checks
# C++ standard
set(CMAKE_CXX_STANDARD 17)              #a lot of c++17 features used in core
set(CMAKE_CXX_STANDARD_REQUIRED True)

#TODO: look into moving it further
configure_file(OVFToolkitConfig.h.in OVFToolkitConfig.h)
target_include_directories(OVFToolkit PUBLIC
                           "${PROJECT_BINARY_DIR}"
                          )

# main parser library include
add_subdirectory(OVFParser)
