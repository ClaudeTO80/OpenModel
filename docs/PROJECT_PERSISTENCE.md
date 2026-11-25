# OpenModel project persistence

## Status and scope

This document defines the first version of the project persistence contract. It deliberately
separates the project document from the binary payloads already owned by the CAD, mesh,
analysis and result subsystems. It does not introduce a second serialization format for
those payloads.

The project document is the authoritative index of the assets that make up a working
session. It records their roles, relative locations, and enough metadata to reopen the
project without reconstructing CAD-to-mesh group assignments by hand.

## File layout

A project is a directory whose entry point is a UTF-8 document named
`<project-name>.omproj`:

```text
MyProject/
  MyProject.omproj
  cad/
    source.step
  mesh/
    surface.omb
    volume.omb
  analysis/
    model.inp
  results/
    calculix.frd
    calculix.dat
```

The CAD source and solver outputs may be external files. The project document stores
paths relative to its own directory whenever possible. An external absolute path is
allowed only when the user explicitly chooses to link rather than copy an asset.

Mesh payloads are MGOM files (`.omb`, see `MGOM_FORMAT.md`). The former MGMB format
is removed.

Jobs, steps and loads are not stored in the mesh or in MGOM. They are lines of the
analysis script (see `SOLVER.md`), kept in the `analysis.model` asset. Saving that
script as an asset of the project is what persists them.

## Project archive

A project can be packed into one file: `openmodel project-export <project.omproj> <archive.zip>`
writes a standard ZIP archive, and `openmodel project-import <archive.zip> <new-directory>`
extracts it again. The project stays a directory in the workbench; the archive is the
format for moving or saving the whole project at once.

- Every entry is stored without compression (method 0). The script, the mesh and any
  other text payload appear verbatim in the file.
- The archive holds `project.omproj`, with every asset path rewritten to
  `assets/<asset id>/<file name>` and storage `managed`, and the payload of every asset.
  Linked assets are copied in, so the archive is self-contained.
- Asset IDs must not contain `/`, `\`, `:` or control characters, and must not be `.` or `..`.
- No ZIP64, no archive comment, no multi-disk archives, no encryption, no data descriptors.
  Projects that need more than 4 GiB per entry or 65,534 entries are refused.
- Export refuses a manifest that does not validate, a missing asset file, or a file whose size
  or SHA-256 differs from the manifest. Nothing is written in those cases.
- Import checks every CRC-32 and every recorded SHA-256, rejects names that would leave the
  destination directory, and refuses a destination that already holds a manifest or
  files with the same names. On failure the files written so far are removed.
- The archive is written to a temporary sibling file and then renamed over the target.

Test: `tests/project_archive_test.cpp` (round trip, byte-for-byte payloads, linked file,
the script's jobs, steps and loads, and each refusal above).

## Document contract

The document has a required format identifier and schema version. Unknown major versions
must be rejected; newer minor versions may be read only when all required fields are
understood. The writer emits the current canonical version.

Top-level metadata:

- project UUID: stable identity independent of the directory or filename;
- project name and optional description;
- document schema major/minor;
- creation and last-save timestamps in UTC;
- application version that last wrote the document.

Assets are typed records. Initial roles:

| Role | Payload | Required metadata |
|---|---|---|
| `cad.source` | Original CAD file (STEP/IGES/BREP etc.) | source format |
| `cad.face_groups` | CAD face/group mapping snapshot | mapping schema version |
| `mesh.surface` | Surface mesh | mesh format/version |
| `mesh.volume` | Volume FEM mesh | mesh format/version |
| `analysis.model` | Analysis setup script | script schema/version |
| `results.solver` | Solver result file(s) | solver and result format |
| `results.metadata` | Result binding and provenance | mesh identity and source references |

Each asset has a stable ID, role, path, storage mode (managed or linked), optional media
type, optional byte size and optional SHA-256 digest. Asset IDs, not paths, are used for
cross-references. A missing optional payload is represented by an absent asset record,
not by a fabricated empty file.

Asset paths are written as UTF-8 on every platform (`TK4CPP::Utility::pathToUtf8`) and read back
with `pathFromUtf8`. A manifest written on Windows by an earlier build with non-ASCII asset paths
is not read back with the same names.

## CAD and mesh identity

The CAD source file is not a substitute for the face-group mapping snapshot. Topological
face IDs may be importer-local and can change when the source is re-imported or the CAD
backend changes. The mapping snapshot must therefore retain:

- CAD source digest and importer/backend identity;
- group name, kind and dimension;
- source entity IDs;
- the mapping between CAD entities and mesh group memberships;
- the geometric tolerance and mapping algorithm/version used.

The mesh payload remains the authoritative record of node/element IDs and group
memberships after meshing. A reopened project must not silently regenerate these mappings.

## FEM analysis and results

The analysis setup is stored as its existing round-trippable script, including stable
condition IDs. The mesh used by an analysis is referenced by asset ID. Solver result
records reference both the mesh asset and the analysis asset that produced them, plus
solver name/version and the original result filenames.

Result files remain solver-native. OpenModel's existing result importers reconstruct
`FemResults`; the project document stores provenance and references, not a lossy
duplicate of the result arrays.

## Path and integrity rules

- Relative paths are resolved against the directory containing the `.omproj` file.
- Paths are normalized and must not escape the project root for managed assets.
- Linked assets may be outside the project root and are never moved by project save.
- A missing asset is reported with its asset ID, role and resolved path; loading the
  remaining project metadata is still possible in recovery mode.
- A digest mismatch is an integrity error, not a reason to silently overwrite the asset.

## Save transaction

Saving is a two-phase operation:

1. Write changed managed payloads to temporary sibling files; flush and close them.
2. Verify payload sizes/digests, then atomically rename payloads into place.
3. Write the new project document to a temporary sibling file and flush it.
4. Atomically replace the previous document last.

The project document is committed last: it must never point to a payload that has not
been installed. On platforms where replacing an existing file is not atomic, use a
backup/rollback sequence and report that weaker guarantee explicitly.

Current state: `OpenModelProject::save()` writes the manifest only; it does not write
payloads. To save everything at once, use `exportProjectArchive()` (see *Project archive*).

## Compatibility and evolution

The first implementation should keep C++17 and the existing dependency-free core.
The project document parser must reject malformed, duplicate asset IDs, unsupported
major versions, invalid roles and managed paths escaping the project root. Schema
upgrades must be explicit and tested with fixture documents from every supported version.

The initial implementation is intentionally incremental: first provide a robust,
versioned manifest and asset/path validation; then add snapshots for CAD face mappings;
finally connect the WebUI open/save workflow and managed-payload transaction.
