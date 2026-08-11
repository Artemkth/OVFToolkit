ovftoolkit_tristate_option(OVFTOOLKIT_BUILD_PYTHON_BINDING
    "Build the nanobind Python extension")
option(OVFTOOLKIT_PYTHON_WHEEL
    "Install the Python extension into an ovftoolkit wheel package" OFF)
option(OVFTOOLKIT_PYTHON_STABLE_ABI
    "Build the Python extension for the CPython 3.12+ stable ABI" OFF)

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

set(_ovftoolkit_python_minimum_version 3.10)
set(_ovftoolkit_python_components Interpreter Development.Module)
if(OVFTOOLKIT_PYTHON_STABLE_ABI)
    set(_ovftoolkit_python_minimum_version 3.12)
    list(APPEND _ovftoolkit_python_components Development.SABIModule)
endif()
find_package(Python ${_ovftoolkit_python_minimum_version}
    COMPONENTS ${_ovftoolkit_python_components} QUIET)
if(NOT Python_FOUND OR NOT TARGET Python::Module)
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but Python 3.10 or newer development "
            "files were not found")
    endif()
    message(STATUS "Python binding disabled: Python development files not found")
    return()
endif()
if(OVFTOOLKIT_PYTHON_STABLE_ABI AND NOT TARGET Python::SABIModule)
    message(FATAL_ERROR
        "OVFTOOLKIT_PYTHON_STABLE_ABI=ON requires Python 3.12 or newer "
        "with Development.SABIModule support")
endif()

get_target_property(_ovftoolkit_python_module_type Python::Module TYPE)
if(_ovftoolkit_python_module_type STREQUAL "STATIC_LIBRARY")
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but Python::Module is a static library. "
            "The nanobind extension requires a shared Python runtime; use a "
            "dynamic Python installation or disable the Python binding.")
    endif()
    message(STATUS
        "Python binding disabled: Python::Module is a static library")
    return()
endif()

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    RESULT_VARIABLE _ovftoolkit_nanobind_result
    OUTPUT_VARIABLE _ovftoolkit_nanobind_cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(_ovftoolkit_nanobind_result EQUAL 0 AND
   IS_DIRECTORY "${_ovftoolkit_nanobind_cmake_dir}")
    # Use the nanobind package installed for the selected interpreter.  In
    # particular, do not accidentally pick up a stale copy from vcpkg or a
    # different Python installation.
    set(nanobind_DIR "${_ovftoolkit_nanobind_cmake_dir}")
    find_package(nanobind CONFIG QUIET NO_DEFAULT_PATH)
endif()
if(NOT nanobind_FOUND)
    if(_ovftoolkit_python_binding_required)
        message(FATAL_ERROR
            "Python binding requested, but nanobind is not installed for "
            "${Python_EXECUTABLE}. Install it with: "
            "\"${Python_EXECUTABLE}\" -m pip install nanobind")
    endif()
    message(STATUS
        "Python binding disabled: nanobind is not installed for "
        "${Python_EXECUTABLE}")
    return()
endif()

# Some packaged Python installations expose separate Debug and Release import
# libraries without supplying a matching debug interpreter.  Map Debug builds
# to Release only when the imported target actually has a Release-specific
# library.  Standard Windows Python uses a configuration-neutral pythonXY.lib,
# which already works for every configuration and must not be remapped.
if(WIN32 AND TARGET Python::Module AND NOT Python_EXECUTABLE_DEBUG)
    get_target_property(_ovftoolkit_python_release_implib
        Python::Module IMPORTED_IMPLIB_RELEASE)
    if(_ovftoolkit_python_release_implib)
        set_property(TARGET Python::Module
            PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
    endif()
endif()

message(STATUS
    "Building Python binding with Python ${Python_VERSION} and nanobind ${nanobind_VERSION}")
set(_ovftoolkit_nanobind_options)
if(OVFTOOLKIT_PYTHON_STABLE_ABI)
    list(APPEND _ovftoolkit_nanobind_options STABLE_ABI)
    message(STATUS "Python binding targets the CPython 3.12+ stable ABI")
endif()
nanobind_add_module(ovftoolkit_native
    ${_ovftoolkit_nanobind_options} bindings.cpp)
set(_python_package_build_dir "${CMAKE_CURRENT_BINARY_DIR}/ovftoolkit")
set_target_properties(ovftoolkit_native PROPERTIES
    OUTPUT_NAME _native
    # The empty generator expression prevents multi-config generators from
    # appending Debug/ or Release/ below the Python package directory.
    LIBRARY_OUTPUT_DIRECTORY "${_python_package_build_dir}/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${_python_package_build_dir}/$<0:>")
file(MAKE_DIRECTORY "${_python_package_build_dir}")
configure_file(ovftoolkit/__init__.py ovftoolkit/__init__.py COPYONLY)
configure_file(ovftoolkit/arrays.py ovftoolkit/arrays.py COPYONLY)
configure_file(ovftoolkit/spectrum.py ovftoolkit/spectrum.py COPYONLY)
configure_file(test_binding.py test_binding.py COPYONLY)
configure_file(test_arrays.py test_arrays.py COPYONLY)
configure_file(test_spectrum.py test_spectrum.py COPYONLY)
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
    add_test(NAME PythonSpectrumTest
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}"
            "${Python_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/test_spectrum.py")
    add_test(NAME PythonArrayViewTest
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}"
            "${Python_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/test_arrays.py")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/arrays.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/spectrum.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/ovftoolkit/py.typed"
    DESTINATION "${_python_package_dir}" COMPONENT Python)
