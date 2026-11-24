# Analysis setup and solver launch

The same script can also drive code_aster: see [`CODE_ASTER.md`](CODE_ASTER.md).

OpenModel meshes carry no material, supports or loads. The **analysis script** adds them; OpenModel writes a complete CalculiX input, runs `ccx` in the background and loads the results (`job.frd` + `job.dat`) into the viewer.

## Script

One command per line, `#` starts a comment.

```
material E=210000 nu=0.3 rho=7.85e-9 [name=STEEL]   # default material: every volume block without an assign
material id=ALU E=70000 nu=0.33 rho=2.7e-9 [name=ALU] # further materials (after the first one), each with its id=
assign Housing material=ALU                         # material of one volume part (a mesh block); names are case-sensitive
fix plane:x=0 [dof=1,2,3] [tol=1e-6]                # default: all translations
force plane:x=10 F=0,-100,0 [amp=A1]               # TOTAL force, shared equally by the selected nodes
pressure group:Top p=2.5 [amp=A1]                   # surface group; positive = into the body
distributed group:Top F=0,0,-40 [amp=A1]           # total force of a surface group, shared by its faces in proportion to area
gravity g=0,0,-9810                                 # acceleration (needs rho)
amplitude name=A1 t=0,0,1,1,2,0.5                   # time-dependent factor: (time, factor) pairs, increasing times
table name=T1 t=0,0,1,1,2,0.5                       # named table of (time, factor) pairs; defined before use
amplitude name=A2 t=T1                               # amplitude given by table T1 (a copy of its points)
job SETTLE                                        # a job is one solver session; its steps run in order
job IMPACT amp=A1:T2                              # this job uses table T2 for amplitude A1 (the loads are unchanged)
step static [loads=F1,gravity] [reset]           # loads=: active loads (ids; default all); reset: zero the loads of the earlier steps
step frequency modes=6                           # or: step dynamic dt=0.01 period=1 [dtmin=1e-5] [dtmax=0.1]   # transient, implicit; needs rho and supports
output nodal=on|off                            # nodal results in the .frd (*NODE FILE); on by default
output element=auto|on|off                          # element S/E in the .dat (auto: up to 200000 elements)
# The two requests are independent: nodal=off writes no *NODE FILE, element=off writes no *EL PRINT.
```

Jobs and steps. A model holds the loads once; a `job NAME` line starts a job (one solver session, one `NAME.inp` deck and one
`NAME.frd` result set), and the `step` lines after it belong to that job. Without `job` the steps form the job `job`. A step
lists its active loads with `loads=` (ids: `id=` on the load line, or the generated `force-N`, `pressure-N`; gravity is `gravity`).

Tables. A `table name=T t=...` line holds a (time, factor) curve that is reused by name. An `amplitude` takes its points either as a list (`t=0,0,1,1,...`) or from a table (`t=T`). A job line can give another table to an amplitude with `amp=A:T`, comma-separated for several amplitudes: each job writes its own `*AMPLITUDE` points, the loads and the model are unchanged. Tables and amplitudes must be defined before they are used. Only the resolved points reach the deck. A table can only hold numbers: it is a list, not a function, so a curve such as `A*sin(t)` is sampled into a table before it is written. In the GUI (*Loads and steps* → *Time dependence* → *Tables*) a table can be created, edited and removed: the dialog checks the name and the rows (at least two numeric rows, strictly increasing times, no duplicate name) and shows a preview, a table used by an amplitude or a job cannot be removed or renamed, and an amplitude can take a table by name. A job override (`amp=A:T`) is text only in the GUI for now. Test: `tests/webgl/gui_tables_smoke.js`.
A load that is not active in a step keeps its last value in the solver, unless the step says `reset`: then the loads of the
earlier steps of the same job that are not active there are set to zero. Supports (`fix`) hold in every step.
The CalculiX writer writes every step of a job in one deck. `openmodel solve` runs one solver session per job, in script
order. code_aster writes one job with one step for now (a job with several steps is refused).

