"""Plot the first layer of a rectangular OVF file in mumax3 style."""

import argparse

import matplotlib.pyplot as plt
import numpy as np
import ovftoolkit as ovf
from matplotlib.colors import hsv_to_rgb

parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("output", nargs="?", default="ovf-layer.png")
args = parser.parse_args()

with ovf.reader(args.input) as source:
    vector = source[0].data[0]

mx, my, mz = np.moveaxis(vector, -1, 0)
present = np.any(vector != 0, axis=-1)
hue = (np.arctan2(my, mx) / (2 * np.pi) + 1) % 1
saturation = np.clip((mz + 1) / 2, 0, 1)
rgba = np.dstack((hsv_to_rgb(np.dstack((hue, saturation, np.ones_like(hue)))), present))
y, x = np.mgrid[:vector.shape[0], :vector.shape[1]]

fig, axis = plt.subplots(figsize=(7, 7), layout="constrained")
axis.imshow(rgba, origin="lower", interpolation="nearest")
axis.quiver(x[present], y[present], mx[present], my[present], color="black",
            angles="xy", scale_units="xy", scale=1.35, width=0.0012, alpha=0.6)
axis.set(title="First OVF layer", xlabel="x cell", ylabel="y cell", aspect="equal")
fig.savefig(args.output, dpi=140)
