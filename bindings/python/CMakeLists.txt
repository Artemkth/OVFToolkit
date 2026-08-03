option(OVFTOOLKIT_BUILD_PYTHON_BINDING
    "Build the nanobind Python extension when dependencies are available" ON)
option(OVFTOOLKIT_PYTHON_WHEEL
    "Install the Python extension into an ovftoolkit wheel package" OFF)
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
nanobind_add_module(ovftoolkit_native bindings.cpp)
set_target_properties(ovftoolkit_native PROPERTIES
    OUTPUT_NAME _native
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit")
configure_file(ovftoolkit/__init__.py ovftoolkit/__init__.py COPYONLY)
configure_file(test_binding.py test_binding.py COPYONLY)
target_link_libraries(ovftoolkit_native PRIVATE ovfparser)
target_compile_options(ovftoolkit_native PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-Werror -Wall -Wextra -pedantic-errors>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

nanobind_add_stub(ovftoolkit_stub
    MODULE ovftoolkit._native
    OUTPUT _native.pyi
    PYTHON_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS ovftoolkit_native)
add_custom_target(ovftoolkit_python ALL DEPENDS ovftoolkit_stub)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_test(NAME PythonBindingTest
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}"
            "${Python_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/test_binding.py")
endif()

if(OVFTOOLKIT_PYTHON_WHEEL)
    set(_python_package_dir "ovftoolkit")
else()
    set(_python_package_dir
        "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages/ovftoolkit")
endif()
install(TARGETS ovftoolkit_native
    LIBRARY DESTINATION "${_python_package_dir}" COMPONENT Python
    RUNTIME DESTINATION "${_python_package_dir}" COMPONENT Python)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/_native.pyi"
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/__init__.pyi"
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/py.typed"
    DESTINATION "${_python_package_dir}" COMPONENT Python)