`amp=NAME` multiplies the load by the factor of the amplitude at the time of the step (CalculiX `AMPLITUDE=`). Loads without `amp` are constant: in a static step they are ramped over the step, in a dynamic step they act at full value from the start. A dynamic step without `dtmin`/`dtmax` uses min(dt, 1e-5 × period) and 1e30 (the CalculiX defaults).

Selectors: `all`; `group:NAME` (node set, or the nodes of any physical group, case-insensitive); `plane:x|y|z=VALUE` (nodes within `tol`, default 1e-6 × model diagonal). Units are yours: use a consistent set (mm, N, tonne, s in the examples). Errors carry the script line number; all are collected.

## What is written

The mesh through `AbaqusInpWriter` (**volume elements only**; surface/line elements are not structure), then `*MATERIAL/*ELASTIC/*DENSITY`, one `*SOLID SECTION` per volume block, `*BOUNDARY`, a single `*STEP` with `*STATIC` (+ `*CLOAD`, `*DLOAD` with `Pn` faces and `GRAV`) or `*FREQUENCY`, `*NODE FILE U[,S,E]` and `*EL PRINT S,E`. Pressure faces are found by matching the surface elements of the group with the faces of tetrahedra/hexahedra (numbering P1–P6 / S1–S4 of CalculiX); a surface element that is not a boundary face is an error.

### Constraints stored in the mesh

The node constraints of `MeshData` (set in the GUI with *Mesh constraints*, or read from `*BOUNDARY`) are added to the script's supports when the input is written (`withMeshConstraints()`): one `fix` per set of blocked translations and prescribed values, id `mesh-constraint-N`. A conflict with a script support is refused with the usual message. Rotations (rx, ry, rz) are not written, because the solid elements have no rotational degrees of freedom: the warning `CONSTRAINT_ROTATION_IGNORED` is reported.

### Rigid links (holes)

A `RigidLink` (`AnalysisModel::rigidLinks`) couples a set of mesh nodes to a reference point with CalculiX `*RIGID BODY`. The reference node is not a mesh node: the writer creates it at the given position with the next free node id after the mesh, and writes `*NSET, NSET=MG_RBS<n>` and `*RIGID BODY, NSET=MG_RBS<n>, REF NODE=<id>` after the `*BOUNDARY` block. All six degrees of freedom of the slave nodes follow the reference node. A node may belong to one link only (refused before writing otherwise). `holeRigidLinks()` makes one link per hole from the node sets of the hole edges: each hole is fitted with a circle (`fitHoleCircle()`, centre = centroid, axis = smallest-spread direction), and the reference is the point given by the user outside the part. Warnings: `RBE2_OFF_AXIS` (reference more than 5 % of the radius off the hole axis), `RBE2_NOT_CIRCULAR` (nodes more than 5 % off a circle).

Not yet: the script syntax for rigid links, the grouping of selected faces into holes, the GUI. The deck syntax has not been run through CalculiX (not installed here).

## Use

* GUI: ribbon tab **Analisi** or section *6 · Analisi e solver*. Job argument: a path ending in `.inp` is the input file, anything else is a folder containing `job.inp`; empty = `./openmodel_job/job.inp`. Solver empty = `ccx` in `PATH`. The log is shown live; *Annulla* sends SIGTERM to the process group, then SIGKILL after 3 s. On success the results are loaded automatically.
* CLI: `openmodel write-input mesh.inp analysis.txt job.inp`; `openmodel solve mesh.inp analysis.txt workdir [--solver /path/ccx] [--web out.mgwv]`.
* Library: `parseAnalysisScript`, `writeSolverInput`, `ProcessRunner` (`include/OpenModel/AnalysisSetup.hpp`, `ProcessRunner.hpp`).

## Limits (not verified)

