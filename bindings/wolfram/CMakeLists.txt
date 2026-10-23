if(OVFTOOLKIT_BUILD_WOLFRAM_BINDING_ENABLED)
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
    target_link_libraries( math-ovftoolkit "${Mathematica_WSTP_LIBRARIES}" ovfparser uuid )
    target_include_directories( math-ovftoolkit PRIVATE "${Mathematica_WSTP_INCLUDE_DIR}" )

    # Keep the conventional prefix installation for command-line use.
    install(TARGETS math-ovftoolkit
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    if(Mathematica_USERBASE_DIR AND
       NOT Mathematica_USERBASE_DIR MATCHES "-NOTFOUND$")
        set(OVFTOOLKIT_WOLFRAM_APPLICATION_DIR
            "${Mathematica_USERBASE_DIR}/Applications"
            CACHE PATH "Per-user Wolfram Applications installation directory")

        # This is an additional copy; it does not alter the normal prefix
        # installation above or ovfparser's root installation rules.
        install(TARGETS math-ovftoolkit
            RUNTIME DESTINATION "${OVFTOOLKIT_WOLFRAM_APPLICATION_DIR}"
            COMPONENT Wolfram
            EXCLUDE_FROM_ALL)

        get_target_property(_ovftoolkit_parser_type ovfparser TYPE)
        if(_ovftoolkit_parser_type STREQUAL "SHARED_LIBRARY")
            # A shared parser runtime must travel with the WSTP executable.
            # The static archive/import library is deliberately not copied.
            install(TARGETS ovfparser
                LIBRARY DESTINATION "${OVFTOOLKIT_WOLFRAM_APPLICATION_DIR}"
                RUNTIME DESTINATION "${OVFTOOLKIT_WOLFRAM_APPLICATION_DIR}"
                COMPONENT Wolfram
                EXCLUDE_FROM_ALL)
        endif()

        message(STATUS
            "Wolfram user application install directory: "
            "${OVFTOOLKIT_WOLFRAM_APPLICATION_DIR}")
    else()
        message(WARNING
            "FindMathematica did not export Mathematica_USERBASE_DIR; "
            "the Wolfram user application install rule is unavailable")
    endif()
endif()
