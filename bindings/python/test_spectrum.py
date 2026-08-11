import numpy as np

import ovftoolkit as ovf
import ovftoolkit.spectrum as spectrum_module


class FakeField:
    def __init__(self, data, frequency):
        self.data = data
        self.header = {"desc": f"f = {frequency:g} Hz"}

    @property
    def complex_data(self):
        return ovf.complex_view(self.data)


class FakeReader(list):
    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False


data = np.array([[[[1, 2, 3, 4], [5, 6, 7, 8]]]], dtype=np.float32)
fields = FakeReader([FakeField(data, 25e6), FakeField(data * 2, 50e6)])
spectrum_module.reader = lambda _: fields

mask = np.array([[[[1], [3]]]], dtype=np.float32)
macroparams = ovf.eval_macroparams("unused.ovf", mask)
assert macroparams.dtype.names == ("f", "m0", "m1", "rms")
np.testing.assert_allclose(macroparams.f, [25e6, 50e6])
np.testing.assert_allclose(macroparams.m0, [4 + 5j, 8 + 10j])
np.testing.assert_allclose(macroparams.m1, [6 + 7j, 12 + 14j])
np.testing.assert_allclose(macroparams.rms, np.sqrt(138) * np.array([1, 2]))

unweighted = ovf.eval_macroparams("unused.ovf")
np.testing.assert_allclose(unweighted.m0, [3 + 4j, 6 + 8j])
np.testing.assert_allclose(unweighted.m1, [5 + 6j, 10 + 12j])
np.testing.assert_allclose(unweighted.rms, np.sqrt(102) * np.array([1, 2]))

vector_data = np.arange(1, 7, dtype=np.float32).reshape(1, 1, 1, 6)
spectrum_module.reader = lambda _: FakeReader([FakeField(vector_data, 25e6)])
vector_params = ovf.eval_macroparams("unused.ovf")
assert vector_params.dtype.names == ("f", "mx", "my", "mz", "rms")
np.testing.assert_allclose(vector_params.mx, [1 + 2j])
np.testing.assert_allclose(vector_params.my, [3 + 4j])
np.testing.assert_allclose(vector_params.mz, [5 + 6j])
