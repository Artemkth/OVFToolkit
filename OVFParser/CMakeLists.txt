#library build file
include(GenerateExportHeader)
#like in windows make only certain function exported
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

#ovfparser library
add_library(ovfparser 
        OVFHeader.cpp
        VField.cpp
        OVFGrammar.cpp
        VField.cpp
        OVFReader.cpp
        OVFWriter.cpp
    )

generate_export_header(ovfparser)

#and install information for library
set(INCLUDE_INSTALL_DIR "${INSTALL_DIR}/include")
install(TARGETS ovfparser DESTINATION ${LIBRARY_INSTALL_DIR})
install(FILES
    OVFHeader.h
    VField.h
    OVFDictionary.h
    OVFParser.h
    OVFWriter.h
    ${PROJECT_BINARY_DIR}/ovfparser_export.h DESTINATION ${INCLUDE_INSTALL_DIR})

