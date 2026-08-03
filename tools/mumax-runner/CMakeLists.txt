find_program(OVFTOOLKIT_GO_EXECUTABLE NAMES go)
find_package(Python3 COMPONENTS Interpreter QUIET)

set(_mumax_runner_default OFF)
if(OVFTOOLKIT_GO_EXECUTABLE AND Python3_Interpreter_FOUND AND
   OVFTOOLKIT_CUFFT_FOUND)
    set(_mumax_runner_default ON)
endif()
option(OVFTOOLKIT_BUILD_MUMAX_RUNNER
    "Build the GPL-licensed interactive mumax3 runner" ${_mumax_runner_default})

if(NOT OVFTOOLKIT_BUILD_MUMAX_RUNNER)
    if(NOT OVFTOOLKIT_GO_EXECUTABLE)
        message(STATUS "Mumax runner disabled: Go was not found")
    elseif(NOT Python3_Interpreter_FOUND)
        message(STATUS "Mumax runner disabled: Python 3 was not found")
    elseif(NOT OVFTOOLKIT_CUFFT_FOUND)
        message(STATUS "Mumax runner disabled: CUDA/cuFFT was not found")
    endif()
    return()
endif()

if(NOT OVFTOOLKIT_GO_EXECUTABLE)
    message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER requires Go")
endif()
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER requires Python 3")
endif()
if(NOT OVFTOOLKIT_CUFFT_FOUND)
    message(FATAL_ERROR "OVFTOOLKIT_BUILD_MUMAX_RUNNER requires CUDA")
endif()

set(MUMAX3_SOURCE_DIR "" CACHE PATH "Existing mumax3 source checkout")
set(MUMAX3_CUDA13_FIX e0e52bb03b1a68156add5e76e38d02239751653c)
set(MUMAX3_RELEASE_TAG v3.12 CACHE STRING
    "Mumax3 release used with CUDA versions older than 13")
set(MUMAX3_CUDA13_REF bugfix/CUDA13.0 CACHE STRING
    "Mumax3 Git ref used with CUDA 13 or newer")

set(_mumax_gopath_source "")
if(NOT MUMAX3_SOURCE_DIR AND DEFINED ENV{GOPATH})
    set(_mumax_gopath_source "$ENV{GOPATH}/src/github.com/mumax/3")
    if(EXISTS "${_mumax_gopath_source}/go.mod")
        set(MUMAX3_SOURCE_DIR "${_mumax_gopath_source}")
        message(STATUS
            "Mumax runner: using explicit GOPATH checkout at ${MUMAX3_SOURCE_DIR}")
    endif()
endif()

if(NOT MUMAX3_SOURCE_DIR)
    include(FetchContent)
    if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
        set(_mumax_git_ref "${MUMAX3_CUDA13_REF}")
    else()
        set(_mumax_git_ref "${MUMAX3_RELEASE_TAG}")
    endif()
    message(STATUS "Mumax runner: fetching mumax3 ${_mumax_git_ref}")
    set(_mumax_fetch_source_argument)
    if(_mumax_gopath_source)
        list(APPEND _mumax_fetch_source_argument SOURCE_DIR "${_mumax_gopath_source}")
        message(STATUS "Mumax runner: populating explicit GOPATH at ${_mumax_gopath_source}")
    endif()
    FetchContent_Declare(mumax3
        GIT_REPOSITORY https://github.com/mumax/3.git
        GIT_TAG "${_mumax_git_ref}"
        GIT_SHALLOW FALSE
        ${_mumax_fetch_source_argument}
        SOURCE_SUBDIR _ovftoolkit_no_cmake_subdirectory)
    FetchContent_MakeAvailable(mumax3)
    set(MUMAX3_SOURCE_DIR "${mumax3_SOURCE_DIR}")
else()
    message(STATUS "Mumax runner: using mumax3 source at ${MUMAX3_SOURCE_DIR}")
endif()

if(NOT EXISTS "${MUMAX3_SOURCE_DIR}/go.mod")
    message(FATAL_ERROR "MUMAX3_SOURCE_DIR does not contain a go.mod")
endif()

file(STRINGS "${MUMAX3_SOURCE_DIR}/go.mod" _mumax_module_line
    REGEX "^module[ \t]+github.com/mumax/3$")
if(NOT _mumax_module_line)
    message(FATAL_ERROR
        "MUMAX3_SOURCE_DIR is not the current github.com/mumax/3 module")
endif()

find_package(Git QUIET)
if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13 AND Git_FOUND AND
   EXISTS "${MUMAX3_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor
            "${MUMAX3_CUDA13_FIX}" HEAD
        WORKING_DIRECTORY "${MUMAX3_SOURCE_DIR}"
        RESULT_VARIABLE _mumax_has_cuda13_fix
        ERROR_QUIET)
    if(NOT _mumax_has_cuda13_fix EQUAL 0)
        message(FATAL_ERROR
            "CUDA ${CMAKE_CUDA_COMPILER_VERSION} requires mumax3 commit "
            "${MUMAX3_CUDA13_FIX} or a descendant")
    endif()
elseif(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    file(READ "${MUMAX3_SOURCE_DIR}/cuda/cu/context.go" _mumax_context_source)
    if(NOT _mumax_context_source MATCHES "cuCtxCreate_v2")
        message(FATAL_ERROR
            "The supplied non-Git mumax3 source lacks the CUDA 13 context fix")
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

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_test(NAME MumaxSocketIPC
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/socket-smoke.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ovftoolkit_mumax/runner.py" "${_mumax_slave}")
    set_tests_properties(MumaxSocketIPC PROPERTIES SKIP_RETURN_CODE 77)
endif()

install(PROGRAMS "${_mumax_slave}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS src/ovftoolkit_mumax/runner.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME mumax-runner)
install(FILES LICENSE README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/mumax-runner")
install(FILES "${MUMAX3_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/mumax-runner"
    RENAME LICENSE.mumax3)
