"""Create a small rectangular OVF file from a NumPy array."""

import numpy as np
import ovftoolkit as ovf

y, x = np.mgrid[-1:1:64j, -1:1:64j]
data = np.stack((-y, x, np.zeros_like(x)), axis=-1)[None].astype(np.float32)
data /= np.maximum(np.linalg.norm(data, axis=-1, keepdims=True), 1e-12)

field = ovf.VField()
field.data = data
field.dummy_header((2e-9, 2e-9, 1e-9))
field.header["Title"] = "In-plane vortex"

with ovf.writer("vortex.ovf") as destination:
    destination.write(field)
