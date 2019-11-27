::void import(const char*, int*, long );

::The OVF importer for Mathematica
:Begin:
:Function:	import
:Pattern:	ImportOVF[x_String, l_List:{}]/;VectorQ[l,IntegerQ]
:Arguments:	{x, l}
:ArgumentTypes:	{String, IntegerList}
:ReturnType:	Manual
:End:

:Evaluate:	ImportOVF::usage = "ImportOVF[FileName] imports vector fields in {{x,y,z},{m_x,m_y,m_z}} format from standard OVF data-files, either specified z-layers or the ones passed through the list l"
:Evaluate:	ImportOVF::notfound = "Could not open file"
:Evaluate:  ImportOVF::badformat = "The file has a bad structure, or is not a valid OVF file"

::void export(const char*, double*, long);
::OVF Vector Field exporter for Mathematica
:Begin:
:Function:  export
:Pattern:   ExportOVF[x_String]
:ArgumentTypes: {String, Manual}
:ReturnType: Manual
:End:
