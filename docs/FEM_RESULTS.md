# Loading FEM results

OpenModel can attach the **nodal results of a solver run** (displacements, stresses, temperatures, ...) to a mesh and show them in the WebGL viewer as colour fields and as a deformed shape.

```
solver result file ──► ResultImporter ──► FemResults ──► bindResults(mesh) ──► WebViewOptions ──► MGWV ──► viewer
   (.frd, .csv)         FemResults.hpp                    ResultBinding.hpp      (fields, displacement)
```

## Supported inputs

| Format | Extension | Notes |
|---|---|---|
| CalculiX result file (ASCII) | `.frd` | Nodal result blocks (`100C`, `-4`, `-5`, `-1`, `-2`, `-3`); short and long node-number formats; several steps/increments; node coordinates (`2C`) are read to check that the results belong to the mesh. |
| Delimited table, one row per node | `.csv`, `.tsv`, `.txt` | Delimiter (`,` `;` tab, blank) detected from the header; decimal comma accepted with `;`/tab; UTF-8 BOM accepted; one step. |

**Not supported** (reported with a diagnostic, never converted silently): binary `.frd`; element results and element-at-node results (`IRTYPE` ≠ 1); integration-point data; material-dependent nodal data; Abaqus `.odb`/`.fil`, Nastran `.op2`, Exodus II, VTK/VTU, ANSYS result files.

## Table layout (`.csv`)

```
node,Temp,U_x,U_y,U_z,S_xx,S_yy,S_zz,S_xy,S_yz,S_zx
1,20.0,0.0,0.0,0.0,0,0,0,0,0,0
2,21.5,0.001,-0.002,0.0,10.5,0,0,0,0,0
```

* One column must be named `node`, `node_id`, `nodeid`, `nid` or `id` (case-insensitive) and holds the **external node id** (the id in the mesh file, not a position).
* `<name>_x/_y/_z` become one 3-component field `<name>`; `<name>_xx/_yy/_zz/_xy/_yz/_zx` (or `_xz`) one 6-component tensor. Every other column is a scalar field.
* **Per-element tables:** name the id column `element`, `element_id`, `elementid`, `elem`, `elem_id` or `eid` instead of a node column. The values are then per element (for example the stress at the element centroid). A table with both a node and an element column is an error: use one table per location. The element id is the one of the mesh (`MeshData` element ids).
* An empty cell or `nan` is a missing value. A duplicate id, a row with the wrong number of cells, a text cell in a value column or a non-integer node id is an error that reports the line number.

## CalculiX text output (`.dat`)

`CalculixDatImporter` reads what `*NODE PRINT` and `*EL PRINT` write. Format as documented by the CalculiX manual and checked against the public parsers (pyformex `ccxdat`) and a published eigenvalue table: header `name (components) for set SET and time T`, a blank line, one row per record, a blank line.

* **Nodal blocks** (`U` → `DISP`, `NT` → `NDTEMP`, `RF`...): rows `node v1 v2 ...`; component labels are the header ones (`VX VY VZ`).
* **Element blocks at integration points** (`S` → `STRESS`, `E` → `STRAIN`, `ME` → `MESTRAIN`, `ENER`...): rows `element point v1 ...`. They are reduced to **one value per element by the plain arithmetic mean over its integration points**, with a warning (`DAT_IP_AVERAGED`). The mean is exact for the one-point tetrahedron and for 2×2×2 hexahedra; for 27-point elements it is approximate because the Gauss weights are not equal. The stress order of this file is `SXX SYY SZZ SXY SXZ SYZ` (note: not the `.frd` order); component names, not positions, drive the invariants, so this is handled.
* **Eigenvalue table** (`*FREQUENCY`): `mode, eigenvalue, rad/time, cycles/time, imaginary part` into `FemResults::eigenmodes` (a negative eigenvalue is reported). The file has no mode shapes, so on its own it cannot be painted: `results-info` lists the frequencies and loading it alone is refused with that explanation.
* Blocks with the same time share a step (label `time T`); the same quantity printed for several sets of one increment is one field (union of the records; the first value wins on overlap). `TOTALS` output (no record ids) is skipped with a warning.
* **Merging** with `mergeResults` / `readResultsMerged`: `openmodel export-web ... --results run.frd --results run.dat`, or `run.frd; run.dat` in the GUI field. Steps are matched by time (relative difference ≤ 1e-5, because `.frd` prints 6 and `.dat` 7 digits); a step without partner is added with a warning; a quantity already present is not replaced (warning). The result gives the nodal fields of the `.frd` and the per-element stresses of the `.dat` on the same step, which the viewer shows together.

