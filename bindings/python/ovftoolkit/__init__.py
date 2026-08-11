"""Python interface for reading and writing OOMMF vector-field files."""

from ._native import *
from .arrays import complex_view as complex_view
from .spectrum import eval_macroparams as eval_macroparams

