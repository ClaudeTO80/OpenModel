# OpenModel mesh importers

## Common API

`<OpenModel/MeshImporter.hpp>` declares `OpenModel::MeshImporter`, a polymorphic interface implemented by format-specific readers.

```cpp
OpenModel::AbaqusInpImporter importer;
MeshData mesh;
const OpenModel::MeshImportResult result = importer.read("model.inp", mesh);
if (!result) {
    for (const auto& d : result.diagnostics) {
        // d.code, d.message and d.line are suitable for application logging.
    }
}
```

An import is transactional: parsing occurs into a temporary `MeshData`; `destination` is changed only after parsing and structural validation succeed. A failed import returns a non-success status and one or more diagnostics.

## Implemented subset (v0.41.0)

The Abaqus and CalculiX importers currently support:

- `*NODE` records with ID and X/Y/Z coordinates (Z defaults to zero for 2D records);
- `*ELEMENT` records for the element types mapped in `InpImporterCommon.hpp`;
- `*NSET` and `*ELSET` with explicit IDs;
- `*BOUNDARY` records on degrees of freedom 1 to 6, imported as `MeshData` constraints (`MeshImportOptions::importConstraints`): `node-or-nset, first[, last[, value]]` and the types `ENCASTRE`, `PINNED`, `XSYMM`, `YSYMM`, `ZSYMM`, `XASYMM`, `YASYMM`, `ZASYMM`; node set names are case-insensitive; `OP=NEW` starts afresh; records on degrees of freedom above 6 and blocks of another `TYPE` (velocity, acceleration) or `USER` are skipped with a warning. Steps are not modelled: the boundary conditions of every step are merged in file order;
- single-line `GENERATE` records containing start, end and increment;
- comments beginning with `**` and unrelated keyword sections, which are skipped.

Element sets are represented as `MeshPhysicalGroup` records when `MeshImportOptions::importPhysicalGroups` is true. `MeshData` does not currently have a separate element-set container, so the source set name is preserved as the physical-group name. Node sets retain their node membership.

## Known limitations

- `*INCLUDE` is not expanded.
- Continuation records for element connectivity are not yet combined.
- Set-to-set references, assembly/instance transformations and generated ranges split across multiple data lines are not supported.
- Materials, sections, loads and analysis steps are outside the mesh importer scope; of the boundary conditions only the kinematic `*BOUNDARY` constraints above are read. `AbaqusInpWriter` writes them back as `*BOUNDARY`, but the solver deck of `writeSolverInput()` is made from the analysis model (`fix`), not from the mesh constraints.
- The `.inp` extension is shared by Abaqus and CalculiX; callers should select the intended importer explicitly. `canRead()` checks extension only and does not identify the originating solver.
- Unknown element types are errors. They are not silently coerced to a different topology.

The parser is intentionally shared only for the overlapping keyword syntax. `AbaqusInpImporter` and `CalculixInpImporter` remain distinct classes so dialect-specific behavior can be added without changing the public interface.

## Future formats

The interface is designed for subsequent `AnsysCdbImporter` and `NastranBulkDataImporter` implementations. These require independent parsers: ANSYS CDB block records and Nastran fixed/large/free-field records have different record layouts and continuation rules.
