# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- **code_aster support** ([`docs/CODE_ASTER.md`](docs/CODE_ASTER.md)): `writeCodeAsterCase` writes `.mail`, `.comm` and `.export` from a mesh and an analysis script (static and frequency);
  `CodeAsterMeshImporter` reads `.mail` meshes; `CodeAsterTableImporter` reads the `.tab` result tables (displacements, stresses, strains, modes); launcher helpers.
- CLI: `export-aster`, `solve-aster`, `export-mail`; `.mail` accepted wherever a mesh is read; `.tab` accepted as result file.
- Desktop workbench: solver selector (CalculiX / code_aster), `.mail` mesh loading, `.tab` results.
- Tests `code_aster_test`, `aster_cli_*` with a stand-in solver. **Not verified against a real code_aster.**

## [0.1.0.0] - 2026-11-02

First release under the name **OpenModel**. The project was previously called MeshGen (development phases 8–50, last CMake
version 0.49.0); the version number restarts here. The old phase notes are kept in [`docs/DEVELOPMENT_LOG.md`](docs/DEVELOPMENT_LOG.md).

### Added
- MIT license.

### Changed
- Renamed the project, C++ namespace (`OpenModel`), headers (`include/OpenModel/`), CMake package (`OpenModel`, target `OpenModel::Core`),
  CMake options (`OPENMODEL_*`), executables (`openmodel`, `openmodel-webui`), viewer API (`OpenModelViewer`) and GUI bindings (`openmodel_*`).
- File formats MGMB and MGWV and their extensions are unchanged.

### Included (carried over from MeshGen phases 1–50)
- Mesh data model, topology, validation, physical groups; Abaqus/CalculiX INP import and export; MGMB binary format; glTF/GLB export.
- Optional backends: Gmsh, Open CASCADE, Assimp, TetGen, Netgen, MFEM.
- FEM result loading (CalculiX `.frd`/`.dat`, CSV), WebGL viewer (MGWV) with fields, deformed shape, modes and GPU picking.
- Analysis script, CalculiX input writer and solver launcher; node/element/face groups from the viewer.
