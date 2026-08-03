# Integrations with external languages and runtimes.
if(OVFTOOLKIT_BUILD_WOLFRAM_BINDING)
    add_subdirectory(wolfram)
endif()
add_subdirectory(python)
