# Worked examples

These are complete, reproducible workflows. Their plots and tables are saved
in the source notebooks and rendered directly into the help pages; building
the documentation does not rerun the simulations.

## Process a vortex spectrum

[Read the rendered workflow](../generated/process-spectrum/OVFToolkitWorkFlow.ipynb)
or download its
[MuMax3 template](../generated/process-spectrum/vortex-dynamics.mx3.in).

This example runs a broadband excitation, transforms its OVF snapshots,
identifies and refines resonance peaks, and compares broadband mode profiles
with continuous-wave simulations.

## YIG strip dispersion

[Read the rendered workflow](../generated/yig-strip-dispersion/YIGStripDispersion.ipynb)
or download its
[MuMax3 template](../generated/yig-strip-dispersion/yig-strip.mx3.in).

This example constructs a physically normalized stripline field, compares
timestamp-dejittered and untreated spectra, and produces an RMS frequency--wave
vector map.

Use the download button on a rendered notebook page to retrieve the original
`.ipynb` file together with its stored outputs.

## Import a figure-eight geometry

[Read the rendered workflow](../generated/imported-figure-eight/FigureEightGeometry.ipynb)
or download its
[MuMax3 template](../generated/imported-figure-eight/figure-eight.mx3.in).

This example replaces Mathematica glyph rasterization with a portable Python
fill-fraction mask and initializes MuMax3 directly from the resulting OVF.

## Import and visualize a pig

[Read the rendered workflow](../generated/imported-pig/PigGeometry.ipynb),
download its [MuMax3 template](../generated/imported-pig/pig.mx3.in), or inspect
the [CC0 model provenance](../generated/imported-pig/model/README.md).

This example voxelizes an open 3D model with PyVista, imports it through
`ext_InitGeomFromOVF`, maps magnetization direction onto the pig surface, and
overlays selected three-dimensional demagnetizing-field streamlines.
