import numpy as np

import ovftoolkit as ovf


real = np.arange(24, dtype=np.float32).reshape(2, 3, 4)
complex_data = ovf.complex_view(real)
assert complex_data.shape == (2, 3, 2)
assert complex_data.dtype == np.complex64
assert np.shares_memory(real, complex_data)
np.testing.assert_array_equal(complex_data.real, real[..., 0::2])
np.testing.assert_array_equal(complex_data.imag, real[..., 1::2])

complex_data[0, 0, 0] = 10 + 20j
assert real[0, 0, 0] == 10
assert real[0, 0, 1] == 20

double_data = np.arange(8, dtype=np.float64).reshape(2, 4)
assert ovf.complex_view(double_data).dtype == np.complex128

try:
    ovf.complex_view(np.zeros((2, 3), dtype=np.float32))
except ValueError as error:
    assert "even final dimension" in str(error)
else:
    raise AssertionError("complex_view must reject an odd final dimension")
