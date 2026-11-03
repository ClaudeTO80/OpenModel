# WebGL / GUI tests

Both scripts need Node.js with the `playwright` package (`NODE_PATH` must resolve it) and a Chromium.
They are not part of the default CTest run.

## Viewer smoke test (`viewer_smoke.js`)
Loads an `.mgwv` file in `examples/webgl-fem-viewer` and checks, on real pixels, rendering, colour modes,
the section plane and block visibility. Enable it in CTest with `-DOPENMODEL_TEST_WEBGL_VIEWER=ON`
(`openmodel-cli demo-web` generates the input).

## GUI end-to-end test (`gui_e2e.js`)
Drives the real `openmodel-webui` on the real WebUI runtime: import a STEP file, select a rendered surface triangle and verify
that the viewer reports its original OCC `sourceFaceId`; create a `BladeRoot` CAD face group, verify its sidecar persistence
after reimport, delete it, then mesh with Gmsh and TetGen, check what the viewer shows, save/export MGMB/INP/MGWV/GLB,
reload them and use the file browser. The test copies the STEP fixture into the work directory and removes its sidecar afterward.

```bash
export OPENMODEL_TEST_CHROMIUM=/path/to/chromium
mkdir -p bin && cp tests/webgl/webui-chrome-wrapper.sh bin/google-chrome && chmod +x bin/google-chrome
PATH=$PWD/bin:$PATH build/openmodel-webui &        # LD_LIBRARY_PATH must contain libwebui-2.so
sleep 6
node tests/webgl/gui_e2e.js model.step /tmp/work /tmp/shots
```
The GUI needs Gmsh and TetGen backends compiled in (`-DOPENMODEL_BUILD_GMSH_BACKEND=ON -DOPENMODEL_BUILD_TETGEN_BACKEND=ON`)
plus OpenCASCADE for STEP import.

## Assembly / CAD groups test (`gui_assembly_e2e.js`)
Same setup as above, with the XDE assembly fixture instead of a plain STEP:

```bash
build/make_assembly_fixture /tmp/asm.step        # needs the OpenCASCADE backend
node tests/webgl/gui_assembly_e2e.js /tmp/asm.step /tmp/work /tmp/shots
```
Checks the CAD structure report, the component tree and CAD colours in the viewer, hiding by component and by branch,
Gmsh conformal/independent meshes with their physical groups, MGMB round trip of the groups and TetGen's diagnostics.

## Solver results in the viewer (`viewer_results_smoke.js`)
Loads an MGWV written with `--results` (fields, displacement) and a plain one. Checks on real pixels that a results file opens in
scalar mode with a colour gradient, that the deformed shape differs from the undeformed one and grows with the scale factor
(factor 0 reproduces the undeformed picture), that slider and number input stay in sync, field selection, the section of the
deformed shape, and that a file without results hides the deformation panel. Enabled with `-DOPENMODEL_TEST_WEBGL_VIEWER=ON`
(needs Python 3 for the fixture `tests/support/make_beam_results.py`).

## GUI results section (`gui_results_e2e.js`)
Same setup as `gui_e2e.js` but no CAD backend is needed (the mesh is an `.inp`):

```bash
python3 tests/support/make_beam_results.py /tmp/beam
node tests/webgl/gui_results_e2e.js /tmp/beam /tmp/shots
```
Checks: refusal while no mesh is loaded, loading `.frd` and `.csv`, step selection, refusal of a missing step / another mesh /
a missing file (the previous results stay on screen), MGWV export with results, status line, removal, and that loading another
mesh drops the results.

## Modes and element fields (`viewer_modal_smoke.js`)
`node tests/webgl/viewer_modal_smoke.js index.html beam_modal.mgwv beam_elem.mgwv beam.mgwv [shots]` (fixtures from
`tests/support/make_beam_results.py` converted with `openmodel-cli export-web`; registered as `webgl_modal_smoke`). Checks the mode
selector and frequencies, the colour range following the selected mode, the animation phase on real pixels (phase 0 = rest shape,
opposite shapes at pi/2 and 3pi/2), per-element fields (one value per triangle, flat colours) and the stress/strain invariants.
`gui_results_e2e.js` needs a freshly started `openmodel-webui` (it expects no mesh at the beginning).

## Ribbon bar (`gui_ribbon_e2e.js`)
Needs a **freshly started** `openmodel-webui` (as `gui_results_e2e.js`) and the fixture of `tests/support/make_beam_results.py`:

```bash
python3 tests/support/make_beam_results.py /tmp/beam
node tests/webgl/gui_ribbon_e2e.js /tmp/beam /tmp/shots
```
Checks the six tabs, that every button has a handler, enabling/disabling by context (no model, model, results, modes), opening a mesh and a
result file through the file dialog (including Enter in the path box), mode selection/animation, deformation scale, field choice, view,
colour, edge, section and theme buttons with their pressed state, status/validation, result removal, MGWV/MGMB/INP export and keyboard
navigation between tabs. The click helpers of the other GUI scripts are scoped to `aside` so that ribbon buttons never match them.

## Solver launch (`gui_solver_e2e.js`)
Needs a **freshly started** `openmodel-webui` and the beam fixture. A stand-in for CalculiX (`tests/support/fake_ccx.py`) replaces `ccx`:

```bash
python3 tests/support/make_beam_results.py /tmp/beam
node tests/webgl/gui_solver_e2e.js /tmp/beam $PWD/tests/support/fake_ccx.py /tmp/work /tmp/shots
```
Checks the disabled state without a mesh, input writing, script errors with line numbers, a static run with live log and automatic
loading of `.frd` + `.dat`, a frequency run (modes), cancelling a solver that ignores SIGTERM, a failing solver, a missing executable,
a run without result file, and the ribbon tab. It tests the plumbing, not CalculiX.

## Picking (`viewer_pick_smoke.js`, `gui_pick_e2e.js`)
`viewer_pick_smoke.js <index.html> <model.mgwv> <model.inp> [shots]` (registered as `webgl_pick_smoke`; the `.inp` is parsed as an independent oracle).
`gui_pick_e2e.js <beam dir> <fake_ccx.py> [work] [shots]` needs a freshly started `openmodel-webui`: mouse selection of faces/nodes, groups, script lines,
the written `*DLOAD` lines against CalculiX face numbers computed in the test, and a run with the fake solver.
