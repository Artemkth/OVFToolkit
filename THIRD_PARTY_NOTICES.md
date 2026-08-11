# Third-party notices

OVFToolkit can use the projects below. This inventory describes the direct
dependencies; an actual binary distribution must also include the verbatim
notices for the exact direct and transitive libraries it contains. The vcpkg
`share/<port>/copyright` files from the build are authoritative for a
vcpkg-produced artifact.

| Dependency | Used by | License / terms | Upstream |
| --- | --- | --- | --- |
| Boost | core headers and command-line tools | Boost Software License 1.0 | <https://www.boost.org/users/license.html> |
| nanobind | Python extension | BSD 3-Clause | <https://github.com/wjakob/nanobind> |
| Kokkos mdspan | fallback when `std::mdspan` is unavailable | Apache-2.0 WITH LLVM-exception | <https://github.com/kokkos/mdspan> |
| FFTW | CPU `ovf-batchfft` backend | GPL-2.0-or-later, or a commercial license | <https://www.fftw.org/> |
| oneTBB | parallel execution used by command-line tools | Apache-2.0 | <https://github.com/uxlfoundation/oneTBB> |
| VTK | `ovf-convert` | BSD 3-Clause; bundled VTK dependencies have additional notices | <https://vtk.org/> |
| HDF5 | `ovf-convert` | HDF5's BSD-style license | <https://www.hdfgroup.org/solutions/hdf5/> |
| NetCDF-C | VTK/NetCDF conversion support | BSD 3-Clause | <https://github.com/Unidata/netcdf-c> |
| NVIDIA CUDA Toolkit and cuFFT | CUDA batch FFT and mumax3 | NVIDIA CUDA Toolkit EULA and component-specific redistribution terms | <https://docs.nvidia.com/cuda/eula/index.html> |
| mumax3 | compiled into `mumax-slave` | GPL-3.0-or-later with a GPLv3 section 7 CUDA permission | <https://github.com/mumax/3> |
| FindMathematica and Wolfram WSTP | Wolfram binding build/runtime | FindMathematica's license and the applicable Wolfram agreement | <https://github.com/sakra/FindMathematica> |
| PyVista and VTK Python packages | optional imported-geometry example visualization | MIT and BSD 3-Clause, respectively | <https://pyvista.org/> |
| Origins of the Pig by Keenan Crane | CC0 mesh used by the pig geometry example | CC0 1.0 Universal Public Domain Dedication | <https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/> |

The nanobind and mdspan license texts used by source distributions and wheels
are included under [`LICENSES/`](LICENSES/). Their inclusion records those
dependencies' terms; it does not change OVFToolkit's MIT license.
