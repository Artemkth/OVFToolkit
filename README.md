# OVFToolkit

OVFToolkit reads, writes, validates, converts, and processes OOMMF vector-field
(OVF) files. Most users will want one of two things:

1. the `ovftoolkit` Python package for NumPy-based analysis; or
2. the `ovf-convert` and `ovf-batchfft` command-line tools.

The C++23 parser underneath them reads OVF 1.0 and 2.0 files, rectangular and
irregular meshes, text and binary data, and multi-segment files. Segments are
loaded lazily, so memory use follows the data currently being processed rather
than the total file size.

## Python package

### Install

[OVFToolkit is available on PyPI](https://pypi.org/project/ovftoolkit/):

```sh
python -m pip install ovftoolkit
```

To install the latest source checkout instead:

```sh
git clone --recurse-submodules https://github.com/Artemkth/OVFToolkit.git
cd OVFToolkit
python -m pip install .
```

Published ABI3 wheels require only Python 3.12 or newer and NumPy. Python 3.10
and 3.11 remain supported through source installation, which additionally needs
a C++23 compiler, Boost headers, and Python development headers. Install those
prerequisites with one of:

```sh
# Debian, Ubuntu, and derivatives
sudo apt update
sudo apt install build-essential libboost-dev python3-dev python3-venv

# Arch Linux and derivatives
sudo pacman -S --needed base-devel boost python

# Fedora
sudo dnf install gcc-c++ boost-devel python3-devel
```

The Python build frontend supplies nanobind and a sufficiently recent CMake in
an isolated build environment.

### Use

Fields expose writable NumPy arrays. Files and their segments are lazy and can
be iterated without loading the complete dataset:

```python
import ovftoolkit as ovf

with ovf.reader("input.ovf") as source:
    for field in source:
        analyse(field.data)
```

Create a file by assigning a rectangular rank-4 or irregular rank-2 array.
`dummy_header` fills the required rectangular-grid metadata from the array
shape and cell size:

```python
field = ovf.VField()
field.data = array
field.dummy_header((dx, dy, dz))
field.header[ovf.OVFParameter.Title] = "Example"

with ovf.writer("output.ovf") as destination:
    destination.write(field)
```

For rectangular fields with coordinate metadata, `meshgrid()` returns
cell-center coordinate arrays aligned with the field data:

```python
x, y, z = field.meshgrid()
assert x.shape == y.shape == z.shape == field.data.shape[:-1]
```

Copy metadata between fields without overwriting the destination data shape:

```python
destination.header.copy_from(source.header)
```

Working programs live in [`examples/`](examples/). In particular,
[`create_ovf.py`](examples/create_ovf.py) creates a normalized in-plane vortex
and [`plot_ovf.py`](examples/plot_ovf.py) renders an OVF layer with NumPy and
Matplotlib. More examples will be linked here as they are added.

![OVF layer rendered with NumPy and Matplotlib](docs/images/ovftoolkit-python-example.png)

For local notebook work:

```sh
python -m pip install jupyterlab matplotlib
jupyter lab
```

## Command-line tools

| Tool | Purpose | Additional dependencies |
| --- | --- | --- |
| `ovf-convert` | Convert OVF to VTK XML or HDF5 | VTK and HDF5 |
| `ovf-batchfft` | Batch spectral analysis | FFTW and/or NVIDIA CUDA with cuFFT |

Detailed usage is documented in the
[`ovf-convert`](tools/ovf-convert/README.md) and
[`ovf-batchfft`](tools/batchfft/README.md) manuals.

When packaged builds are available, they are attached to the
[GitHub releases](https://github.com/Artemkth/OVFToolkit/releases). Unpack the
Windows archive and run the executables from its `bin` directory; the portable
archive carries the runtime DLLs it needs. CUDA archives additionally require a
compatible NVIDIA GPU and driver.

### Linux dependencies

For a complete CPU build containing both tools:

```sh
# Debian, Ubuntu, and derivatives
sudo apt update
sudo apt install build-essential cmake ninja-build \
  libboost-program-options-dev libfftw3-dev libhdf5-dev libtbb-dev libvtk9-dev

# Arch Linux and derivatives
sudo pacman -S --needed base-devel cmake ninja boost boost-libs fftw hdf5 \
  onetbb vtk netcdf

# Fedora
sudo dnf install gcc-c++ cmake ninja-build boost-devel fftw-devel hdf5-devel \
  tbb-devel vtk-devel netcdf-devel
```

OVFToolkit requires CMake 3.30 or newer. Some long-term-support distributions
ship an older CMake; in that case use a newer CMake package or a temporary
Python environment:

```sh
python3 -m venv .venv-build
. .venv-build/bin/activate
python -m pip install "cmake>=3.30"
```

### Build and install

With the dependencies installed, the complete build is deliberately short:

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOVFTOOLKIT_BUILD_TOOLS=ON -DOVFTOOLKIT_USE_CUFFT=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

The tools are installed under `$HOME/.local/bin`. Add that directory to
`PATH` if it is not already present. To run the test suite before installing:

```sh
ctest --test-dir build --output-on-failure
```

### Windows with vcpkg

Install Visual Studio 2022 with the C++ workload, CMake 3.30 or newer, Git, and
vcpkg. Set `VCPKG_ROOT` to your vcpkg directory. Forward slashes are used below
so paths such as `C:\Users\...` are never interpreted by CMake as escape
sequences.

> **VTK warning:** the default Windows build includes `ovf-convert`, so vcpkg
> builds VTK and its large dependency tree. A clean build can take a long time
> and many gigabytes. If you only need `ovf-batchfft`, use one of the no-VTK
> configurations below.

Build both CPU tools, including VTK/HDF5 conversion and FFTW analysis:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake".Replace('\', '/')
cmake -S . -B build -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DOVFTOOLKIT_BUILD_TOOLS=ON `
  -DOVFTOOLKIT_USE_CUFFT=OFF
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix build/stage
```

Build only the FFTW-based `ovf-batchfft`, without downloading or building VTK:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake".Replace('\', '/')
cmake -S . -B build-fftw -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVCPKG_MANIFEST_FEATURES=fftw `
  -DOVFTOOLKIT_BUILD_TOOLS=ON `
  -DOVFTOOLKIT_USE_FFTW=ON `
  -DOVFTOOLKIT_USE_CUFFT=OFF
cmake --build build-fftw --config Release --parallel
cmake --install build-fftw --config Release --prefix build-fftw/stage
```

Or build only the CUDA/cuFFT version without VTK or FFTW:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake".Replace('\', '/')
cmake -S . -B build-cuda -A x64 -T cuda=12.9 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DOVFTOOLKIT_BUILD_TOOLS=ON `
  -DOVFTOOLKIT_USE_FFTW=OFF `
  -DOVFTOOLKIT_USE_CUFFT=ON
cmake --build build-cuda --config Release --parallel
cmake --install build-cuda --config Release --prefix build-cuda/stage
```

The staged executables and any required dynamic-library dependencies are placed
together in the stage `bin` directory. Change `cuda=12.9` to the installed CUDA
toolset version.

The vcpkg manifest has two default features:

- `convert` supplies the VTK/HDF5 stack for `ovf-convert`;
- `fftw` supplies the CPU backend for `ovf-batchfft`.

`VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` is therefore the switch that avoids the
large conversion stack. Enabling `fftw` explicitly adds FFTW back without
adding VTK.

## Build selection

Optional project components use the same three-state convention:

| CMake setting | Meaning |
| --- | --- |
| `AUTO` | Build the component when its dependencies are usable. |
| `ON` | Require the component and stop configuration if required dependencies are missing. |
| `OFF` | Do not probe or build the component. |

The main settings are `OVFTOOLKIT_BUILD_TOOLS`,
`OVFTOOLKIT_BUILD_PYTHON_BINDING`, `OVFTOOLKIT_BUILD_WOLFRAM_BINDING`,
`OVFTOOLKIT_BUILD_MUMAX_RUNNER`, and `OVFTOOLKIT_BUILD_DOCS`. FFT backends are
selected independently with `OVFTOOLKIT_USE_FFTW` and
`OVFTOOLKIT_USE_CUFFT`.

The separately licensed mumax controller is packaged from
`tools/mumax-runner` as `ovftoolkit-mumax-runner`; it does not bundle CUDA,
mumax3, or `mumax-slave`.

## C++ library and developer documentation

The `ovfparser` C++23 library is always built. Its public interface uses
`std::span`, `std::mdspan`, `std::expected`, concepts, and RAII ownership. A
bundled Kokkos mdspan implementation is used when the standard library does not
yet provide a sufficiently recent `std::mdspan`.

When Doxygen is available, build the API documentation with:

```sh
cmake --build build --target ovftoolkit-docs
```

## License

Most original OVFToolkit source is distributed under the
[MIT License](LICENSE). The exception is `tools/mumax-runner/`, which is a
separate GPL-3.0-or-later distribution because its Go executable incorporates
mumax3. Third-party source and libraries retain their own licenses.

The license requirements of a binary depend on the components linked into it:

| Artifact | Distribution terms |
| --- | --- |
| `ovfparser` and the Python `ovftoolkit` wheel | MIT, with the notices for included dependencies. |
| `ovf-convert` | OVFToolkit code remains MIT; include the VTK, HDF5, NetCDF, and transitive dependency notices from the actual build. |
| CUDA-only `ovf-batchfft` | OVFToolkit code remains MIT; NVIDIA's terms also apply to any CUDA redistributables. |
| FFTW-linked `ovf-batchfft` | When FFTW is used under its GPL terms, distribute the executable under GPL-3.0-or-later and provide its complete corresponding source. Static and dynamic linking have the same licensing consequence. |
| FFTW-and-cuFFT `ovf-batchfft` | Not published by this project. OVFToolkit has no FFTW copyright-holder CUDA exception; obtain legal review or a commercial FFTW license before distributing such a binary. |
| `mumax-slave` and the mumax runner | GPL-3.0-or-later with mumax3's GPLv3 section 7 CUDA permission; provide the exact corresponding runner and mumax3 source. |
| `math-ovftoolkit` | OVFToolkit source remains MIT, but check the applicable Wolfram agreement before redistributing a WSTP-linked binary. |

See [LICENSING.md](LICENSING.md) for the release checklist and precise scope,
and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the dependency
inventory. Packaged artifacts must carry the verbatim notices for the exact
direct and transitive libraries they contain; for vcpkg builds these are under
`vcpkg_installed/<triplet>/share/<port>/copyright`.
