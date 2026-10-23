# Python API reference

Import the public package as:

```python
import ovftoolkit as ovf
```

## File access

### `reader(path, eager=False) -> Reader`

Open an OVF file. Data loading is lazy by default. `Reader` is a context
manager, sequence, and iterator over the file's segments.

```python
with ovf.reader("field.ovf") as source:
    first = source[0]
    metadata = source.header(0)
    for field in source:
        process(field)
```

`Reader` provides `read(segment=0)`, `header(segment=0)`, `close()`,
`closed`, `len(reader)`, indexed access, and iteration.

### `writer(path, segments=1, deduction_iterations=5) -> Writer`

Create a streaming OVF writer with automatic header deduction.

```python
with ovf.writer("result.ovf", segments=2) as destination:
    destination.write(first_field)
    destination.write(second_field)
```

`Writer` provides `write(field)`, `close()`, `closed`, `segments_written`, and
`deduction_report`.

## `VField`

`VField()` constructs an empty vector field. `VField(version)` selects an
`OVFVersion` explicitly.

| Member | Description |
| --- | --- |
| `header` | Mutable [`SegmentHeader`](#segmentheader) attached to the field. |
| `data` | Writable, zero-copy NumPy view. Assignment replaces the C++-owned storage and invalidates older views. |
| `complex_data` | Writable zero-copy complex view formed from adjacent real/imaginary values; the final dimension must be even. |
| `scalar_count` | Total number of stored scalar values. |
| `point_count` | Number of spatial points. |
| `point_dimension` | Number of values per point. |
| `meshgrid()` | Return `(x, y, z)` cell-centre grids shaped like `data[..., 0]`. |
| `dummy_header(cell_size, origin=None)` | Populate rectangular-grid metadata. `cell_size` may be an isotropic scalar or an `(x, y, z)` sequence. |
| `deduce(max_iterations=5)` | Deduce missing header fields in place and return the report. |
| `validate()` | Validate the header and data, raising `ValueError` on failure. |

## `SegmentHeader`

`SegmentHeader` is a mutable mapping whose keys may be text names or
`OVFParameter` values. It supports indexed access, assignment, deletion,
iteration, `get()`, `keys()`, `values()`, and `items()`.

| Member | Description |
| --- | --- |
| `copy_from(source)` | Replace assignable metadata from another header while preserving protected data-shape fields on headers attached to a `VField`. |
| `validate()` | Validate the metadata or raise `ValueError`. |
| `version` | Parsed `OVFVersion`. |
| `point_count` | Declared point count, or `None`. |
| `point_dimension` | Declared value dimension, or `None`. |

The enums `OVFVersion`, `MeshType`, and `OVFParameter` provide typed values for
file versions, mesh types, and standard header fields.

## NumPy helpers

### `complex_view(data) -> numpy.ndarray`

View adjacent `float32` or `float64` values as complex numbers without copying.
The final axis must be contiguous and have even length.

### `eval_macroparams(file_name, mask=None, frequency_regex=...) -> numpy.recarray`

Reduce a multi-segment complex spectrum to spatially averaged components and
RMS amplitude. An optional non-negative mask supplies spatial weights. Three
components are named `mx`, `my`, and `mz`; other component counts use `m0` ...
`mN`. The returned record array also contains `f` and `rms` fields.
