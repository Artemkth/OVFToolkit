::The OVF interaction library for Wolfram Mathematica

::First miscellaneous error defines
:Evaluate:      OVFToolkit::fsub      = "Could not find the original file name: \"`1`\". Using expansion: \"`2`\" instead!"
:Evaluate:      OVFToolkit::chtype    = "First argument \"`1`\" is not a valid file specification" 
:Evaluate:      OVFToolkit::notperm   = "Was not permitted to `1` \"`2`\""
:Evaluate:      OVFToolkit::bfmt      = "File has a bad structure, or it is not an OVF file"
:Evaluate:      ImportOVF::argx       = "ImportOVF called with 0 parameters, at least one was expected"
:Evaluate:      ImportOVF::prserr     = "Received following errors while parsing a file:\n `1`"
:Evaluate:      ImportOVF::naddr      = "Data in segment `1` of the file \"`2`\" is not addressible!!"
:Evaluate:      ImportOVF[]           := Message[ImportOVF::argx];
:Evaluate:      ImportOVF[x_,___]     := Message[OVFToolkit::chtype, x];           

::Functions exported
::void importWhole(const char*)
:Begin:
:Function:      importWhole
:Pattern:       ImportOVF[x_String]
:Arguments:     {x}
:ArgumentTypes: {String}
:ReturnType:    Manual
:End:
::void importPart(const char*)
::Begin:
::Function:      importPart
::Pattern:       ImportOVF[x_String, sequence_]
::Arguments:     {x}
::ArgumentTypes: {String}
::ReturnType:    Manual
::End: 
