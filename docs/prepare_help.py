"""Stage executable examples for the conversion-only MkDocs build."""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "docs" / "help" / "generated"
DOXYGEN_INDEX = DESTINATION / "cpp-api" / "index.html"

EXAMPLES = {
    "process-spectrum": (
        ROOT / "examples" / "Process spectrum" / "OVFToolkitWorkFlow.ipynb",
        ROOT / "examples" / "Process spectrum" / "vortex-dynamics.mx3.in",
    ),
    "yig-strip-dispersion": (
        ROOT / "examples" / "YIG strip dispersion" / "YIGStripDispersion.ipynb",
        ROOT / "examples" / "YIG strip dispersion" / "yig-strip.mx3.in",
    ),
    "imported-figure-eight": (
        ROOT / "examples" / "Imported geometry - figure eight" / "FigureEightGeometry.ipynb",
        ROOT / "examples" / "Imported geometry - figure eight" / "figure-eight.mx3.in",
    ),
    "imported-pig": (
        ROOT / "examples" / "Imported geometry - pig" / "PigGeometry.ipynb",
        ROOT / "examples" / "Imported geometry - pig" / "pig.mx3.in",
        ROOT / "examples" / "Imported geometry - pig" / "model" / "origins-of-the-pig.obj",
        ROOT / "examples" / "Imported geometry - pig" / "model" / "README.md",
        ROOT / "examples" / "Imported geometry - pig" / "model" / "upstream-info.txt",
    ),
}


def validate_notebook(path: Path) -> None:
    notebook = json.loads(path.read_text(encoding="utf-8"))
    if notebook.get("nbformat") != 4 or not isinstance(notebook.get("cells"), list):
        raise ValueError(f"{path} is not a valid version 4 notebook")


def main() -> None:
    shutil.rmtree(DESTINATION, ignore_errors=True)
    for slug, sources in EXAMPLES.items():
        target = DESTINATION / slug
        target.mkdir(parents=True)
        for source in sources:
            if not source.is_file():
                raise FileNotFoundError(f"Example source is missing: {source}")
            if source.suffix == ".ipynb":
                validate_notebook(source)
            relative_target = (
                Path("model") / source.name
                if source.parent.name == "model"
                else Path(source.name)
            )
            (target / relative_target).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target / relative_target)
            print(f"Staged {source.relative_to(ROOT)}")

    doxygen = shutil.which("doxygen")
    if doxygen is None:
        raise RuntimeError(
            "Doxygen is required to build the C++ API reference; install it "
            "or build only the notebooks manually"
        )
    subprocess.run(
        [doxygen, str(ROOT / "docs" / "Doxyfile")],
        cwd=ROOT,
        check=True,
    )
    if not DOXYGEN_INDEX.is_file():
        raise RuntimeError(f"Doxygen did not generate {DOXYGEN_INDEX}")
    print(f"Generated {DOXYGEN_INDEX.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
