::The OVF interaction library for Wolfram Mathematica

::First miscellaneous error defines
:Evaluate:      OVFToolkit::fsub      = "Could not find the original file name: \"`1`\". Using expansion: \"`2`\" instead!"
:Evaluate:      OVFToolkit::chtype    = "First argument \"`1`\" is not a valid file specification" 
:Evaluate:      OVFToolkit::notperm   = "Was not permitted to `1` \"`2`\""
:Evaluate:      OVFToolkit::prserr    = "Could not parse additional arguments"
:Evaluate:      OVFToolkit::unhpack   = "Unhandled packets were found on the link, skipping all of them!"
:Evaluate:      ImportOVF::argx       = "ImportOVF called with 0 parameters, at least one was expected"
:Evaluate:      ImportOVF::prserr     = "Received following errors while parsing a file:\n `1`"
:Evaluate:      ImportOVF::naddr      = "Data in segment `1` of the file \"`2`\" is not addressible!!"
:Evaluate:      ImportOVF[]           := Message[ImportOVF::argx];
:Evaluate:      ImportOVF[x_,___]     := Message[OVFToolkit::chtype, x];           

::Functions exported
::Importing data
::void import(const char*, int)
:Begin:
:Function:      import
:Pattern:       ImportOVF[fileName_String, options:Rule[_String,_]...]
:Arguments:     {fileName, Length@List@options, options}
:ArgumentTypes: {String, Integer32, Manual}
:ReturnType:    Manual
:End:

:Evaluate:      ImportOVF::usage      = "ImportOVF[source, options...]\nImports vector field data from ovf file pointed to by 'source', returning Wolfram language representation of both its header and data sections. Options are 'GetData' and 'GetHeader', when set to False those will prevent the respective section from being imported."

::void exportOVF(const char*, int)
:Begin:
:Function:      exportOVF
:Pattern:       ExportOVF[fileName_String, data_?(ArrayQ[#,2|4,NumericQ]&), header:{Rule[_String,_]..}, options:Rule[_String,_]...]
:Arguments:     {fileName, Length@List@options, options, data, header }
:ArgumentTypes: {String, Integer32, Manual}
:ReturnType:    Manual
:End:

:Evaluate:      ExportOVF::usage      = "ExportOVF[dest, data, header]\nExports vector field data into an OVF file compliant to version provided in header."

