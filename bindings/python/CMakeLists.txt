ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_PYTHON_BINDING
    "Build the nanobind Python extension")
option(OVFTOOLKIT_PYTHON_WHEEL
    "Install the Python extension into an ovftoolkit wheel package" OFF)

if(OVFTOOLKIT_BUILD_PYTHON_BINDING STREQUAL "OFF")
    if(OVFTOOLKIT_PYTHON_WHEEL)
        message(FATAL_ERROR
            "OVFTOOLKIT_PYTHON_WHEEL=ON requires "
            "OVFTOOLKIT_BUILD_PYTHON_BINDING to be AUTO or ON")
    endif()
    return()
endif()

set(_ovftoolkit_python_binding_required OFF)
if(OVFTOOLKIT_BUILD_PYTHON_BINDING STREQUAL "ON" OR
   OVFTOOLKIT_PYTHON_WHEEL)
    set(_ovftoolkit_python_binding_required ON)
endif()

find_package(Python 3.10 COMPONENTS Interpreter Development.Module QUIET)
if(NOT Python_FOUND OR NOT TARGET Python::Module)
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but Python 3.10 or newer development "
            "files were not found")
    endif()
    message(STATUS "Python binding disabled: Python development files not found")
    return()
endif()

get_target_property(_ovftoolkit_python_module_type Python::Module TYPE)
if(_ovftoolkit_python_module_type STREQUAL "STATIC_LIBRARY")
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but Python::Module is a static library. "
            "The nanobind extension requires a shared Python runtime; use a "
            "dynamic Python/vcpkg triplet or disable the Python binding.")
    endif()
    message(STATUS
        "Python binding disabled: Python::Module is a static library")
    return()
endif()

find_package(nanobind CONFIG QUIET)
if(NOT nanobind_FOUND)
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but nanobind was not found")
    endif()
    message(STATUS "Python binding disabled: nanobind not found")
    return()
endif()

# vcpkg supplies the debug Python library on Windows, but not a matching debug
# interpreter.  Stub generation therefore has to load Debug extension builds
# into the release interpreter.  Link those builds to the release Python DLL
# so that the process does not contain two incompatible Python runtimes.
if(WIN32 AND TARGET Python::Module AND NOT Python_EXECUTABLE_DEBUG)
    set_property(TARGET Python::Module PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

message(STATUS
    "Building Python binding with Python ${Python_VERSION} and nanobind ${nanobind_VERSION}")
nanobind_add_module(ovftoolkit_native bindings.cpp)
set(_python_package_build_dir "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit")
set_target_properties(ovftoolkit_native PROPERTIES
    OUTPUT_NAME _native
    # The empty generator expression prevents multi-config generators from
    # appending Debug/ or Release/ below the Python package directory.
    LIBRARY_OUTPUT_DIRECTORY "${_python_package_build_dir}/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${_python_package_build_dir}/$<0:>")
file(MAKE_DIRECTORY "${_python_package_build_dir}")
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