## Binding to a mesh

`bindResults` matches records to mesh nodes **by node id**. It then

1. **checks the model**: when the file carries node coordinates (FRD does), those of the nodes the file and the mesh share are compared with the mesh. More than 1 % of mismatches is an error ("results were computed on a different mesh"), fewer is a warning; no shared node at all is an error. The tolerance is `2e-5 × max(bounding-box diagonal, largest |coordinate|)`, which covers the six significant digits of CalculiX's `E12.5` output. `--no-coordinate-check` disables it. CSV files have no coordinates, so only the node ids can be checked.
2. produces scalar fields: each component as `FIELD.COMP`, a single-component field under its plain name, and (unless disabled) derived fields **first**:
   * `NAME magnitude` for fields with 3 (or 4, 5, 7, ...) components, using the first three;
   * for 6-component **stress** tensors (components `SXX SYY SZZ SXY SYZ SZX` as CalculiX writes them, or `XX YY ZZ XY YZ ZX` when the field name says stress: `stress`, `s`, `sigma`): `NAME von Mises`, `NAME principal 1/2/3` (eigenvalues, sorted so that 1 is the largest, computed with Jacobi rotations), `NAME Tresca` (principal 1 − principal 3) and `NAME hydrostatic` (mean of the diagonal);
   * for 6-component **strain** tensors (`EXX … EZX`, or bare names with a field name containing `strain`, `e`, `epsilon`): `NAME equivalent` (von Mises equivalent strain, √(2/3 e:e) of the deviator), `NAME principal 1/2/3`, `NAME max shear` (principal 1 − principal 3, an engineering shear strain) and `NAME volumetric` (trace). CalculiX writes **tensor** shear components; for files that hold engineering shear strains (γ = 2ε) use `--engineering-strain` (`BindOptions::engineeringShearStrain`), which halves the shear components before the invariants are computed. Stresses are not affected.
   * Derived fields are computed for nodal and per-element fields alike. They are computed from the values at the nodes (or elements), not at the integration points; a node where one component is missing gets no invariant (filled with the minimum, see below).
3. picks the **displacement**: the first field named `DISP`, `DISPLACEMENT` or `U` with at least three components (override with `--displacement NAME`, disable with `--displacement none`). Missing displacement entries count as zero.
4. selects one **step** (default: the last). The viewer shows one step at a time; to see another one, bind again with `--step N`.
5. binds **per-element fields** by element id: the viewer paints each face with the value of the element that owns it (a boundary face: its volume element; a face with only a shell element: that shell). Values are *flat per element*, not interpolated; interior (cut) faces show the value of one of their two neighbours.

### Vibration modes

CalculiX frequency steps (header type `ICTYPE` 2, value = frequency) are read as **modes**: `FemResults::modeSteps()`, numbered 1, 2, ... in file order (ascending frequency for a normal extraction), labelled `mode k: f = value`. Two blocks with the same frequency stay two modes (a quantity that appears twice in the same increment opens a new step).

* `BindOptions::maxModes` (`--modes N` on the command line): by default (**auto**) a file made only of modes and no explicit `--step` gives a modal view with up to 20 modes; `--modes N` takes the first N modes of any file (an error if it has none); `--modes 0` never does. In a modal view the scalar fields (and any stress/strain of the mode) come from the first mode, or from `--step`.
* Each mode keeps its own displacement; the viewer lists them with their frequency, offers an **Anima** check box (the shape oscillates as `sin(phase)`, one cycle every 2 s) and a field `|u| (modo)` that follows the selected mode. The deformation factor is chosen from the largest displacement over all modes. A mode shape is arbitrary in scale and sign (eigenvectors): the amplitude shown says nothing about real displacements.
* The frequency is displayed in **the unit the solver wrote** (CalculiX: cycles per time unit); OpenModel does not convert it.

