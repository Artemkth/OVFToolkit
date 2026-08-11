find_package(Doxygen QUIET)
if(NOT Doxygen_FOUND)
    if(OVFTOOLKIT_BUILD_DOCS STREQUAL "ON")
        message(FATAL_ERROR
            "OVFTOOLKIT_BUILD_DOCS=ON requires Doxygen")
    endif()
    message(STATUS
        "Doxygen was not found; the ovftoolkit-docs target is unavailable")
    return()
endif()

set(DOXYGEN_PROJECT_NAME "OVFToolkit")
set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
set(DOXYGEN_PROJECT_BRIEF "C++ interfaces for OOMMF vector-field files")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_AS_ERROR YES)
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")

doxygen_add_docs(ovftoolkit-docs
    "${PROJECT_SOURCE_DIR}/OVFParser/OVFHeader.h"
    "${PROJECT_SOURCE_DIR}/OVFParser/OVFDictionary.h"
    "${PROJECT_SOURCE_DIR}/OVFParser/VField.h"
    "${PROJECT_SOURCE_DIR}/OVFParser/OVFParser.h"
    "${PROJECT_SOURCE_DIR}/OVFParser/OVFWriter.h"
    "${PROJECT_SOURCE_DIR}/README.md"
    COMMENT "Generating OVFToolkit ${PROJECT_VERSION} API documentation")

install(DIRECTORY "${DOXYGEN_OUTPUT_DIRECTORY}/html"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    OPTIONAL)