* **The generated `.inp` has not been run through a real CalculiX** (not available here). Tests use `tests/support/fake_ccx.py`, which checks that the input has the expected sections and copies fixture results: they verify the plumbing, not the physics.
* Forces are shared equally among nodes. For quadratic elements this is not the consistent load (a warning is issued); prefer `pressure`.
* Distributed loads: the total force of a face group is shared by the faces in proportion to their area (a uniform traction, `total / area`), and each face gives its share to its corner nodes as `*CLOAD`. The total is exact; the split among nodes is the consistent one for linear faces, and a quadratic mesh receives the total on its corner nodes (a warning is issued). CalculiX only: code_aster refuses them. The solver's `*DLOAD` takes only the normal pressure of a face, so the vector traction is written as nodal forces. Units are the model's own.
* Pressure supports tetrahedra/hexahedra (linear and quadratic); no pyramids/wedges, no shell/beam elements, no contact, no nonlinear materials. Per-part materials (`assign`) are written by CalculiX only; code_aster takes one material.
* Loads not written yet: body forces per element (`BX/BY/BZ`), centrifugal loads (`CENTRIF`: the manual does not state unambiguously the meaning of its first value), distributed traction (tangential), nodal moments (solid elements have no rotational degrees of freedom), thermal loads (out of scope). Each one needs its syntax checked against the solver manual before it is written.
* Dynamic steps and amplitudes are not written to the code_aster case: `writeCodeAsterCase` refuses them with a message.
* The results reader loads every increment of a dynamic run as its own step. The web view binds the displacement of those steps as time-history frames (at most 100, sampled evenly, first and last kept): the viewer's "Increment" slider steps through them and "Play" runs them at five per second, while the scalar fields stay those of the selected step. The default frame is the selected step (the last one unless `--time T` or `t=T` chooses another). Files with a single increment, and the modal results, have no frames. A static run with several load increments has frames too, since CalculiX labels them as times. The frames are not written to the code_aster case.
* ProcessRunner uses `TK4CPP::Process`: a process group on POSIX and a Job Object on Windows. The Windows path has not been built or tested yet.
* Child process receives no sandbox: it runs with the user's rights.

## Picking and groups (phase 50)

Selecting in the viewer replaces typing selectors. In the GUI (section *7 · Selection and groups*, or the *Analysis* ribbon tab) choose what to pick — **nodes, elements or faces** — then:

* click selects, Ctrl+click adds/removes, Alt+drag (or the *Riquadro* option) selects a rectangle; Shift+drag still pans;
* left-button rotation about one axis of the view: Ctrl+Alt+drag rolls (about the view axis), Ctrl+Shift+drag pitches (about the horizontal axis), Shift+Alt+drag yaws (about the vertical axis); plain drag rotates about both screen axes. These drags never select;
* faces: the *angle* option extends a click to the connected faces whose normals deviate less than that angle (0 = one face; 1° = a planar patch);
* only what is **visible** can be picked (hidden blocks, back faces, parts cut by the section plane are not). With the section plane on, interior faces become pickable; interior elements are only reachable through a visible face.

A selection becomes a group (`openmodel_create_group`): a **node set** (picked nodes, or the nodes of picked elements/faces, including mid-side nodes of quadratic elements found geometrically), an **element group** (physical group, dimension 3) or a **face group** (physical group, dimension 2, backed by new Triangle3/Quadrilateral4 surface elements with outward orientation, named `NAME_tri`/`NAME_quad`). The buttons *Fix*, *Force*, *Pressure* create the right kind of group with an automatic name (FIX1, FORCE1, PRESS1) and append `fix group:…`, `force group:…` or `pressure group:…` to the script. *Show* selects a group again in the viewer.

Group names: 1–64 characters from letters, digits, `_`, `-`, `.`; unique, case-insensitively, among node sets, groups and blocks.

MGWV: three optional chunks (`vertexNodeId`, `triElementId`, `triFace`) carry the ids; see `MGWV_FORMAT.md`. Library: `include/OpenModel/MeshSelection.hpp`.