Mesh nodes that have no value in a field (a partial table, a `NaN` in the file) are drawn with that field's minimum so they do not stretch the colour range; the number of such nodes is reported as a warning. Records for nodes that are not in the mesh are counted and ignored.

## Command line

```bash
openmodel results-info run.frd                       # steps, fields, components, node counts
openmodel export-web model.inp model.mgwv --results run.frd
openmodel export-web model.inp model.mgwv --results run.frd --step 3 --displacement none --no-derived
openmodel export-web model.inp model.mgwv --results table.csv
```

`export-web` exits with an error and writes nothing when the results do not fit the mesh.

## Library

```cpp
#include <OpenModel/ResultBinding.hpp>

OpenModel::FemResults results;
const auto imported = OpenModel::readResults("run.frd", results);   // picks the importer by extension
if (!imported) { /* imported.diagnostics: code, message, line */ }

WebViewOptions view;
OpenModel::BindOptions bind;            // last step, derived fields, automatic displacement
const auto bound = OpenModel::bindResults(mesh, results, bind, view);
if (!bound.success) { /* bound.error */ }
WebViewWriter::writeFile("model.mgwv", mesh, view);
```

`ResultImporter` is the extension point for further formats: implement `formatName`, `canRead`, `read` and `parse`, fill `FemResults`, and add the importer to `readResults`.

The library follows the project's C++17 baseline; failures are reported through result structs with diagnostics, like `MeshImporter`.

## GUI

Section **5 · Risultati FEM** of `openmodel-webui`: choose the file, optionally a step number (empty = last), *Carica risultati*. The results are attached to the mesh currently shown; if they do not belong to it nothing changes and the reason is shown. Loading, generating or importing another mesh drops the results (they refer to the old node numbering). *Esporta MGWV* keeps the results in the exported file.

## Viewer

A file with results opens coloured by the first field; per-element fields are marked `[elemento]` in the field list. The **Deformata** panel (shown when the file has a displacement) has an on/off switch, a slider and a numeric scale factor. The slider's middle position is the suggested factor, `0.1 × model size / largest displacement`; the slider goes up to ten times that. The section plane works in undeformed coordinates.

## Limits

* Values at nodes (FRD, DAT, CSV) and per element (DAT, CSV). Element blocks in an FRD file (`EL FILE` output) are **skipped with a warning**; for per-element values use `*EL PRINT` (`.dat`, averaged over the integration points as described above) or a per-element table. Extrapolated or smoothed integration-point values are not computed.
* The `.dat` reader was written from the CalculiX manual, public parsers and published output excerpts and tested on files following that layout; no `.dat` written by a real CalculiX run was available to compare. Unusual output options (`FREQUENCY` with `GLOBAL`/`TOTALS`, contact or `*NODE PRINT` of other quantities) are untested; unrecognised text is ignored.
* Stresses and strains are printed by CalculiX in the *global* axes; there is no transformation to a local system. Principal values are invariants and do not depend on the axes.
* The mode numbering and the `ICTYPE` assignment (2 = frequency) follow the format description (cgx documentation, ccx2paraview) and a hand-made fixture; they have not been checked against a modal file written by a real CalculiX run.
* Only corner nodes of higher-order elements are drawn (as before), so mid-side values are not visible.
* Printing precision limits the data: CalculiX writes six significant digits.
* The FRD reader was checked on files written following the format specification and on result and header lines quoted from real CalculiX output; no complete file produced by a CalculiX run was available, so unusual files (very large node numbers beyond `I10`, `NODE FILE` options not seen so far) are untested.
* Performance for the earlier (single step, nodal) features (Release, GCC 13, one thread): a 139 MB `.frd` (270,009 nodes, 2 steps, 4 fields) is parsed in 0.94 s with 98 MB peak memory; `export-web` of a 720,000-tetrahedron `.inp` with those results takes 3.8 s and 560 MB, against 2.7 s and 424 MB without results. Rendering performance on a real GPU has not been measured.
