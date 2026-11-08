# OpenModel

[![CI](https://github.com/OWNER/OpenModel/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/OpenModel/actions/workflows/ci.yml)

A C++17 library, command-line tool and lightweight desktop workbench for **finite-element meshes and results**:
import CAD and mesh files, generate and validate meshes, write solver input, run CalculiX or code_aster, and inspect the results in a WebGL viewer
where you can also pick nodes, elements and faces to build groups, supports and loads.

> **Status: 0.1.0.0, early.** The project was previously called MeshGen (development phases 8–50, last version 0.49.0); the version
> number restarted with the rename. See [Status and known limits](#status-and-known-limits) before relying on it.

## Features

| Area | What it does |
|---|---|
| Mesh core | `MeshData` (nodes, linear and quadratic elements, blocks, node sets, physical groups), topology (faces, edges, material interfaces), geometric validation and quality metrics |
| Mesh I/O | Abaqus / CalculiX `.inp` reader and writer; code_aster `.mail` reader and writer; compact binary **MGMB**; glTF / GLB export with PBR materials |
| Meshing backends *(optional)* | Gmsh, TetGen, Netgen, MFEM adapter, Open CASCADE (STEP / IGES / BREP) and Assimp importers |
| FEM results | CalculiX `.frd`, `.dat`, code_aster `.tab` tables and CSV readers: displacements, stresses, strains, principal and von Mises values, modal frequencies and mode shapes |
| Viewer | Single-file WebGL viewer (**MGWV**): colour fields, deformed shape, modal animation, section plane, GPU picking of nodes / elements / faces |
| Pre-processing | Selection → node / element / face groups → `fix`, `force`, `pressure` lines in an analysis script |
| Solver | Analysis script → complete CalculiX input → `ccx` run in the background with live log and cancel → results loaded back into the viewer; the same script can also write and run a **code_aster** case ([`docs/CODE_ASTER.md`](docs/CODE_ASTER.md)) |
| Interfaces | C++ library (`OpenModel::Core`), CLI (`openmodel`), desktop workbench (`openmodel-webui`, **Italian user interface**) |

## Quick start

Requirements: a C++17 compiler (developed with GCC 14.2), CMake ≥ 3.20, POSIX threads. Python 3 and Node.js are only needed by some optional tests.

```bash
git clone https://github.com/OWNER/OpenModel.git
cd OpenModel
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Try the CLI with the built-in demo, then open the result in a browser:

```bash
build/openmodel info
build/openmodel demo-web demo.mgwv 12      # a demo mesh with a scalar field
# open examples/webgl-fem-viewer/index.html in a browser and choose demo.mgwv
```

## Command-line tool

```text
openmodel info
openmodel validate-demo
openmodel demo-inp <output.inp>
openmodel demo-gltf <output.gltf>   |   demo-glb <output.glb>
openmodel demo-web <output.mgwv> [cells-per-side]
openmodel export-web <input.mgmb|input.inp> <output.mgwv> [--no-interior] [--field x|y|z|r]
                     [--results <file.frd|file.dat|file.csv> ...] [--step N] [--displacement NAME|none] [--modes N]
                     [--engineering-strain] [--no-derived] [--no-coordinate-check]
openmodel results-info <file.frd|file.dat|file.csv> [more files, merged]
openmodel write-input <mesh.mgmb|mesh.inp> <analysis.txt> <job.inp>
openmodel solve <mesh.mgmb|mesh.inp> <analysis.txt> <workdir> [--solver ccx-path] [--web out.mgwv]
openmodel export-aster <mesh> <analysis.txt> <dir> [--job NAME] [--solver MUMPS] [--cpus N] [--memory MB] [--time S]
openmodel solve-aster <mesh> <analysis.txt> <dir> [--launcher run_aster] [--web out.mgwv]
openmodel export-mail <mesh> <out.mail>
```

Example: load a CalculiX result on its mesh and view it.

```bash
openmodel export-web beam.inp beam.mgwv --results beam.frd --results beam.dat
```

### Analysis script and solver

A mesh carries no material, supports or loads; a short script adds them (one command per line, `#` for comments):

```text
material E=210000 nu=0.3 rho=7.85e-9
fix plane:x=0                       # clamp all nodes on x = 0
force plane:x=10 F=0,-100,0         # total force shared by the selected nodes
step static                         # or: step frequency modes=6
```

```bash
openmodel solve beam.inp analysis.txt job/ --solver /usr/bin/ccx --web job/result.mgwv
```

Selectors, all commands and the exact limits are documented in [`docs/SOLVER.md`](docs/SOLVER.md).

## Using the library from CMake

```bash
cmake --install build --prefix /opt/openmodel
```

```cmake
find_package(OpenModel 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE OpenModel::Core)
```

```cpp
#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>
```

Use `CMAKE_PREFIX_PATH` or `OpenModel_DIR` for a non-standard prefix. A minimal consumer is in [`examples/cmake-consumer`](examples/cmake-consumer).
The installed package contains the core; optional backends are built from source and are not exported as relocatable components.

## Desktop workbench

`openmodel-webui` is a desktop GUI (ribbon bar, embedded WebGL viewer) built on the [WebUI](https://webui.me) library, which is **not vendored**:
install its SDK and point CMake at it.

```bash
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release \
      -DOPENMODEL_BUILD_WEBUI=ON -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk
cmake --build build-gui --target openmodel-webui
```

It needs a Chromium-based browser at run time. The interface strings are currently in Italian.

## Optional backends

Each backend is off by default and needs its native library to be found by CMake.

| CMake option | Adds |
|---|---|
| `OPENMODEL_BUILD_GMSH_BACKEND` | Gmsh surface and volume meshing. Point `GMSH_ROOT` to the Gmsh install prefix (`include/gmsh.h`, `lib/`, `bin/`); use `GMSH_ROOT_DEBUG` / `GMSH_ROOT_RELEASE` for separate Debug and Release builds. `GMSH_INCLUDE_DIR`, `GMSH_LIBRARY_DEBUG/RELEASE` and `GMSH_RUNTIME_LIBRARY_DEBUG/RELEASE` override the search. The C++ API must come from a build made by the same compiler (with MSVC, a Gmsh built with MSVC: the official MinGW SDK does not link); a configure-time link check warns about it (`OPENMODEL_GMSH_LINK_CHECK`) |
| `OPENMODEL_BUILD_OCC_BACKEND` | Open CASCADE import of STEP / IGES / BREP, assemblies |
| `OPENMODEL_BUILD_OCC_BACKEND` | Open CASCADE CAD import (STEP / IGES / BREP). Select the installation with `OpenCASCADE_DIR` (folder with `OpenCASCADEConfig.cmake`) or `OPENCASCADE_ROOT` / `CASROOT` (install prefix); use the same installation as Gmsh |
| `OPENMODEL_BUILD_ASSIMP_BACKEND` | Assimp import of general 3D formats |
| `OPENMODEL_BUILD_TETGEN_BACKEND` | TetGen volume meshing |
| `OPENMODEL_BUILD_NETGEN_BACKEND` (+ `OPENMODEL_NETGEN_HAS_OCC`) | Netgen meshing |
| `OPENMODEL_BUILD_MFEM_BACKEND` | MFEM mesh adapter |

General options: `OPENMODEL_BUILD_TESTS` (ON), `OPENMODEL_BUILD_CLI` (ON), `OPENMODEL_BUILD_SHARED` (ON), `OPENMODEL_BUILD_BENCHMARKS` (OFF),
`OPENMODEL_BUILD_WEBUI` (OFF), `OPENMODEL_TEST_WEBGL_VIEWER` (OFF, needs Node.js with Playwright).

## Repository layout

```text
include/OpenModel/   public headers
src/                 library sources; src/cli (openmodel), src/webui (openmodel-webui + gui.html), src/importers
examples/            webgl-fem-viewer (the viewer), webgl-viewer (glTF), cmake-consumer
tests/               C++ tests, tests/support (fixtures, fake ccx), tests/webgl (browser and GUI tests)
benchmarks/          optional synthetic performance benchmarks
docs/                format and feature documentation
cmake/               package configuration template
```

## Documentation

- [Fatigue analysis — detailed MathJax HTML manual](docs/fatigue/index.html) – theory, FEM result contract, tensor history, Rainflow, S–N, strain-life, multiaxial extension contract, spectra, reports and C++ integration examples.
- [`docs/SOLVER.md`](docs/SOLVER.md) – analysis script, solver launch, picking and groups
- [`docs/CODE_ASTER.md`](docs/CODE_ASTER.md) – code_aster export, run, mesh import and result tables
- [`docs/FEM_RESULTS.md`](docs/FEM_RESULTS.md) – result formats and how they are bound to a mesh
- [`docs/MESH_IMPORTERS.md`](docs/MESH_IMPORTERS.md) – mesh importers
- [`docs/MGMB_FORMAT.md`](docs/MGMB_FORMAT.md), [`docs/MGWV_FORMAT.md`](docs/MGWV_FORMAT.md) – binary formats
- [`tests/webgl/README.md`](tests/webgl/README.md) – browser and GUI end-to-end tests
- [`docs/DEVELOPMENT_LOG.md`](docs/DEVELOPMENT_LOG.md) – historical phase notes (Italian, former MeshGen numbering)
- [`CHANGELOG.md`](CHANGELOG.md), [`CONTRIBUTING.md`](CONTRIBUTING.md)

## Status and known limits

What the automated tests cover (62 CTest tests on Linux with GCC, including headless-browser tests of the viewer and of the real GUI):
mesh core, I/O round trips, result readers, result binding, group creation, analysis-script parsing, input writing, process control, viewer rendering and picking.

What is **not** verified:

- The generated CalculiX input has never been run through a real `ccx`; the tests use a stand-in (`tests/support/fake_ccx.py`) and check the plumbing, not the physics.
- The code_aster export has never been run through a real code_aster (stand-in `tests/support/fake_aster.py`); MED files are not supported. See [`docs/CODE_ASTER.md`](docs/CODE_ASTER.md).
- The solver runner is POSIX only (Linux, macOS); on Windows it returns an error. Only Linux has been built and tested.
- Gmsh, Open CASCADE, Assimp, TetGen, Netgen and MFEM backends were not compiled in the latest test environment.
- Nodal forces are shared equally among the selected nodes (not consistent for quadratic elements; use `pressure`); one material and one step per analysis; no contact or nonlinear materials.
- Groups cannot yet be deleted or renamed; picking performance with very large selections and touch input are untested.
- The code base is C++17; migration to C++23 (`std::expected`) is not done.

## License

OpenModel is released under the [MIT License](LICENSE). Optional backends (Gmsh, Open CASCADE, Assimp, TetGen, Netgen, MFEM, WebUI) and
CalculiX are separate projects with their own licenses, which apply when you build against or run them; none of them is bundled in this repository.