Limits (not verified or not done): groups cannot be deleted or renamed (MeshData has no removal); a face group adds surface elements to the mesh (they appear in MGOM/INP exports); picking sends ids as text, performance with selections of hundreds of thousands of faces is untested; picking nodes of hidden blocks near visible surfaces is not filtered per block; touch input is untested; mid-side node detection assumes straight edges (within a quarter edge length).

## Several solvers and Elmer

### Solvers and defaults

Settings hold a list of solvers. Each one has an **id** (letters, digits, `_`, `-`), a **kind** (`calculix`, `code_aster` or `elmer`) and an **executable** (empty = the default name of the kind on `PATH`). The built-in ids `calculix`, `code_aster` and `elmer` are always present. The settings also give the **default solver** of each analysis type: `static`, `frequency`, `dynamic`.

Which analysis types a kind can run: CalculiX all three; code_aster static and frequency; Elmer static. A default that a kind cannot run is refused when the settings are saved.

A job chooses its solver in the script:

    job SETTLE solver=elmer
    step static

Without `solver=` the job uses the default of its analysis type (the type of its first step). The Analysis panel's Solver field overrides both for one run. A job naming an unknown solver, or a solver that cannot run its type, is an error.

The settings file holds flat keys: `solver.<id>.kind`, `solver.<id>.path`, `default.<type>`. A file of the earlier version with one `solverPath` is read as the solver `calculix`.

### Elmer case

`openmodel export-elmer <mesh> <analysis.txt> <dir> [--job NAME]` writes, in `<dir>`, the mesh in the ElmerSolver directory format (`mesh.header`, `mesh.nodes`, `mesh.elements`, `mesh.boundary`) and the solver input `<job>.sif`. The solver runs in that folder: `ElmerSolver <job>.sif`. Its output is `<job>.result` and `<job>.vtu`.

| Script | Elmer |
|---|---|
| each volume element block | one Body; its material is the default or the one of `assign BLOCK material=ID` |
| `material E= nu= rho=` | Youngs Modulus, Poisson ratio, Density |
| `fix plane:... dof=...` | one Boundary Condition per fix, with Displacement for the fixed directions (free directions not set); each constrained node is a point element |
| `pressure group:NAME p=` | Normal Force on the faces of the surface group (linear triangles and quadrilaterals; each face must belong to one volume element) |
| `gravity g=` | Stress Bodyforce = density times g, per body (density needed) |

Refused with a message for Elmer: nodal `force`, `distributed` loads, `rbe2` rigid links, time-dependent loads, frequency and dynamic steps, and jobs with several steps. Core: `include/OpenModel/ElmerSolver.hpp`; tests in `tests/solvers_test.cpp`.

### Not verified

ElmerSolver was not installed where this was written, so nothing here has been solved. The tests check the files written and the messages, not the solution. The following come from the Elmer manual as remembered and must be checked with a test case before relying on results:

- the point elements (type 101) used for the fixes, and the Displacement conditions on them;
- the sign convention of Normal Force (the pressure direction);
- the node order of each element type in the mesh files (the OpenModel order is used as it is; the export says so);
- the keyword names `Stress Bodyforce`, `Youngs Modulus`, `Poisson ratio`, the solver procedure `StressSolve`, and the header of `mesh.header`.

The results are not read back into OpenModel: the Elmer output is not in the viewer. The GUI and the `solve` command report the end of an Elmer run and its output file. In the command line, `solve` also writes the CalculiX input (`job.inp`) of every job, Elmer ones included, because the shared preparation writes it; that file is not run for an Elmer job.

### Settings in the WebUI

The Settings dialog lists the solvers (id, kind, executable with Browse, Remove) and the defaults of the three analysis types. The defaults offer only the solvers that can run their type. Saving sends the whole list; the built-in solvers cannot be removed, only changed.

