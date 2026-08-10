# Licensing guide

This file explains how the licenses in this repository apply to its source
and to binaries assembled with optional third-party components. It is a
practical release guide, not legal advice.

## Repository source

Unless a file or directory says otherwise, original OVFToolkit source is
licensed under the [MIT License](LICENSE).

`tools/mumax-runner/` is a deliberately separate distribution licensed under
GPL-3.0-or-later. Its complete license is in
[`tools/mumax-runner/LICENSE`](tools/mumax-runner/LICENSE), and its component
notice is in [`tools/mumax-runner/NOTICE`](tools/mumax-runner/NOTICE).

Git submodules, generated files, and third-party dependencies retain their own
copyrights and licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the license files supplied by those projects.

## Expected binary licensing

| Artifact | Project license and release requirement |
| --- | --- |
| `ovfparser` | MIT; include `LICENSE` and applicable dependency notices. |
| Python `ovftoolkit` wheel | MIT; include the nanobind notice and, when the fallback is bundled, the mdspan notice. |
| `ovf-convert` | OVFToolkit code remains MIT; include the notices for VTK, HDF5, NetCDF, and every library actually shipped with the artifact. |
| CUDA-only `ovf-batchfft` | OVFToolkit code remains MIT; comply with the CUDA Toolkit EULA for any NVIDIA redistributables and include all other dependency notices. |
| FFTW-linked `ovf-batchfft` | When using FFTW under its GPL terms, the executable must be distributed under GPL-3.0-or-later, with the complete corresponding source and notices. FFTW is GPL-2.0-or-later; GPLv3 is used for the combined executable because an Apache-2.0 dependency such as oneTBB may also be linked. A commercial FFTW license may provide different terms. |
| FFTW-and-cuFFT `ovf-batchfft` | Do not publish this combined binary as an OVFToolkit release. The project has no FFTW copyright-holder exception comparable to mumax3's CUDA exception. Obtain appropriate legal review or a commercial FFTW license before distributing it. |
| `mumax-slave` and mumax runner package | GPL-3.0-or-later, plus mumax3's GPLv3 section 7 CUDA permission. Publish the exact corresponding mumax3 and runner source used for the binary. |
| `math-ovftoolkit` | OVFToolkit source remains MIT. Do not publish the WSTP-linked binary until the applicable Wolfram agreement has been checked for redistribution rights. |

Static and dynamic linking do not remove the FFTW GPL obligations. A private
build that is not conveyed is different from publishing a binary.

## Release checklist

For every release artifact:

1. Record the dependency versions, vcpkg triplet, feature set, and exact Git
   commits used to build it.
2. Include `LICENSE`, this guide, `THIRD_PARTY_NOTICES.md`, and the verbatim
   copyright/license files for all libraries shipped or linked into it.
3. For vcpkg builds, collect the authoritative files from
   `vcpkg_installed/<triplet>/share/<port>/copyright` and retain the generated
   `vcpkg.spdx.json` files as the basis of an SBOM.
4. For a GPL binary, publish the complete corresponding source beside the
   binary. A link to an upstream repository is not sufficient if it can move
   or does not exactly match the build.
5. Keep FFTW-only and CUDA-only `ovf-batchfft` artifacts separate. Do not put
   both backends in a public binary.
6. Review the resulting archive or wheel itself; configuring a compliant
   build does not guarantee that its packaging step copied every notice.
