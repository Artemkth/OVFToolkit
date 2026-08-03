file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(input "${TEST_DIRECTORY}/irregular.ovf")
file(WRITE "${input}" [=[# OOMMF OVF 2.0
# Segment count: 1
# Begin: Segment
# Begin: Header
# Title: irregular converter smoke test
# meshtype: irregular
# meshunit: m
# pointcount: 2
# valuedim: 3
# valuelabels: mx my mz
# valueunits: A/m A/m A/m
# xmin: 0
# ymin: 0
# zmin: 0
# xmax: 4
# ymax: 5
# zmax: 6
# End: Header
# Begin: Data Text
0.0 0.0 0.0 1.0 2.0 3.0
4.0 5.0 6.0 7.0 8.0 9.0
# End: Data Text
# End: Segment
]=])

foreach(extension IN ITEMS vtu h5)
    execute_process(
        COMMAND "${CONVERTER}" --force "${input}"
            "${TEST_DIRECTORY}/irregular.${extension}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "ovf-convert .${extension} smoke test failed:\n${output}${error}")
    endif()
    if(NOT EXISTS "${TEST_DIRECTORY}/irregular.${extension}")
        message(FATAL_ERROR "ovf-convert did not create .${extension} output")
    endif()
    set(json "${TEST_DIRECTORY}/irregular.json")
    if(NOT EXISTS "${json}")
        message(FATAL_ERROR "ovf-convert did not create companion JSON for .${extension}")
    endif()
    file(READ "${json}" metadata)
    if(NOT metadata MATCHES "\"Title\": \"irregular converter smoke test\"")
        message(FATAL_ERROR "companion JSON did not preserve OVF title")
    endif()
    if(metadata MATCHES "pointcount|xnodes|ynodes|znodes")
        message(FATAL_ERROR "companion JSON contains redundant mesh counts")
    endif()
    if(extension STREQUAL "vtu" AND
       NOT metadata MATCHES "\"vtk_file\": \"irregular.vtu\"")
        message(FATAL_ERROR "VTK companion JSON does not name its data file")
    endif()
endforeach()
