if(OVFTOOLKIT_BUILD_MUMAX_RUNNER STREQUAL "OFF")
    return()
endif()

find_program(OVFTOOLKIT_GO_EXECUTABLE NAMES go)
find_package(Git QUIET)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT OVFTOOLKIT_GO_EXECUTABLE OR NOT Git_FOUND OR
   NOT Python3_Interpreter_FOUND OR NOT OVFTOOLKIT_CUFFT_FOUND)
    if(OVFTOOLKIT_BUILD_MUMAX_RUNNER STREQUAL "ON")
        if(NOT OVFTOOLKIT_GO_EXECUTABLE)
            message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER=ON requires Go")
        elseif(NOT Python3_Interpreter_FOUND)
            message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER=ON requires Python 3")
        elseif(NOT Git_FOUND)
            message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER=ON requires Git")
        else()
            message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER=ON requires CUDA/cuFFT")
        endif()
    endif()
    if(NOT OVFTOOLKIT_GO_EXECUTABLE)
        message(STATUS "Mumax runner disabled: Go was not found")
    elseif(NOT Python3_Interpreter_FOUND)
        message(STATUS "Mumax runner disabled: Python 3 was not found")
    elseif(NOT Git_FOUND)
        message(STATUS "Mumax runner disabled: Git was not found")
    elseif(NOT OVFTOOLKIT_CUFFT_FOUND)
        message(STATUS "Mumax runner disabled: CUDA/cuFFT was not found")
    endif()
    return()
endif()

set(MUMAX3_SOURCE_DIR "" CACHE PATH
    "Existing mumax3 checkout, or an empty/missing directory to clone into")
