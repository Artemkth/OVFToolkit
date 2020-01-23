::The OVF interaction library for Wolfram Mathematica

::First miscellaneous error defines
:Evaluate:      OVFToolkit::badformat = "File has a bad structure, or it is not an OVF file"
:Evaluate:      OVFToolkit::badheader = "Header parameters were malformed"
:Evaluate:      OVFToolkit::baddata   = "Data was formatted badly"

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
