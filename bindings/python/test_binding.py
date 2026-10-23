import gc
import pathlib
import tempfile

import numpy as np

import ovftoolkit as ovf


field = ovf.VField()
assert field.data is None

header = field.header
header["xbase"] = 0.5
header["ybase"] = 0.5
header["zbase"] = 0.5
header["xstepsize"] = 1.0
header["ystepsize"] = 1.0
header["zstepsize"] = 1.0
header[ovf.OVFParameter.Title] = "binding test"
keys = list(header)
assert keys == header.keys()
assert ovf.OVFParameter.Title in keys
assert dict(header.items())[ovf.OVFParameter.Title] == "binding test"
assert len(header.values()) == len(header) == len(header.items())
assert header.get(ovf.OVFParameter.Title) == "binding test"
assert header.get("Desc") is None
sentinel = object()
assert header.get("Desc", sentinel) is sentinel
header["Title"] = None
assert ovf.OVFParameter.Title not in header

source = np.arange(6, dtype=np.float32).reshape(1, 1, 2, 3)
field.data = source
view = field.data
assert isinstance(view, np.ndarray)
assert view.shape == (1, 1, 2, 3)
assert view.dtype == np.float32
assert "meshtype" in header
assert header["Meshtype"] == ovf.MeshType.Rectangular
assert header[ovf.OVFParameter.Meshtype] == ovf.MeshType.Rectangular
assert header[ovf.OVFParameter.znodes] == 1
assert header[ovf.OVFParameter.ynodes] == 1
assert header[ovf.OVFParameter.xnodes] == 2
assert header[ovf.OVFParameter.valuedim] == 3
assert ovf.OVFParameter.pointcount not in header
source[0, 0, 0, 0] = -1
assert view[0, 0, 0, 0] == 0  # assignment copies into C++ ownership
view[0, 0, 1, 2] = 42
assert field.data[0, 0, 1, 2] == 42  # getter is a writable zero-copy view

try:
    field.complex_data
except ValueError as error:
    assert "even final dimension" in str(error)
else:
    raise AssertionError("complex_data must reject an odd final dimension")

complex_source = np.arange(16, dtype=np.float32).reshape(1, 2, 2, 4)
complex_field = ovf.VField()
complex_field.data = complex_source
complex_view = complex_field.complex_data
assert complex_view.shape == (1, 2, 2, 2)
assert complex_view.dtype == np.complex64
assert np.shares_memory(complex_field.data, complex_view)
np.testing.assert_array_equal(complex_view.real, complex_field.data[..., 0::2])
np.testing.assert_array_equal(complex_view.imag, complex_field.data[..., 1::2])
complex_view[0, 0, 0, 0] = 10 + 20j
assert complex_field.data[0, 0, 0, 0] == 10
assert complex_field.data[0, 0, 0, 1] == 20

try:
    header["does_not_exist"]
except KeyError:
    pass
else:
    raise AssertionError("unknown header identifiers must raise KeyError")

try:
    header["xnodes"] = 10
except AttributeError:
    pass
else:
    raise AssertionError("shape-derived field headers must be read-only")

del header, field
gc.collect()
assert view[0, 0, 1, 2] == 42  # ndarray owner keeps VField alive

field = ovf.VField()
field.data = np.arange(12, dtype=np.float64).reshape(2, 6)
assert field.data.shape == (2, 6)
assert field.header[ovf.OVFParameter.Meshtype] == ovf.MeshType.Irregular
assert field.header[ovf.OVFParameter.pointcount] == 2
assert field.header[ovf.OVFParameter.valuedim] == 3
assert ovf.OVFParameter.xnodes not in field.header
field.data = None
assert field.data is None
for parameter in (
    ovf.OVFParameter.Meshtype,
    ovf.OVFParameter.pointcount,
    ovf.OVFParameter.valuedim,
    ovf.OVFParameter.xnodes,
    ovf.OVFParameter.ynodes,
    ovf.OVFParameter.znodes,
):
    assert parameter not in field.header

standalone = ovf.SegmentHeader()
assert type(standalone) is type(field.header)
assert not hasattr(ovf, "OVFHeader")
assert not hasattr(ovf, "VFieldHeader")
standalone[ovf.OVFParameter.xnodes] = 7
assert standalone[ovf.OVFParameter.xnodes] == 7