set(_mumax_cuda13_fix e0e52bb03b1a68156add5e76e38d02239751653c)
set(_mumax_repository https://github.com/mumax/3.git)

function(_mumax_is_source dir out_var)
    set(_is_source FALSE)
    if(EXISTS "${dir}/go.mod")
        file(STRINGS "${dir}/go.mod" _module_line
            REGEX "^module[ \t]+github.com/mumax/3$")
        if(_module_line)
            set(_is_source TRUE)
        endif()
    endif()
    set(${out_var} "${_is_source}" PARENT_SCOPE)
endfunction()

function(_mumax_directory_is_empty dir out_var)
    set(_is_empty FALSE)
    if(IS_DIRECTORY "${dir}")
        file(GLOB _entries LIST_DIRECTORIES TRUE "${dir}/*" "${dir}/.*")
        list(FILTER _entries EXCLUDE REGEX "[/\\\\]\\.\\.?$")
        if(NOT _entries)
            set(_is_empty TRUE)
        endif()
    endif()
    set(${out_var} "${_is_empty}" PARENT_SCOPE)
endfunction()

function(_mumax_has_cuda13_fix dir out_var)
    set(_has_fix FALSE)
    if(EXISTS "${dir}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor
                "${_mumax_cuda13_fix}" HEAD
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE _ancestor_result
            ERROR_QUIET)
        if(_ancestor_result EQUAL 0)
            set(_has_fix TRUE)
        endif()
    elseif(EXISTS "${dir}/cuda/cu/context.go")
        file(READ "${dir}/cuda/cu/context.go" _context_source)
        if(_context_source MATCHES "cuCtxCreate_v2")
            set(_has_fix TRUE)
        endif()
    endif()
    set(${out_var} "${_has_fix}" PARENT_SCOPE)
endfunction()

# Normalize external Windows paths before using them in CMake strings or go.work.
# In particular, this prevents C:\Users\... from being interpreted as a \U escape.
set(_mumax_explicit_hint FALSE)
set(_mumax_clone_required FALSE)
if(MUMAX3_SOURCE_DIR)
    set(_mumax_explicit_hint TRUE)
    cmake_path(CONVERT "${MUMAX3_SOURCE_DIR}" TO_CMAKE_PATH_LIST
        MUMAX3_SOURCE_DIR NORMALIZE)
    set(MUMAX3_SOURCE_DIR "${MUMAX3_SOURCE_DIR}" CACHE PATH
        "Existing mumax3 checkout, or an empty/missing directory to clone into"
        FORCE)

    _mumax_is_source("${MUMAX3_SOURCE_DIR}" _mumax_valid_source)
    if(NOT _mumax_valid_source)
        _mumax_directory_is_empty("${MUMAX3_SOURCE_DIR}" _mumax_empty_hint)
        if(NOT EXISTS "${MUMAX3_SOURCE_DIR}" OR _mumax_empty_hint)
            set(_mumax_clone_required TRUE)
        else()
            message(FATAL_ERROR
                "MUMAX3_SOURCE_DIR is nonempty but does not contain mumax3: "
                "${MUMAX3_SOURCE_DIR}. The directory was not modified.")
        endif()
    endif()
endif()

# GOPATH is discovery-only. Never use it as a clone or update destination.
if(NOT _mumax_explicit_hint AND DEFINED ENV{GOPATH} AND NOT "$ENV{GOPATH}" STREQUAL "")
    cmake_path(CONVERT "$ENV{GOPATH}" TO_CMAKE_PATH_LIST
        _mumax_gopath_roots NORMALIZE)
    foreach(_mumax_gopath_root IN LISTS _mumax_gopath_roots)
        cmake_path(APPEND _mumax_gopath_root "src" "github.com" "mumax" "3"
            OUTPUT_VARIABLE _mumax_gopath_source)
        _mumax_is_source("${_mumax_gopath_source}" _mumax_valid_source)
        if(_mumax_valid_source)
            set(MUMAX3_SOURCE_DIR "${_mumax_gopath_source}")
            message(STATUS
                "Mumax runner: using read-only GOPATH checkout at "
                "${MUMAX3_SOURCE_DIR}")
            break()
        endif()
    endforeach()
endif()

if(NOT MUMAX3_SOURCE_DIR)
    cmake_path(SET MUMAX3_SOURCE_DIR NORMALIZE
        "${CMAKE_CURRENT_BINARY_DIR}/mumax3-src")
    _mumax_is_source("${MUMAX3_SOURCE_DIR}" _mumax_valid_source)
    if(NOT _mumax_valid_source)
        _mumax_directory_is_empty("${MUMAX3_SOURCE_DIR}" _mumax_empty_default)
        if(NOT EXISTS "${MUMAX3_SOURCE_DIR}" OR _mumax_empty_default)
            set(_mumax_clone_required TRUE)
        else()
            message(FATAL_ERROR
                "The build-managed mumax3 directory is nonempty but invalid: "
                "${MUMAX3_SOURCE_DIR}. It was not modified; remove it manually "
                "or provide MUMAX3_SOURCE_DIR.")
        endif()
    endif()
endif()

if(_mumax_clone_required)
    message(STATUS "Mumax runner: cloning mumax3 into ${MUMAX3_SOURCE_DIR}")
    cmake_path(GET MUMAX3_SOURCE_DIR PARENT_PATH _mumax_clone_parent)
    file(MAKE_DIRECTORY "${_mumax_clone_parent}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone "${_mumax_repository}"
            "${MUMAX3_SOURCE_DIR}"
        RESULT_VARIABLE _mumax_clone_result
        OUTPUT_VARIABLE _mumax_clone_output
        ERROR_VARIABLE _mumax_clone_error)
    if(NOT _mumax_clone_result EQUAL 0)
        message(FATAL_ERROR
            "Could not clone mumax3 into ${MUMAX3_SOURCE_DIR}:\n"
            "${_mumax_clone_output}${_mumax_clone_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
        RESULT_VARIABLE _mumax_describe_result
        OUTPUT_VARIABLE _mumax_latest_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _mumax_describe_error)
    if(NOT _mumax_describe_result EQUAL 0 OR NOT _mumax_latest_tag)
        message(FATAL_ERROR
            "Could not determine the latest mumax3 tag: ${_mumax_describe_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" checkout "${_mumax_latest_tag}"
        WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
        RESULT_VARIABLE _mumax_checkout_result
        OUTPUT_VARIABLE _mumax_checkout_output
        ERROR_VARIABLE _mumax_checkout_error)
    if(NOT _mumax_checkout_result EQUAL 0)
        message(FATAL_ERROR
            "Could not check out mumax3 tag ${_mumax_latest_tag}:\n"
            "${_mumax_checkout_output}${_mumax_checkout_error}")
    endif()
    message(STATUS "Mumax runner: checked out latest tag ${_mumax_latest_tag}")

    if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
        _mumax_has_cuda13_fix("${MUMAX3_SOURCE_DIR}" _mumax_compatible)
        if(NOT _mumax_compatible)
            message(STATUS
                "Mumax runner: ${_mumax_latest_tag} lacks CUDA 13 support; "
                "using current master")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" checkout master
                WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
                RESULT_VARIABLE _mumax_master_checkout_result
                OUTPUT_VARIABLE _mumax_master_checkout_output
                ERROR_VARIABLE _mumax_master_checkout_error)
            if(NOT _mumax_master_checkout_result EQUAL 0)
                message(FATAL_ERROR
                    "Could not check out mumax3 master:\n"
                    "${_mumax_master_checkout_output}"
                    "${_mumax_master_checkout_error}")
            endif()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" pull --ff-only origin master
                WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
                RESULT_VARIABLE _mumax_master_pull_result
                OUTPUT_VARIABLE _mumax_master_pull_output
                ERROR_VARIABLE _mumax_master_pull_error)
            if(NOT _mumax_master_pull_result EQUAL 0)
                message(FATAL_ERROR
                    "Could not update mumax3 master:\n"
                    "${_mumax_master_pull_output}${_mumax_master_pull_error}")
            endif()
        endif()
    endif()
