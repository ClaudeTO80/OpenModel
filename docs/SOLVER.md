# Analysis setup and solver launch

The same script can also drive code_aster: see [`CODE_ASTER.md`](CODE_ASTER.md).

OpenModel meshes carry no material, supports or loads. The **analysis script** adds them; OpenModel writes a complete CalculiX input, runs `ccx` in the background and loads the results (`job.frd` + `job.dat`) into the viewer.

## Script

One command per line, `#` starts a comment.

```
material E=210000 nu=0.3 rho=7.85e-9 [name=STEEL]   # one material for all volume blocks
fix plane:x=0 [dof=1,2,3] [tol=1e-6]                # default: all translations
force plane:x=10 F=0,-100,0                         # TOTAL force, shared equally by the selected nodes
pressure group:Top p=2.5                            # surface group; positive = into the body
gravity g=0,0,-9810                                 # acceleration (needs rho)
step static                                         # or: step frequency modes=6
output element=auto|on|off                          # element S/E in the .dat (auto: up to 200000 elements)
```

Selectors: `all`; `group:NAME` (node set, or the nodes of any physical group, case-insensitive); `plane:x|y|z=VALUE` (nodes within `tol`, default 1e-6 × model diagonal). Units are yours: use a consistent set (mm, N, tonne, s in the examples). Errors carry the script line number; all are collected.

## What is written

The mesh through `AbaqusInpWriter` (**volume elements only**; surface/line elements are not structure), then `*MATERIAL/*ELASTIC/*DENSITY`, one `*SOLID SECTION` per volume block, `*BOUNDARY`, a single `*STEP` with `*STATIC` (+ `*CLOAD`, `*DLOAD` with `Pn` faces and `GRAV`) or `*FREQUENCY`, `*NODE FILE U[,S,E]` and `*EL PRINT S,E`. Pressure faces are found by matching the surface elements of the group with the faces of tetrahedra/hexahedra (numbering P1–P6 / S1–S4 of CalculiX); a surface element that is not a boundary face is an error.

## Use

* GUI: ribbon tab **Analisi** or section *6 · Analisi e solver*. Job argument: a path ending in `.inp` is the input file, anything else is a folder containing `job.inp`; empty = `./openmodel_job/job.inp`. Solver empty = `ccx` in `PATH`. The log is shown live; *Annulla* sends SIGTERM to the process group, then SIGKILL after 3 s. On success the results are loaded automatically.
* CLI: `openmodel write-input mesh.inp analysis.txt job.inp`; `openmodel solve mesh.inp analysis.txt workdir [--solver /path/ccx] [--web out.mgwv]`.
* Library: `parseAnalysisScript`, `writeSolverInput`, `ProcessRunner` (`include/OpenModel/AnalysisSetup.hpp`, `ProcessRunner.hpp`).

## Limits (not verified)

* **The generated `.inp` has not been run through a real CalculiX** (not available here). Tests use `tests/support/fake_ccx.py`, which checks that the input has the expected sections and copies fixture results: they verify the plumbing, not the physics.
* Forces are shared equally among nodes. For quadratic elements this is not the consistent load (a warning is issued); prefer `pressure`.
* Pressure supports tetrahedra/hexahedra (linear and quadratic); no pyramids/wedges, no shell/beam elements, no contact, no nonlinear materials, one material, one step.
* ProcessRunner is POSIX only; on Windows it returns an error (untested/unimplemented).
* Child process receives no sandbox: it runs with the user's rights.

## Picking and groups (phase 50)

Selecting in the viewer replaces typing selectors. In the GUI (section *7 · Selection and groups*, or the *Analysis* ribbon tab) choose what to pick — **nodes, elements or faces** — then:

* click selects, Ctrl+click adds/removes, Alt+drag (or the *Riquadro* option) selects a rectangle; Shift+drag still pans;
* faces: the *angle* option extends a click to the connected faces whose normals deviate less than that angle (0 = one face; 1° = a planar patch);
* only what is **visible** can be picked (hidden blocks, back faces, parts cut by the section plane are not). With the section plane on, interior faces become pickable; interior elements are only reachable through a visible face.

A selection becomes a group (`openmodel_create_group`): a **node set** (picked nodes, or the nodes of picked elements/faces, including mid-side nodes of quadratic elements found geometrically), an **element group** (physical group, dimension 3) or a **face group** (physical group, dimension 2, backed by new Triangle3/Quadrilateral4 surface elements with outward orientation, named `NAME_tri`/`NAME_quad`). The buttons *Fix*, *Force*, *Pressure* create the right kind of group with an automatic name (FIX1, FORCE1, PRESS1) and append `fix group:…`, `force group:…` or `pressure group:…` to the script. *Show* selects a group again in the viewer.

Group names: 1–64 characters from letters, digits, `_`, `-`, `.`; unique, case-insensitively, among node sets, groups and blocks.

MGWV: three optional chunks (`vertexNodeId`, `triElementId`, `triFace`) carry the ids; see `MGWV_FORMAT.md`. Library: `include/OpenModel/MeshSelection.hpp`.

Limits (not verified or not done): groups cannot be deleted or renamed (MeshData has no removal); a face group adds surface elements to the mesh (they appear in MGMB/INP exports); picking sends ids as text, performance with selections of hundreds of thousands of faces is untested; picking nodes of hidden blocks near visible surfaces is not filtered per block; touch input is untested; mid-side node detection assumes straight edges (within a quarter edge length).
