# CAD-to-FEM group mapping — Phase 3.6: Workbench registry view

The Workbench now exposes three distinct group collections in the selection panel.

## Native CAD groups

The read-only **Gruppi nominati importati dal CAD** list is populated from
`CADModel::getGroups()`. Each entry displays:
- the name stored in the CAD source;
- the group kind (`layer` or `named-shape`);
- the topological dimension and number of entities.

Dimensions are shown as vertex (0), edge (1), face (2) and solid (3). This list
does not create or modify CAD metadata. Its purpose is to make imported XDE
layers and named sub-shapes inspectable before meshing.

## User-defined CAD face groups

The existing editable face-group workflow remains separate. It stores selected
B-Rep face IDs in the `.omgroups` sidecar, supports create/update/delete and
invalidates a generated volume mesh when membership changes.

This phase deliberately does not pretend that edge/vertex/solid picking is
available in the viewer. Those editable selections require dimension-aware
picking and a generalized sidecar schema; until that work is implemented, the
native groups are displayed read-only.

## Generic FEM mesh groups

The **Gruppi FEM generici** list is populated from
`MeshData::getMeshGroupRegistry()`. Each entry shows:
- group name;
- topological dimension;
- element and node membership counts;
- source (`CAD`, `Imported`, `UserDefined`).

The registry is independent of legacy mesher physical groups and remains the
backend-independent representation used by the solver writers and analysis
setup. The GUI refreshes this list together with the CAD group collections
after import, mesh loading, mesh generation and CAD face-group operations.

## Tests

The CAD assembly GUI end-to-end test now checks that:
- native named CAD groups appear after import with their topological dimension;
- generic FEM groups appear after Gmsh volume meshing and report their source.

The test runs in the WebUI end-to-end environment, not in the dependency-free
core test suite.


## Phase 4.1 — Prescribed translations

The `fix` command accepts `value=ux,uy,uz` in model length units. Values default to zero, preserving existing scripts. Only the degrees of freedom selected by `dof` are emitted. CalculiX/Abaqus and Code_Aster writers consume the same support values. Rotational DOFs, local coordinate systems and time-dependent amplitudes are outside this increment.


## Phase 4.1 — Prescribed translations and support validation

The analysis script accepts `value=ux,uy,uz` on `fix` commands. Values
are in model length units; omitted values remain zero for compatibility.
Only selected translational degrees of freedom are imposed.

Both CalculiX/Abaqus and Code_Aster exporters reject non-finite active
prescriptions and conflicting values assigned to the same node and degree of
freedom by overlapping support selections. Equal overlapping prescriptions
are accepted. Diagnostics identify both support declarations, the mesh node
ID, and the conflicting degree of freedom.

Rotational DOFs, local coordinate systems, time-dependent amplitudes and GUI
symbol editing remain outside this increment.


### Phase 4.1 — WebUI prescribed support values

The support controls in the Workbench now expose a prescribed translation
field `ux,uy,uz`, in the model's length units. The value is included in
the generated `fix` command both for a viewer selection and for a saved CAD
face group transferred to a FEM node set. The existing default remains
`0,0,0`, so the previous fixed-support workflow is unchanged.


## Phase 4.1 — Workbench condition list

The analysis panel now builds a live list of recognised script commands:
`material`, `fix`, `force`, `pressure`, `gravity`, `step` and
`output`. Each listed row can be edited or removed. Editing replaces only
that source line and requires the command kind to remain unchanged; removal
deletes only the selected line. The script remains the authoritative input
passed to the existing solver actions.

This is an incremental GUI editing layer, not yet a persistent condition
object model. Row identity is derived from the current script line position;
stable condition IDs, project-level persistence, geometric highlighting and
undo/redo are not implemented in this increment.


## Phase 4.2 — Stable analysis condition identifiers

Analysis material, fixed-support, nodal-force and surface-pressure records now
carry a stable string `id`. The script parser accepts an optional `id=`
token on these commands and generates deterministic per-kind IDs when it is
omitted (for example `fix-1`, `force-1`). IDs are unique across all these
condition records in one parsed model; duplicate or malformed IDs are errors.
The Workbench condition list displays an ID when one is present in the script.

The C++ API `serializeAnalysisScript(const AnalysisModel&)` writes a
round-trippable analysis script and retains those IDs. The parser and serializer
cover material, supports, forces, pressures, gravity, step and element-output
settings. Existing scripts remain compatible: their conditions receive
generated IDs when parsed.

This increment does **not** yet add a project-file format or persistent project
save/load. Nor does it yet expose native C++ CRUD-by-ID endpoints in the
Workbench; GUI editing still edits the script text. Those are separate follow-up
tasks and should not be confused with the IDs now available in the analysis
model.
