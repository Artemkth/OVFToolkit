::The OVF interaction library for Wolfram Mathematica

::First miscellaneous error defines
:Evaluate:      OVFToolkit::fsub      = "Could not find the original file name: \"`1`\". Using expansion: \"`2`\" instead!"
:Evaluate:      OVFToolkit::chtype    = "First argument \"`1`\" is not a valid file specification." 
:Evaluate:      OVFToolkit::notperm   = "Was not permitted to `1` \"`2`\"."
:Evaluate:      OVFToolkit::prserr    = "Could not parse additional arguments!"
:Evaluate:      ImportOVF::argx       = "ImportOVF called with 0 parameters, at least one was expected!"
:Evaluate:      ImportOVF::prserr     = "Received following errors while parsing a file:\n `1`."
:Evaluate:      ImportOVF::naddr      = "Data in segment `1` of the file \"`2`\" is not addressible!!"
:Evaluate:      ImportOVF::oob        = "Index `1` in the range specification is out of bounds!"
:Evaluate:      ImportOVF::bspan      = "Received a bad span specification."
:Evaluate:      ImportOVF[]           := Message[ImportOVF::argx];
:Evaluate:      ImportOVF[x_,___]     := Message[OVFToolkit::chtype, x];           

::Functions exported
::Importing data
::void import(const char*, int)
:Begin:
:Function:      import
:Pattern:       ImportOVF[fileName_String, spans:(_Integer | List[___Integer] | All | Span[(_Integer | All)..])..., options:Rule[_String,_]...]
:Arguments:     {fileName, Length@List@options, Length@List@spans, spans, options}
:ArgumentTypes: {String, Integer32, Integer32, Manual}
:ReturnType:    Manual
:End:

:Evaluate:      ImportOVF::usage      = "ImportOVF[source, options...]\nImports vector field data from ovf file pointed to by 'source', returning Wolfram language representation of both its header and data sections. Options are 'GetData' and 'GetHeader', when set to False those will prevent the respective section from being imported."
:Evaluate:      ExportOVF::badsize    = "Got a bad size `1` for internal data, expected it to be 4 or 8. Proceeding with the defaul value of 4."
:Evaluate:      ExportOVF::ambig      = "The filename for export \"`1`\" is ambiguous, got `2` alternatives!"
:Evaluate:      ExportOVF::badexp     = "Bad header expression at header token \"`1`\": expected `2`!"
:Evaluate:      ExportOVF::unkntok    = "Unknow token \"`1`\" in a header rule!"
:Evaluate:      ExportOVF::redund     = "Header field \"`1`\" is redundant, it is ignored in favor of value deduced from the array's shape!"
:Evaluate:      ExportOVF::noncomp    = "The field received tested to be noncompliant with the standard, validation report:\n `1`"
:Evaluate:      ExportOVF::expfail    = "Errors occured exporting the field! WriteOVF returned: \n `1`"
:Evaluate:      ExportOVF::dedfail    = "Failed to deduce the following parameters: `1`"

::void exportOVF(const char*, int)
:Begin:
:Function:      exportOVF
:Pattern:       ExportOVF[fileName_String, data_?(ArrayQ[#,2|4,NumericQ]&), header:{Rule[_String,_]..}, options:Rule[_String,_]...]
:Arguments:     {fileName, Length@List@options, options, N[data], N[header] }
:ArgumentTypes: {String, Integer32, Manual}
:ReturnType:    Manual
:End:

:Evaluate:      ExportOVF::usage      = "ExportOVF[dest, data, header]\nExports vector field data into an OVF file compliant to version provided in header."

