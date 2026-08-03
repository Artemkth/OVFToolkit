# OVFToolkit

OVFToolkit is a C++23 library and collection of tools for reading, writing,
validating, and processing OOMMF vector-field (OVF) files.

The repository is organised into:

- `OVFParser/` — the public C++ library;
- `tools/` — standalone command-line applications;
- `bindings/` — integrations with external languages and runtimes;
- `tests/` — library and numerical regression tests;
- `docs/` — documentation build configuration.

## Documentation

When Doxygen is installed, configure normally and build the documentation with:

```sh
cmake --build build --target ovftoolkit-docs
```

## License

OVFToolkit is distributed under the MIT License. See `LICENSE`.
