import gc

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

standalone = ovf.OVFHeader()
standalone[ovf.OVFParameter.xnodes] = 7
assert standalone[ovf.OVFParameter.xnodes] == 7

for invalid in (ovf.OVFHeader(), ovf.VField(), ovf.VField().header):
    try:
        invalid.validate()
    except ValueError as error:
        assert str(error)
    else:
        raise AssertionError("invalid objects must raise ValueError when validated")