else()
    message(STATUS "Mumax runner: using existing source at ${MUMAX3_SOURCE_DIR}")
endif()

_mumax_is_source("${MUMAX3_SOURCE_DIR}" _mumax_valid_source)
if(NOT _mumax_valid_source)
    message(FATAL_ERROR
        "MUMAX3_SOURCE_DIR is not the github.com/mumax/3 Go module: "
        "${MUMAX3_SOURCE_DIR}")
endif()

if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    _mumax_has_cuda13_fix("${MUMAX3_SOURCE_DIR}" _mumax_compatible)
    if(NOT _mumax_compatible)
        if(_mumax_clone_required)
            message(FATAL_ERROR
                "Current mumax3 master is not compatible with CUDA "
                "${CMAKE_CUDA_COMPILER_VERSION}; expected commit "
                "${_mumax_cuda13_fix} or a descendant.")
        else()
            message(FATAL_ERROR
                "CUDA ${CMAKE_CUDA_COMPILER_VERSION} requires mumax3 commit "
                "${_mumax_cuda13_fix} or a descendant. The existing source at "
                "${MUMAX3_SOURCE_DIR} was not modified.")
        endif()
    endif()
endif()

set(MUMAX3_VERSION "unknown")
if(Git_FOUND AND EXISTS "${MUMAX3_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
        OUTPUT_VARIABLE MUMAX3_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
message(STATUS "Mumax runner: mumax3 version ${MUMAX3_VERSION}")

set(_mumax_build_dir "${CMAKE_CURRENT_BINARY_DIR}/go-build")
file(MAKE_DIRECTORY "${_mumax_build_dir}")
configure_file(go.work.in "${_mumax_build_dir}/go.work" @ONLY)
set(_mumax_slave "${_mumax_build_dir}/mumax-slave${CMAKE_EXECUTABLE_SUFFIX}")

add_custom_command(
    OUTPUT "${_mumax_slave}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "GOWORK=${_mumax_build_dir}/go.work"
        "GOCACHE=${_mumax_build_dir}/cache"
        "${OVFTOOLKIT_GO_EXECUTABLE}" build -mod=readonly
        "-ldflags=-X=main.mumaxVersion=${MUMAX3_VERSION}"
        -o "${_mumax_slave}" "${CMAKE_CURRENT_SOURCE_DIR}/mumax-slave.go"
    DEPENDS mumax-slave.go go.mod "${_mumax_build_dir}/go.work"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building GPL-licensed mumax3 socket slave"
    VERBATIM)
add_custom_target(mumax-runner ALL DEPENDS "${_mumax_slave}")

set(_mumax_python_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ovftoolkit_mumax/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ovftoolkit_mumax/__main__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ovftoolkit_mumax/runner.py")
set(_mumax_package_build_dir "${CMAKE_CURRENT_BINARY_DIR}/python-package")
set(_mumax_package_source_dir
    "${_mumax_package_build_dir}/src/ovftoolkit_mumax")
set(_mumax_wheel_dir "${CMAKE_CURRENT_BINARY_DIR}/wheel")
set(_mumax_wheel_stamp "${_mumax_wheel_dir}/wheel.stamp")

add_custom_command(
    OUTPUT "${_mumax_wheel_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_mumax_package_source_dir}" "${_mumax_wheel_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml"
        "${CMAKE_CURRENT_SOURCE_DIR}/setup.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
        "${_mumax_package_build_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${_mumax_python_sources} "${_mumax_package_source_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_mumax_slave}" "${_mumax_package_source_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${MUMAX3_SOURCE_DIR}/LICENSE"
        "${_mumax_package_build_dir}/LICENSE.mumax3"
    COMMAND "${Python3_EXECUTABLE}" -m pip wheel
        --no-deps --no-build-isolation
        --wheel-dir "${_mumax_wheel_dir}"
        "${_mumax_package_build_dir}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_mumax_wheel_stamp}"
    DEPENDS
        pyproject.toml setup.py README.md LICENSE NOTICE
        "${MUMAX3_SOURCE_DIR}/LICENSE"
        ${_mumax_python_sources} "${_mumax_slave}"
    COMMENT "Building ovftoolkit-mumax-runner Python wheel"
    VERBATIM)
add_custom_target(mumax-runner-wheel ALL DEPENDS "${_mumax_wheel_stamp}")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_test(NAME MumaxSocketIPC
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/socket-smoke.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ovftoolkit_mumax/runner.py" "${_mumax_slave}")
    set_tests_properties(MumaxSocketIPC PROPERTIES SKIP_RETURN_CODE 77)
endif()

install(DIRECTORY "${_mumax_wheel_dir}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/python"
    FILES_MATCHING PATTERN "*.whl")
install(FILES LICENSE NOTICE README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/mumax-runner")
install(FILES "${MUMAX3_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/mumax-runner"
    RENAME LICENSE.mumax3)
