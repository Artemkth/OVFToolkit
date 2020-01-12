cmake_minimum_required(VERSION 3.10)

#versioning
project(OVFToolkit VERSION 0.1)

#TODO: add some more checks
# C++ standard
set(CMAKE_CXX_STANDARD 17)              #a lot of c++17 features used in core
set(CMAKE_CXX_STANDARD_REQUIRED True)
# using boost for endianess, need to check if it is available!
find_package(Boost 1.58.0 REQUIRED)


#library build file
include(GenerateExportHeader)
#like in windows make only certain function exported
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

#ovfparser library
add_library(ovfparser
    OVFParser/OVFHeader.cpp
    OVFParser/VField.cpp
    OVFParser/OVFGrammar.cpp
    OVFParser/VField.cpp
    OVFParser/OVFReader.cpp
    #OVFParser/OVFWriter.cpp
)
generate_export_header(ovfparser EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/OVFParser/ovfparser_export.h")

#TODO: look into moving it further
configure_file(OVFToolkitConfig.h.in OVFToolkitConfig.h)
target_include_directories(ovfparser 
                           INTERFACE
                           $<INSTALL_INTERFACE:include/OVFParser>
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/OVFparser>
                           $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/OVFParser>
                           PRIVATE
                           #boost for building internals
                           "${BOOST_INCLUDE_DIRS}"
                           #config header
                           "${PROJECT_BINARY_DIR}"
                          )
# add the multithread support libs later if needed, otherwise no extra links needed

#and install information for library
set(INCLUDE_INSTALL_DIR "${INSTALL_DIR}/include/OVFParser")
install(TARGETS ovfparser DESTINATION "${LIBRARY_INSTALL_DIR}")
install(FILES
    OVFParser/OVFHeader.h
    OVFParser/VField.h
    OVFParser/OVFDictionary.h
    OVFParser/OVFParser.h
    OVFParser/OVFWriter.h
    "${PROJECT_BINARY_DIR}/OVFParser/ovfparser_export.h" DESTINATION "${INCLUDE_INSTALL_DIR}"
)

