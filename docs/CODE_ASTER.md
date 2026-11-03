# code_aster

OpenModel can prepare a [code_aster](https://www.code-aster.org) case from the same mesh and the same analysis script used for CalculiX
(see [`SOLVER.md`](SOLVER.md)), run it, and load the results back into the viewer. It can also read code_aster meshes in the
text format `.mail`.

> **Status: not verified against a real code_aster.** code_aster was not available where this was written. The tests check the
> files OpenModel writes and reads, and run the whole chain with a stand-in (`tests/support/fake_aster.py`) that produces made-up
> tables. That the generated command file is accepted and gives correct results with your installation **has not been tested**;
> see [Limits](#limits-not-verified). Try a small model first and read the `.mess` file.

## Workflow

```bash
# 1. write the case (job.mail, job.comm, job.export)
openmodel export-aster beam.inp analysis.txt job/ [--job NAME] [--solver MULT_FRONT|MUMPS|PETSC|LDLT]
                                                   [--cpus N] [--memory MB] [--time SECONDS] [--version V]
# 2. run it yourself ...
cd job && run_aster job.export            # or: as_run job.export (older installations)
# 3. ... and look at the results
openmodel export-web beam.inp result.mgwv --results job/job_disp.tab --results job/job_stress.tab --results job/job_strain.tab

# or 1-3 in one go
openmodel solve-aster beam.inp analysis.txt job/ [--launcher /path/run_aster] --web job/result.mgwv

# mesh only, and the other direction
openmodel export-mail beam.inp beam.mail
openmodel export-web beam.mail beam.mgwv
```

The launcher is `--launcher`, else the environment variable `OPENMODEL_CODE_ASTER`, else `run_aster`, then `as_run`, in `PATH`. It
is started in the job folder with the single argument `<job>.export`.

In the desktop workbench (section *6 · Analisi e solver*) choose **code_aster** in the *Solutore* list; *Scrivi input* writes the
case, *Avvia solutore* runs it and loads the tables when it ends. *Apri mesh* accepts `.mail`, *Carica risultati* accepts `.tab`.

Library: `writeCodeAsterCase`, `writeCodeAsterMesh`, `findCodeAsterExecutable`, `codeAsterLaunchCommand`, `codeAsterResultFiles`,
`CodeAsterMeshImporter`, `CodeAsterTableImporter` (`include/OpenModel/CodeAster.hpp`).

## What is written

| File | Content |
|---|---|
| `job.mail` | mesh in the code_aster text format "ASTER": `COOR_3D`, one block per element type, `GROUP_NO`, `GROUP_MA`, `FIN` |
| `job.comm` | Python command file |
| `job.export` | `P` lines (`ncpus`, `memory_limit`, `time_limit`, optional `version`) and `F` lines for the command file (unit 1), mesh (unit 20), message file (unit 6) and the result tables (units 80–82, or 80–81) with **absolute paths** |

* Node `N<id>` and element `M<id>` keep the external ids of the mesh; ids must have at most 7 digits (code_aster names have 8 characters).
* Physical groups become `GROUP_MA`, node sets `GROUP_NO`; names are upper-cased and reduced to `A-Z 0-9 _`, 24 characters at most, made unique. The analysis adds `OM_VOLUME` (all volume elements), `OM_FIX<i>`, `OM_LOAD<i>`.
* The whole mesh is written (surface elements are needed for `PRES_REP`), the model (`AFFE_MODELE`, `3D`) and the material are assigned to `OM_VOLUME` only.
* Element types: POI1, SEG2, SEG3, TRIA3, TRIA6, QUAD4, QUAD8, QUAD9, TETRA4, TETRA10, HEXA8, HEXA20, PENTA6, PENTA15, PYRAM5. Node order is assumed to be the Abaqus one; for 15-node wedges the mid-side nodes are reordered. 27-node hexahedra are refused.

Script command → code_aster:

| Script | Command file |
|---|---|
| `material E= nu= rho=` | `DEFI_MATERIAU(ELAS=_F(E, NU, RHO))`, `AFFE_MATERIAU` |
| `fix SEL [dof=]` | `AFFE_CHAR_MECA(DDL_IMPO=_F(GROUP_NO='OM_FIX<i>', DX=0., ...))` |
| `force SEL F=` | `FORCE_NODALE` on `OM_LOAD<i>`, total force divided by the number of nodes |
| `pressure group:G p=` | `PRES_REP(GROUP_MA=G, PRES=p)`; positive = into the body, as in the CalculiX path |
| `gravity g=` | `PESANTEUR(GRAVITE=|g|, DIRECTION=g/|g|)` |
| `step static` | `MECA_STATIQUE` (solver from `--solver`), `CALC_CHAMP(SIGM_ELNO, SIGM_NOEU, EPSI_ELNO, EPSI_NOEU)` |
| `step frequency modes=N` | `CALC_MATR_ELEM` (`RIGI_MECA`, `MASS_MECA`), `NUME_DDL`, `ASSE_MATRICE`, `CALC_MODES(OPTION='PLUS_PETITE', NMAX_FREQ=N)` |

Selections are restricted to nodes that belong to a volume element (a warning says how many were left out). `output element=` has no
effect: only nodal results are produced.

## Result tables

The command file writes with `CREA_TABLE(RESU=...)` + `IMPR_TABLE(FORMAT='TABLEAU', SEPARATEUR=',', FORMAT_R='1PE16.9')`:

| File | Content | Becomes |
|---|---|---|
| `<job>_disp.tab` | `DEPL` | `DISP` (D1 D2 D3) |
| `<job>_stress.tab` | `SIGM_NOEU` | `STRESS` (SXX SYY SZZ SXY SYZ SZX; code_aster's `SIXZ` is `SZX`) |
| `<job>_strain.tab` | `EPSI_NOEU` | `STRAIN` (EXX EYY EZZ EXY EYZ EZX, tensor shear components) |
| `<job>_modes_disp.tab` | `DEPL` of every mode | one frequency step per `NUME_ORDRE` |
| `<job>_modes.tab` | `NUME_ORDRE,FREQ` (written by a Python block with `getAccessParameters()`) | frequencies and eigenvalue table |

`CodeAsterTableImporter` (extension `.tab`) accepts comma, semicolon, tab or blank separated tables, with or without a leading `#`,
and the columns `NOEUD`/`NODE`, `NUME_ORDRE`, `NUME_MODE`, `INST`, `FREQ`; other numeric columns become scalar fields under their own
name, so tables you write yourself with `IMPR_TABLE` can be loaded too. A file named `*_modes*.tab` is read as modal. Several tables are
merged exactly like the CalculiX `.frd` + `.dat` (`--results a.tab --results b.tab`).

## Mesh import

`CodeAsterMeshImporter` reads `.mail` files: `COOR_3D`/`COOR_2D`, the element blocks above, `GROUP_NO NOM = name`, `GROUP_MA NOM = name`
(a group mixing dimensions is split into `NAME_1D`, `NAME_2D`, `NAME_3D`), Fortran exponents (`1.0D+02`), `%` comments, element lines
continued on the next line. A name like `N12` / `M7` gives the id 12 / 7; if the names have no unique number, entities are numbered from 1 in file order
(warning `ASTER_IDS_RENUMBERED`). Unknown blocks are skipped with a warning. Writing then reading a mesh gives the same nodes, ids,
connectivity and groups (tested for every supported type).

## Limits (not verified)

* **No real code_aster run.** Keywords and syntax were written from the code_aster documentation as remembered; not checked against any version. The points most likely to need an adjustment for your installation:
  * the `CREA_TABLE(RESU=_F(..., TOUT='OUI', TOUT_CMP='OUI'))` call and the column names of the table it produces (the reader is tolerant, but `NOEUD` or an equivalent must be there);
  * `modes.getAccessParameters()` for the frequencies (inside `try`, a failure only prints a message; mode shapes then load without frequencies);
  * writing `fort.81` from Python so that unit 81 is returned by the `F libr` line;
  * `DEBUT(LANG='EN')`, `CALC_MODES` arguments, and the `P version` line.
  Edit `job.comm` freely: the reader only needs the `.tab` files described above.
* **MED files are not supported** (neither reading nor writing; MED needs the HDF5/MED libraries). Convert a `.med` mesh to `.mail`/`.inp` with another tool, or use `.mail` directly.
* The ASTER text format is the old one: the reader handles `GROUP_NO NOM = x` but not the historical layout with the name on its own line; `SOUS_STRUC` and discrete elements are not supported.
* Element node order is assumed to follow Abaqus; tetrahedra with negative signed volume produce a warning (code_aster may refuse them). 15-node wedge reordering is from memory of the code_aster/MED numbering.
* Element-based results (`SIEF_ELGA`, `SIGM_ELEM`) are not exported; only nodal fields (the viewer derives von Mises, principal stresses etc. from them).
* Nodal results from `SIGM_NOEU` are averaged across elements by code_aster; at material interfaces this differs from CalculiX's output.
* Forces are shared equally among nodes (not consistent for quadratic elements; use `pressure`), one material, one step, linear elasticity only.
* Mode shapes use code_aster's default normalisation; amplitudes are arbitrary (as for any mode shape).
* Paths in `job.export` are absolute: move the folder and rewrite the case. The launcher runs without sandbox, POSIX only.
