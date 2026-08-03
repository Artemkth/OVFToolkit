import gc

import numpy as np

import ovftoolkit as ovf


field = ovf.VField()
assert field.data is None

header = field.header
header[ovf.OVFParameter.Meshtype] = ovf.MeshType.Rectangular
header[ovf.OVFParameter.valuedim] = 3
header[ovf.OVFParameter.xnodes] = 2
header[ovf.OVFParameter.ynodes] = 1
header[ovf.OVFParameter.znodes] = 1
header["xbase"] = 0.5
header["ybase"] = 0.5
header["zbase"] = 0.5
header["xstepsize"] = 1.0
header["ystepsize"] = 1.0
header["zstepsize"] = 1.0
assert "meshtype" in header
assert header["Meshtype"] == ovf.MeshType.Rectangular
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

source = np.arange(6, dtype=np.float32).reshape(2, 3)
field.data = source
view = field.data
assert view.shape == (1, 1, 2, 3)
assert view.dtype == np.float32
source[0, 0] = -1
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
    header["xnodes"] = "two"
except TypeError:
    pass
else:
    raise AssertionError("wrong header types must raise TypeError")

del header, field
gc.collect()
assert view[0, 0, 1, 2] == 42  # ndarray owner keeps VField alive

field = ovf.VField()
field.data = np.arange(4, dtype=np.float64)
assert field.data.shape == (4,)
field.data = None
assert field.data is None
