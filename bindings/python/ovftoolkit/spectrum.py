"""Helpers for reducing OVF frequency spectra."""

from __future__ import annotations

import re
from os import PathLike
from typing import Pattern

import numpy as np
from numpy.typing import ArrayLike

from ._native import reader


DEFAULT_FREQUENCY_REGEX = r"f\s*=\s*([^\s]+)\s*Hz"


def eval_macroparams(
    file_name: str | PathLike[str],
    mask: ArrayLike | None = None,
    frequency_regex: str | Pattern[str] = DEFAULT_FREQUENCY_REGEX,
) -> np.recarray:
    """Return spatially averaged complex components for each spectrum segment.

    Adjacent values in the final data dimension are interpreted as the real
    and imaginary parts of one component. ``mask`` supplies non-negative
    spatial weights and may include a trailing singleton value dimension.
    A three-component spectrum is named ``mx``, ``my``, and ``mz``; other
    component counts use ``m0`` ... ``mN``. The remaining fields are ``f`` and
    ``rms``.
    """
    pattern = re.compile(frequency_regex)

    with reader(file_name) as spectrum:
        if not len(spectrum):
            raise ValueError("The spectrum contains no segments")

        first = spectrum[0]
        spatial_shape = first.data.shape[:-1]
        value_dim = first.data.shape[-1]
        if value_dim % 2:
            raise ValueError("Complex spectrum value dimension must be even")
        component_count = value_dim // 2
        component_names = (
            ("mx", "my", "mz")
            if component_count == 3
            else tuple(f"m{i}" for i in range(component_count))
        )

        dtype = (
            [("f", np.float64)]
            + [(name, np.complex128) for name in component_names]
            + [("rms", np.float64)]
        )
        result = np.empty(len(spectrum), dtype=dtype).view(np.recarray)

        weights = None if mask is None else np.asarray(mask)
        if weights is not None:
            if weights.shape == spatial_shape + (1,):
                weights = weights[..., 0]
            try:
                weights = np.broadcast_to(weights, spatial_shape).reshape(-1)
            except ValueError as error:
                raise ValueError(
                    f"Mask shape {np.shape(mask)} cannot be broadcast to "
                    f"spectrum shape {spatial_shape}"
                ) from error
            if not np.all(np.isfinite(weights)) or np.any(weights < 0):
                raise ValueError("Mask weights must be finite and non-negative")
            if weights.sum() == 0:
                raise ValueError("Mask must contain at least one positive weight")

        for row, field in enumerate(spectrum):
            data = field.data
            if data.shape != spatial_shape + (value_dim,):
                raise ValueError("All spectrum segments must have the same shape")

            frequency = pattern.search(field.header.get("desc", ""))
            if frequency is None:
                raise ValueError(f"Frequency was not found in spectrum segment {row}")

            values = field.complex_data.reshape(-1, component_count)
            power = np.sum(np.abs(values) ** 2, axis=1)

            result.f[row] = float(frequency.group(1))
            means = (
                values.mean(axis=0)
                if weights is None
                else np.average(values, axis=0, weights=weights)
            )
            for name, mean in zip(component_names, means):
                result[name][row] = mean
            result.rms[row] = np.sqrt(
                power.mean() if weights is None else np.average(power, weights=weights)
            )

    return result
