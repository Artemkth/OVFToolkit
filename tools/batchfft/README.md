# ovf-batchfft

`ovf-batchfft` calculates a temporal Fourier spectrum from a sequence of
single-segment OVF snapshots. Each spatial value/component is transformed
independently along the time axis. Processing is batched and intermediate
results can spill to `.batchfft-temp`, so the complete time series does not
need to fit in RAM or GPU memory.

The tool supports FFTW on the CPU and cuFFT on NVIDIA GPUs. If both backends
were found at build time, the GPU is the default and `--engine` selects one.
If only one backend was built, it is selected automatically and the option is
not shown.

## Basic use

```sh
ovf-batchfft -o spectrum.ovf snapshots/*.ovf
```

Every input file must contain exactly one segment. The files must describe
compatible meshes with the same point count and value dimension; their order
on the command line does not matter because the tool sorts them by timestamp.
At least two snapshots are required.

By default, time in seconds is extracted from each segment's `Desc` field with
this ECMAScript regular expression:

```text
Total simulation time:\s+(.+?)\s+s
```

The first capture group must be a number. Supply a different expression when
your simulation uses another description format:

```sh
ovf-batchfft --time-regex 't\s*=\s*([^[:space:]]+)\s*s' \
  -o spectrum.ovf snapshots/*.ovf
```

The captured values are always interpreted as seconds; convert other units in
the metadata or choose a pattern that captures a value already expressed in
seconds.

## FFT engines and memory

When both engines are installed, select one explicitly with:

```sh
ovf-batchfft --engine gpu --gpu 0 --max-vram 6G --max-ram 4G \
  -o spectrum.ovf snapshots/*.ovf

ovf-batchfft --engine fftw --max-ram 4G \
  -o spectrum.ovf snapshots/*.ovf
```

Omit `--engine` in a single-backend build. The program checks that the selected
engine is available before opening input files.

Memory limits accept a decimal number followed by uppercase `K`, `M`, or `G`,
for example `512M`, `1.5G`, or `4G`. With FFTW, the default workspace limit is
the smaller of 4 GiB and 95% of currently available physical memory. With
cuFFT, the default transform limit is 95% of currently available GPU memory.
`--max-ram` also controls how much transformed output can be retained on the
host. If every padded transform batch fits within that limit, BatchFFT keeps
the complete spectrum in memory and uses the legacy sequential exporter.

## Sampling and output

Sampling jitter is removed by cubic-spline interpolation onto a uniform time
grid. Interpolation is skipped automatically when the timestamps are already
uniform within single-precision rounding tolerance; use `--no-reinterp` to
disable it unconditionally.

The transform is normalized by `sqrt(dt)` by default, producing amplitudes in
the input unit per `sqrt(Hz)`. Pass `--no-norm` to retain the FFT engine's raw,
unnormalized result.

The output is an OVF 2.0 file containing `N/2 + 1` segments for `N` input
samples. Segment `k` represents

```text
f[k] = k / (N * dt)
```

and its `Desc` contains `f = ... Hz`. Each original value component becomes an
adjacent real/imaginary pair, the header value dimension is doubled, and
available value labels and units are expanded accordingly. The DC component
is the first segment and the Nyquist component is included for even `N`.

By default, transformed point batches are written directly into prepared
frequency segments using positioned writes, so no full-size placeholder data
or temporary spectrum is written. FFT batches are rounded down to whole input
value vectors (`Vdim`) and each output chunk therefore contains complete mesh
points. If `--max-ram` can hold the complete padded spectrum, the former
in-memory sequential export strategy is selected automatically. Pass
`--buffered-export` to force the former strategy for larger spectra; it then
creates `.batchfft-temp` in the current working directory as needed and removes
it after a successful transform.

## Options

```text
-h, --help              Show help and compiled FFT engines
-v, --version           Show version and compiled FFT engines
-o, --output FILE       Output file (default: spectrum.ovf)
    --engine NAME       gpu or fftw; present only when both are built
    --gpu ID             CUDA device, or -1 for the current/default device
    --max-ram SIZE       Host buffer limit
    --max-vram SIZE      GPU transform limit (CUDA builds only)
    --time-regex REGEX   Timestamp extraction pattern
    --no-norm            Disable sqrt(dt) normalization
    --buffered-export    Use temporary-file and sequential output
    --no-reinterp        Disable timestamp dejittering
```

## Building

Enable the tools and provide Boost.Program_options plus at least one FFT
backend: single-precision FFTW (`fftw3f`, optionally `fftw3f_threads`) or the
CUDA Toolkit with cuFFT. GNU/Clang builds also use oneTBB for parallel standard
library execution.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOVFTOOLKIT_BUILD_TOOLS=ON
cmake --build build --parallel --target ovf-batchfft
```

CMake reports the backends it found during configuration. The executable is
installed into the normal binary directory by `cmake --install build`.
`OVFTOOLKIT_USE_CUFFT` and `OVFTOOLKIT_USE_FFTW` each accept `AUTO`, `ON`, or
`OFF`. `AUTO` discovers an available backend, `ON` requires it, and `OFF`
prevents its discovery. Release packaging should set both explicitly so that a
binary cannot acquire an unintended backend from the build environment.

## Distribution and licensing

The original OVFToolkit source for this tool is MIT-licensed, but the license
requirements of a linked executable depend on its FFT backend:

- a CUDA-only binary remains under the MIT license for OVFToolkit code and is
  also subject to NVIDIA's terms for any CUDA redistributables;
- an FFTW-linked binary using FFTW under its GPL terms must be distributed
  under GPL-3.0-or-later with its complete corresponding source; and
- a binary containing both FFTW and cuFFT is not published by this project.
  OVFToolkit has no FFTW copyright-holder exception for CUDA. Obtain legal
  review or a commercial FFTW license before distributing such a binary.

Static versus dynamic linking does not change the FFTW licensing requirement.
See the repository's [licensing guide](../../LICENSING.md) before packaging.
