# ovf-convert

`ovf-convert` converts an OOMMF vector-field file into VTK XML or HDF5:

```text
ovf-convert input.ovf output.vts
ovf-convert input.ovf output.vtu
ovf-convert input.ovf output.h5
```

Rectangular meshes use VTK structured-grid (`.vts`) output. Irregular meshes
use VTK unstructured-grid (`.vtu`) output with one vertex cell per OVF point.
An HDF5 output contains `/segments/NNNNNN/values`; irregular segments also
contain `/segments/NNNNNN/points`. Rectangular value dimensions are ordered
`[z, y, x, component]`, matching `VField::gridView()`.

Every conversion writes a same-named `.json` file containing the OVF header
metadata. For VTK it records the corresponding `.vts` or `.vtu` file for each
segment; for HDF5 it records the segment group. Point and rectangular node
counts are omitted because those sizes are already represented by the output
datasets.

Input data is loaded lazily, one OVF segment at a time. A multi-segment VTK
conversion creates files named `output.segment-NNNNNN.vts` or `.vtu`; HDF5
stores every segment in one file. Existing files are preserved unless
`--force` is passed.
