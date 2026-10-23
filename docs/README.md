# Building the help site

The help site renders the saved outputs from the example notebooks and builds
the C++ Doxygen reference. It never executes notebook cells, launches MuMax3,
or regenerates simulation data.

From the repository root, create a documentation environment and build:

```sh
python -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
# Install Doxygen with the platform package manager.
python docs/prepare_help.py
python -m mkdocs build --strict
```

On PowerShell, activate the environment with:

```powershell
.venv-docs\Scripts\Activate.ps1
```

For a live local preview, replace the final command with:

```sh
python -m mkdocs serve
```

`prepare_help.py` copies each notebook and companion MuMax template and runs
Doxygen into the ignored `docs/help/generated` staging directory. Edit and
execute notebooks only under `examples/`; never edit the staged copies.
