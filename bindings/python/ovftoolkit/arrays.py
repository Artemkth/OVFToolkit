"""NumPy array views used by OVFToolkit."""

from __future__ import annotations

import numpy as np
from numpy.typing import NDArray


def complex_view(data: NDArray) -> NDArray:
    """View adjacent real/imaginary values as complex numbers without copying."""
    if not isinstance(data, np.ndarray):
        raise TypeError("complex_view requires a NumPy array")
    if data.ndim == 0 or data.shape[-1] % 2:
        raise ValueError("Complex data requires an even final dimension")

    if data.dtype == np.float32:
        complex_dtype = np.complex64
    elif data.dtype == np.float64:
        complex_dtype = np.complex128
    else:
        raise TypeError("complex_view requires float32 or float64 data")

    if data.strides[-1] != data.dtype.itemsize:
        raise ValueError("Complex data pairs must be contiguous in the final dimension")
    return data.view(complex_dtype)
