# Integrations with external languages and runtimes.
if(OVFTOOLKIT_BUILD_WOLFRAM_BINDING_ENABLED)
    add_subdirectory(wolfram)
endif()
add_subdirectory(python)