metadata = ovf.SegmentHeader()
metadata[ovf.OVFParameter.Title] = "copied metadata"
metadata[ovf.OVFParameter.Desc] = "source description"
metadata[ovf.OVFParameter.xbase] = 3.5
metadata[ovf.OVFParameter.Meshtype] = ovf.MeshType.Irregular
metadata[ovf.OVFParameter.pointcount] = 99
metadata[ovf.OVFParameter.valuedim] = 8

destination = ovf.VField()
destination.data = np.zeros((2, 3, 4, 3), dtype=np.float32)
destination.header[ovf.OVFParameter.xmin] = -10.0
destination.header.copy_from(metadata)
assert destination.header[ovf.OVFParameter.Title] == "copied metadata"
assert destination.header[ovf.OVFParameter.Desc] == "source description"
assert destination.header[ovf.OVFParameter.xbase] == 3.5
assert ovf.OVFParameter.xmin not in destination.header
assert destination.header[ovf.OVFParameter.Meshtype] == ovf.MeshType.Rectangular
assert destination.header[ovf.OVFParameter.xnodes] == 4
assert destination.header[ovf.OVFParameter.ynodes] == 3
assert destination.header[ovf.OVFParameter.znodes] == 2
assert destination.header[ovf.OVFParameter.valuedim] == 3
assert ovf.OVFParameter.pointcount not in destination.header
destination.header.copy_from(destination.header)
assert destination.header[ovf.OVFParameter.Title] == "copied metadata"
assert destination.header[ovf.OVFParameter.xnodes] == 4

standalone.copy_from(metadata)
assert standalone[ovf.OVFParameter.Meshtype] == ovf.MeshType.Irregular
assert standalone[ovf.OVFParameter.pointcount] == 99
assert standalone[ovf.OVFParameter.valuedim] == 8
metadata.copy_from(metadata)
assert metadata[ovf.OVFParameter.Title] == "copied metadata"

for invalid in (ovf.SegmentHeader(), ovf.VField(), ovf.VField().header):
    try:
        invalid.validate()
    except ValueError as error:
        assert str(error)
    else:
        raise AssertionError("invalid objects must raise ValueError when validated")

with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "π-field.ovf"
    original = ovf.VField()
    original.data = np.arange(24, dtype=np.float32).reshape(1, 2, 4, 3)
    original.dummy_header((2.0, 3.0, 4.0))
    assert original.header[ovf.OVFParameter.xbase] == 1.0
    assert original.header[ovf.OVFParameter.ybase] == 1.5
    assert original.header[ovf.OVFParameter.zbase] == 2.0
    assert original.header[ovf.OVFParameter.xmin] == 0.0
    assert original.header[ovf.OVFParameter.xmax] == 8.0
    x, y, z = original.meshgrid()
    assert x.shape == y.shape == z.shape == original.data.shape[:-1]
    np.testing.assert_allclose(x[0, 0, :], [1.0, 3.0, 5.0, 7.0])
    np.testing.assert_allclose(y[0, :, 0], [1.5, 4.5])
    np.testing.assert_allclose(z[:, 0, 0], [2.0])

    original.header[ovf.OVFParameter.xbase] = None
    x_from_bounds, _, _ = original.meshgrid()
    np.testing.assert_allclose(x_from_bounds, x)
    original.header[ovf.OVFParameter.Title] = "Python context manager"

    with ovf.writer(path) as destination:
        destination.write(original)
        assert destination.segments_written == 1
        assert destination.deduction_report
    assert destination.closed

    with ovf.reader(path) as source:
        assert len(source) == 1
        read_header = source.header()
        assert isinstance(read_header, ovf.SegmentHeader)
        assert read_header[ovf.OVFParameter.Title] == "Python context manager"
        loaded, = source
        assert isinstance(loaded.data, np.ndarray)
        np.testing.assert_array_equal(loaded.data, original.data)
    assert source.closed
    np.testing.assert_array_equal(loaded.data, original.data)

incomplete = ovf.VField()
incomplete.data = np.zeros((1, 1, 1, 3), dtype=np.float32)
try:
    incomplete.meshgrid()
except ValueError as error:
    assert "stepsize" in str(error)
else:
    raise AssertionError("meshgrid must reject incomplete coordinate metadata")

irregular = ovf.VField()
irregular.data = np.zeros((1, 6), dtype=np.float32)
try:
    irregular.meshgrid()
except ValueError as error:
    assert "rectangular" in str(error)
else:
    raise AssertionError("meshgrid must reject irregular meshes")
