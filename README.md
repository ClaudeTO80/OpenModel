# OpenModel

[![CI](https://github.com/ClaudeTO80/OpenModel/actions/workflows/ci.yml/badge.svg)](https://github.com/ClaudeTO80/OpenModel/actions/workflows/ci.yml)

A C++17 library, command-line tool and lightweight desktop workbench for **finite-element meshes and results**:
import CAD and mesh files, generate and validate meshes, write solver input, run CalculiX or code_aster, and inspect the results in a WebGL viewer
where you can also pick nodes, elements and faces to build groups, supports and loads. A fatigue-analysis library works on the loaded results.

> **Version 0.1.1 — early.** The project was previously called MeshGen; the notes of that development (phases 8–50) are in
> [`docs/DEVELOPMENT_LOG.md`](docs/DEVELOPMENT_LOG.md). See [Status and known limits](#status-and-known-limits) before relying on it.

## Features

| Area | What it does |
|---|---|
| Mesh core | `MeshData` (nodes, linear and quadratic elements, blocks, node sets, physical groups), topology (faces, edges, material interfaces), geometric validation and quality metrics |
| Mesh I/O | Abaqus / CalculiX `.inp` reader and writer; code_aster `.mail` reader and writer; compact binary **MGMB**; glTF / GLB export with PBR materials |
| Meshing backends *(optional)* | Gmsh, TetGen, Netgen, MFEM adapter, Open CASCADE (STEP / IGES / BREP) and Assimp importers |
| CAD → FEM groups | Named CAD groups (XDE layers, named sub-shapes) and user-defined B-Rep face groups are carried onto the mesh as dimension-2 groups, by Gmsh at meshing time or afterwards by geometry (`mapCADFacesToMesh`); face groups are stored in a `.omgroups` sidecar next to the CAD file ([`docs/cad-face-groups-phase-4.md`](docs/cad-face-groups-phase-4.md)) |
| FEM results | CalculiX `.frd`, `.dat`, code_aster `.tab` tables and CSV readers: displacements, stresses, strains, principal and von Mises values, modal frequencies and mode shapes |
| Viewer | Single-file WebGL viewer (**MGWV**): colour fields, deformed shape, modal animation, section plane, GPU picking of nodes / elements / faces |
| Pre-processing | Selection → node / element / face groups → `fix`, `force`, `pressure` lines in an analysis script |
| Solver | Analysis script → complete CalculiX input → `ccx` run in the background with live log and cancel → results loaded back into the viewer; the same script can also write and run a **code_aster** case ([`docs/CODE_ASTER.md`](docs/CODE_ASTER.md)) |
| Fatigue *(library)* | `OpenModel::Fatigue`: stress history from FEM results, Rainflow counting, S–N and strain-life damage, Palmgren–Miner and Wang–Brown, mean-stress corrections, operating spectra, reports and damage maps for the viewer ([manual](docs/fatigue/index.html)). Not exposed by the CLI or the workbench yet |
| Project manifest *(library)* | `OpenModelProject`: an `.omproj` document that indexes CAD, mesh, analysis and result files with relative paths, SHA-256 and size checks ([`docs/PROJECT_PERSISTENCE.md`](docs/PROJECT_PERSISTENCE.md)). Not used by the CLI or the workbench yet |
| Interfaces | C++ library (`OpenModel::Core`), CLI (`openmodel`), desktop workbench (`openmodel-webui`, **Italian user interface**) |

## Quick start

Requirements: a C++17 compiler (developed with GCC 14.2; also built with GCC 13.3), CMake ≥ 3.20, POSIX threads. Python 3 and Node.js are only needed by some optional tests.

```bash
git clone https://github.com/ClaudeTO80/OpenModel.git
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
openmodel --version
openmodel info
openmodel validate-demo
openmodel demo-inp <output.inp>
openmodel demo-gltf <output.gltf>   |   demo-glb <output.glb>
openmodel demo-web <output.mgwv> [cells-per-side]
openmodel export-web <input.mgmb|input.inp|input.mail> <output.mgwv> [--no-interior] [--field x|y|z|r]
                     [--results <file.frd|file.dat|file.csv> ...] [--step N] [--displacement NAME|none] [--modes N]
                     [--engineering-strain] [--no-derived] [--no-coordinate-check]
openmodel results-info <file.frd|file.dat|file.csv> [more files, merged]
openmodel write-input <mesh.mgmb|mesh.inp> <analysis.txt> <job.inp>
openmodel solve <mesh.mgmb|mesh.inp> <analysis.txt> <workdir> [--solver ccx-path] [--web out.mgwv]
openmodel export-aster <mesh> <analysis.txt> <dir> [--job NAME] [--solver MUMPS] [--cpus N] [--memory MB] [--time S]
openmodel solve-aster <mesh> <analysis.txt> <dir> [--launcher run_aster] [--web out.mgwv]
openmodel export-mail <mesh> <out.mail>
openmodel mesh-volume <input.step|input.iges|input.brep> <output.inp|.mgmb|.mgwv> <max-element-size> [order]   # only with the Gmsh backend
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

## Version

The current version is **0.1.1** ([`CHANGELOG.md`](CHANGELOG.md)). It is set in one place, the CMake cache option `OPENMODEL_VERSION`
(`MAJOR.MINOR.PATCH`, default `0.1.1`; minor and patch between 0 and 99, no leading zeros):

```bash
cmake -S . -B build -DOPENMODEL_VERSION=0.2.0     # override, e.g. from a release script
```

From that value CMake derives the project version, the installed CMake package (`find_package(OpenModel 0.1 CONFIG)` is satisfied by a package with the same
major version and at least that version), the CPack file names and the generated header `<OpenModel/Version.hpp>`, which is installed with the headers:

```cpp
#include <OpenModel/Version.hpp>

std::string s = OpenModel::versionString();        // "0.1.1", the version of the linked library
OpenModel::Version v = OpenModel::version();       // v.major, v.minor, v.patch
#if OPENMODEL_VERSION_NUMBER >= 101                // major*10000 + minor*100 + patch (0.1.1 -> 101)
// ... code that needs 0.1.1 or later
#endif
```

`OPENMODEL_VERSION_MAJOR`, `_MINOR`, `_PATCH`, `_NUMBER` and `_STRING` are macros that describe the headers a program was compiled against;
`OpenModel::version()` and `OpenModel::versionString()` describe the library it is linked with, which can differ with a shared library.
The CLI prints it with `openmodel --version`.

`<OpenModel/BuildInfo.hpp>` describes how the linked library was built (generated by CMake at configure time):

```cpp
#include <OpenModel/BuildInfo.hpp>

const OpenModel::BuildInfo& b = OpenModel::buildInfo();
// b.version, b.configuration (Release, ...), b.compiler ("GNU 13.3.0"), b.cxxStandard, b.system, b.cmakeVersion
for (const OpenModel::BuildOption& o : b.options) { /* o.name = "OPENMODEL_BUILD_OCC_BACKEND", o.value = "ON" or "OFF" */ }
```

## Desktop workbench

`openmodel-webui` is a desktop GUI (ribbon bar, embedded WebGL viewer) built on the [WebUI](https://webui.me) library, which is **not vendored**:
install its SDK and point CMake at it.

```bash
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release \
      -DOPENMODEL_BUILD_WEBUI=ON -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk
cmake --build build-gui --target openmodel-webui
```

It needs a Chromium-based browser at run time. The interface strings are currently in Italian.

**Help > About** shows the program version, how OpenModel was built (configuration, compiler, C++ standard, system, `OPENMODEL_*` options) and the libraries linked into the program (WebUI, Assimp, Open CASCADE, Gmsh, TetGen, Netgen, as enabled). For each library it lists the version the library itself reports, where CMake found it and, when available, the options it was built with: the settings recorded by the Open CASCADE CMake package, the compile flags reported by Assimp and the build information of Gmsh (`General.BuildInfo`). Information a library does not expose (for example the version of TetGen or Netgen) is shown as "non disponibile". The **Copia** button puts the same report on the clipboard, ready to paste into a bug report.

## Optional backends

Each backend is off by default and needs its native library to be found by CMake.

| CMake option | Adds |
|---|---|
| `OPENMODEL_BUILD_GMSH_BACKEND` | Gmsh surface and volume meshing. Point `GMSH_ROOT` to the Gmsh install prefix (`include/gmsh.h`, `lib/`, `bin/`); use `GMSH_ROOT_DEBUG` / `GMSH_ROOT_RELEASE` for separate Debug and Release builds. `GMSH_INCLUDE_DIR`, `GMSH_LIBRARY_DEBUG/RELEASE` and `GMSH_RUNTIME_LIBRARY_DEBUG/RELEASE` override the search. The C++ API must come from a build made by the same compiler (with MSVC, a Gmsh built with MSVC: the official MinGW SDK does not link); a configure-time link check warns about it (`OPENMODEL_GMSH_LINK_CHECK`). On Windows `winmm`, `ws2_32`, `wsock32`, `psapi` and `ole32` are linked for a static `gmsh.lib`; `OPENMODEL_GMSH_EXTRA_LIBRARIES` adds other libraries a static Gmsh needs |
| `OPENMODEL_BUILD_OCC_BACKEND` | Open CASCADE CAD import (STEP / IGES / BREP, assemblies, XDE layers and named sub-shapes). Select the installation with `OpenCASCADE_DIR` (folder with `OpenCASCADEConfig.cmake`) or `OPENCASCADE_ROOT` / `CASROOT` (install prefix); use the same installation as Gmsh |
| `OPENMODEL_BUILD_ASSIMP_BACKEND` | Assimp import of general 3D formats; triangles are meshed and grouped by geometric selectors (plane, normal, box, cylinder, sphere, cone), with no CAD face identity ([`docs/assimp-surface-fallback.md`](docs/assimp-surface-fallback.md)) |
| `OPENMODEL_BUILD_TETGEN_BACKEND` | TetGen volume meshing |
| `OPENMODEL_BUILD_NETGEN_BACKEND` (+ `OPENMODEL_NETGEN_HAS_OCC`) | Netgen meshing |
| `OPENMODEL_BUILD_MFEM_BACKEND` | MFEM mesh adapter |

General options: `OPENMODEL_VERSION` (`0.1.1`, see [Version](#version)), `OPENMODEL_BUILD_TESTS` (ON), `OPENMODEL_BUILD_CLI` (ON), `OPENMODEL_BUILD_SHARED` (ON), `OPENMODEL_BUILD_BENCHMARKS` (OFF),
`OPENMODEL_BUILD_WEBUI` (OFF), `OPENMODEL_TEST_WEBGL_VIEWER` (OFF, needs Node.js with Playwright).

### Gmsh meshing controls

`MeshingOptions` (Gmsh backend) controls, besides the element sizes and the order:

- `structure` / `structuredShape`: `Unstructured` (tetrahedra, any solid) or `Structured`, which sweeps the end-face mesh of a
  straight prismatic solid (beam, bar) in layers into hexahedra (`Hexahedron`: triangles that the recombination leaves become
  prisms) or prisms (`Prism`). Other solids fall back to tetrahedra with a diagnostic, or fail when `structuredFallback` is
  false. Second-order structured meshes use 20-node hexahedra and 15-node prisms in Abaqus node order. Conformal interfaces
  between several solids are not available for structured meshes.
- `thinFaceSizeFactor`: limits the element size at the corners of each face to the given multiple of the face width
  (2 * area / perimeter). It avoids needle triangles on strips such as the flange edges of a beam; 1 is a good start.
- `computeMeshQuality(mesh)` (`MeshQuality.hpp`) reports the quality of every element type (1 = ideal; non-positive = inverted).

## Repository layout

```text
include/OpenModel/   public headers (include/OpenModel/Fatigue for the fatigue module)
src/                 library sources; src/fatigue (fatigue module), src/cli (openmodel), src/webui (openmodel-webui + gui.html), src/importers
examples/            webgl-fem-viewer (the viewer), webgl-viewer (glTF), cmake-consumer, cmake-assimp-consumer
tests/               C++ tests, tests/fatigue, tests/support (fixtures, fake ccx / code_aster), tests/webgl (browser and GUI tests)
benchmarks/          optional synthetic performance benchmarks
docs/                format and feature documentation; docs/index.html is the HTML manual
cmake/               package configuration template, Version.hpp.in (generated version header)
```

## Documentation

- [HTML manual](docs/index.html) – getting started, data model, mesh mathematics, CAD and meshing, analysis, formats, architecture, glossary.
- [Fatigue analysis — detailed MathJax HTML manual](docs/fatigue/index.html) – theory, FEM result contract, tensor history, Rainflow, S–N, strain-life, multiaxial extension contract, spectra, reports and C++ integration examples.
- Fatigue module reference: [`FATIGUE_RESULT_CONTRACT.md`](docs/FATIGUE_RESULT_CONTRACT.md), [`FATIGUE_STRESS_HISTORY.md`](docs/FATIGUE_STRESS_HISTORY.md), [`FATIGUE_CYCLE_COUNTING.md`](docs/FATIGUE_CYCLE_COUNTING.md), [`FATIGUE_MULTIAxIAL_CYCLE_CONTRACT.md`](docs/FATIGUE_MULTIAxIAL_CYCLE_CONTRACT.md), [`FATIGUE_DAMAGE_MODELS.md`](docs/FATIGUE_DAMAGE_MODELS.md), [`FATIGUE_REPORT.md`](docs/FATIGUE_REPORT.md), [`FATIGUE_SPECTRUM_REPORT.md`](docs/FATIGUE_SPECTRUM_REPORT.md), [`FATIGUE_DAMAGE_MAP.md`](docs/FATIGUE_DAMAGE_MAP.md)
- [`docs/SOLVER.md`](docs/SOLVER.md) – analysis script, solver launch, picking and groups
- [`docs/CODE_ASTER.md`](docs/CODE_ASTER.md) – code_aster export, run, mesh import and result tables
- [`docs/FEM_RESULTS.md`](docs/FEM_RESULTS.md) – result formats and how they are bound to a mesh
- [`docs/MESH_IMPORTERS.md`](docs/MESH_IMPORTERS.md) – mesh importers
- [`docs/cad-face-groups-phase-4.md`](docs/cad-face-groups-phase-4.md) – CAD face → Gmsh surface mapping; the earlier steps are in `docs/cad-fem-group-mapping-phase-3.*.md`
- [`docs/assimp-surface-fallback.md`](docs/assimp-surface-fallback.md) – Assimp import and geometric selectors without CAD
- [`docs/PROJECT_PERSISTENCE.md`](docs/PROJECT_PERSISTENCE.md) – project manifest (`.omproj`)
- [`docs/MGMB_FORMAT.md`](docs/MGMB_FORMAT.md), [`docs/MGWV_FORMAT.md`](docs/MGWV_FORMAT.md) – binary formats
- [`tests/webgl/README.md`](tests/webgl/README.md) – browser and GUI end-to-end tests
- [`docs/DEVELOPMENT_LOG.md`](docs/DEVELOPMENT_LOG.md) – historical phase notes (Italian, former MeshGen numbering)
- [`CHANGELOG.md`](CHANGELOG.md), [`CONTRIBUTING.md`](CONTRIBUTING.md)

## Status and known limits

What the automated tests cover (67 CTest tests in the default configuration, all passing on Linux with GCC 13.3; the headless-browser tests of the viewer and of the GUI are opt-in, see below):
mesh core, I/O round trips, result readers, result binding, group creation, CAD face mapping, analysis-script parsing, input writing for CalculiX and code_aster, process control, the fatigue module and the project manifest.
With `OPENMODEL_TEST_WEBGL_VIEWER=ON` and Node.js with Playwright, CTest also runs the viewer tests (rendering, picking, modal animation). The GUI end-to-end scripts in `tests/webgl` are run by hand against a real `openmodel-webui` (see [`tests/webgl/README.md`](tests/webgl/README.md)); they need the WebUI SDK, Gmsh, TetGen and Open CASCADE.

What is **not** verified:

- The generated CalculiX input has never been run through a real `ccx`; the tests use a stand-in (`tests/support/fake_ccx.py`) and check the plumbing, not the physics.
- The code_aster export has never been run through a real code_aster (stand-in `tests/support/fake_aster.py`); MED files are not supported. See [`docs/CODE_ASTER.md`](docs/CODE_ASTER.md).
- The solver runner is POSIX only (Linux, macOS); on Windows it returns an error. The CI workflow also builds the core on Windows (MSVC) and skips the solver tests there; locally only Linux has been built and tested.
- Gmsh, Open CASCADE, TetGen, Netgen and MFEM backends were not compiled in the latest test environment; the Assimp backend is built by a separate CI job on Linux and Windows.
- The fatigue module is a library: it has no CLI command or workbench panel. It is covered by unit tests but was not validated against test data of a real component; it applies no safety, size, surface or notch factors, and multiaxial cycle counting is only a contract, not an implementation ([`docs/fatigue/methods.html`](docs/fatigue/methods.html)).
- `OpenModelProject` is a library API only; neither the CLI nor the workbench reads or writes `.omproj` files yet.
- The workbench CAD face groups need the Open CASCADE backend, and Gmsh for the volume mesh; only face groups are editable (named CAD groups of other dimensions are shown read-only).
- Nodal forces are shared equally among the selected nodes (not consistent for quadratic elements; use `pressure`); one material and one step per analysis; no contact or nonlinear materials.
- Groups cannot yet be deleted or renamed; picking performance with very large selections and touch input are untested.
- The code base is C++17; migration to C++23 (`std::expected`) is not done.

## License

OpenModel is released under the [MIT License](LICENSE). Optional backends (Gmsh, Open CASCADE, Assimp, TetGen, Netgen, MFEM, WebUI) and
CalculiX are separate projects with their own licenses, which apply when you build against or run them; none of them is bundled in this repository.
