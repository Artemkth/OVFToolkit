# OVFToolkit

OVFToolkit is a C++23 library and collection of tools for reading, writing,
validating, and processing OOMMF vector-field (OVF) files.

## Python

Binary wheels expose lazy, bounded-memory OVF segment access as writable NumPy
arrays:

```sh
python -m pip install ovftoolkit
```

```python
import ovftoolkit as ovf

with ovf.reader("input.ovf") as source:
    for field in source:
        analyse(field.data)
```

Create files by assigning a rank-4 rectangular or rank-2 irregular NumPy
array. For rectangular data, `dummy_header` fills a valid unit/header template
from the array shape and supplied cell size:

```python
field = ovf.VField()
field.data = array
field.dummy_header((dx, dy, dz))
field.header[ovf.OVFParameter.Title] = "Example"

with ovf.writer("output.ovf") as destination:
    destination.write(field)
```

The included `cibuildwheel` configuration targets CPython 3.10 through 3.14 on
64-bit Linux and Windows. A source build requires CMake 3.30, a C++23
compiler, Boost headers, and either native `std::mdspan` or the Kokkos mdspan
reference headers.

Release artifacts can be built with:

```sh
python -m build
python -m cibuildwheel --output-dir wheelhouse
```

The separately licensed mumax controller is packaged from
`tools/mumax-runner` as `ovftoolkit-mumax-runner`.

The repository is organised into:

- `OVFParser/` — the public C++ library;
- `tools/` — standalone command-line applications;
- `bindings/` — integrations with external languages and runtimes;
- `tests/` — library and numerical regression tests;
- `docs/` — documentation build configuration.

## Documentation

When Doxygen is installed, configure normally and build the documentation with:

```sh
cmake --build build --target ovftoolkit-docs
```

## License

OVFToolkit is distributed under the MIT License. See `LICENSE`.
