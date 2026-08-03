option(OVFTOOLKIT_BUILD_PYTHON_BINDING
    "Build the nanobind Python extension when dependencies are available" ON)
if(NOT OVFTOOLKIT_BUILD_PYTHON_BINDING)
    return()
endif()

find_package(Python 3.10 COMPONENTS Interpreter Development.Module QUIET)
find_package(nanobind CONFIG QUIET)
if(NOT Python_FOUND OR NOT nanobind_FOUND)
    message(STATUS "Python binding disabled: Python development files or nanobind not found")
    return()
endif()

message(STATUS
    "Building Python binding with Python ${Python_VERSION} and nanobind ${nanobind_VERSION}")
nanobind_add_module(ovftoolkit bindings.cpp)
target_link_libraries(ovftoolkit PRIVATE ovfparser)
target_compile_options(ovftoolkit PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

nanobind_add_stub(ovftoolkit_stub
    MODULE ovftoolkit
    OUTPUT ovftoolkit.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:ovftoolkit>
    DEPENDS ovftoolkit)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_test(NAME PythonBindingTest
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=$<TARGET_FILE_DIR:ovftoolkit>"
            "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_binding.py")
endif()

set(_python_site_packages
    "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages")
install(TARGETS ovftoolkit LIBRARY DESTINATION "${_python_site_packages}"
    COMPONENT Python)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit.pyi"
    DESTINATION "${_python_site_packages}" COMPONENT Python)
