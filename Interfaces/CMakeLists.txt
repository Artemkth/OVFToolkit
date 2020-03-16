OPTION(NO_MATHEMATICA_INTERFACE "Skip building interface library for Wolram Mathematica to access OVFToolkit functionality" OFF)
IF(Mathematica_WSTP_FOUND)
    CHECK_INCLUDE_FILE("wordexp.h" WORDEXP_FOUND)
    IF(WORDEXP_FOUND)
        add_definitions(-DEXPANDPATH)
    ENDIF()
    MESSAGE(STATUS "Building Wolfram Mathematica integration libraries.")

    #first add generator for glue code
    Mathematica_WSTP_WSPREP_TARGET(
        OVFToolkit.tm
        OUTPUT OVFToolkit.tm.c)
    #and an executable
    add_executable( math-ovftoolkit
        OVFToolkit.tm.c
        math-interface.cpp )

    IF(WIN32)
        set_target_properties( math-ovftoolkit PROPERTIES WIN32_EXECUTABLE TRUE)
    ENDIF()
    target_link_libraries( math-ovftoolkit "${Mathematica_WSTP_LIBRARIES}" ovfparser )
    target_include_directories( math-ovftoolkit PRIVATE "${Mathematica_WSTP_INCLUDE_DIR}" )
ENDIF()
