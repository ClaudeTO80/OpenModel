# Two-dimensional heat conduction, thin plate (`OpenModel::Heat2D`)

Status: **core solver and script implemented and tested** (structured grid, holes by element centroid). Not yet connected
to the CLI, the GUI or the Gmsh path. Headers: `include/OpenModel/Heat2D.hpp`, `include/OpenModel/Heat2DScript.hpp`.
Sources: `src/Heat2D.cpp`, `src/Heat2DScript.cpp`. Tests: `tests/heat2d_test.cpp` (CTest `heat2d_test`) and
`tests/heat2d_script_test.cpp` (CTest `heat2d_script_test`).

## 1. Problem

Plate \( [0, L_x] \times [0, L_y] \) with uniform thickness \( t \) along \( z \), minus the holes. The temperature does
not vary through the thickness (thin-plate model). The energy balance gives

\[
t\,\rho c_p\,\frac{\partial T}{\partial t}
= \nabla\cdot\big(t\,k(x,y,T)\,\nabla T\big) + t\,q(x,y,t)
\]

On each side of the plate and on each hole boundary one of:

| Condition | Expression (heat entering the solid) |
|---|---|
| Temperature | \( T = T_b(t) \) |
| Heat flux | \( q''(t) \) per unit area of the edge (insulated when empty) |
| Convection and radiation | \( h\,(T_\infty - T) + \varepsilon\sigma\,(T_\infty^4 - T^4) \) |

The radiation sink is the ambient temperature of the same exchange, as in Heat1D.

## 2. Geometry and mesh

* Structured grid of \( n_x \times n_y \) bilinear quadrilaterals on the plate.
* A hole removes every element whose **centroid** is inside it (circle or axis-aligned rectangle). The solid boundary is
  therefore a **staircase**: its geometric error is first order in the element size. A curved boundary needs a
  conforming mesh (the Gmsh path, not in this increment).
* Nodes that belong to no solid element are inactive: they are removed from the system and reported as inactive.
* Edges with a condition: a plate side with the side's condition; a solid element against a hole, or against the
  outside of the plate, gets the hole condition or the plate side condition. A side between two solid elements carries
  nothing.

## 3. Discretisation

* Galerkin with bilinear shape functions, 2x2 Gauss points in each element; edge terms with 2-point Gauss on the edge.
* Conduction \( K_{ij} = \int t\,k\,\nabla N_i\cdot\nabla N_j\,dA \), capacity \( C_{ij} = \int t\,\rho c_p\,N_i N_j\,dA \)
  (consistent, or row-sum lumped), source \( g_i = \int t\,q\,N_i\,dA \).
* Convection and radiation on an edge: \( a(T) = h + \varepsilon\sigma (T^2+T_\infty^2)(T+T_\infty) \); the edge adds
  \( t\,a\,N_iN_j \) to \( K \) and \( t\,a\,T_\infty N_i \) to \( g \).
* Temperature sides: nodes fixed by elimination, which keeps the matrix symmetric.
* Theta method and Picard iteration exactly as in Heat1D (`docs/HEAT_1D.md`, section 3). The coefficients that depend on
  \( T \) are frozen at the iterate until the change is below `tolerance * max(1, max|T|)`.
* Linear solver: conjugate gradient with a Jacobi preconditioner (the matrices are symmetric positive definite).
  The solver is not a direct factorisation: the work per step grows with the number of nodes times the CG iterations.

## 4. Verification (what was run)

Build: `cmake --build . --target heat2d_test` in the project's CMake configuration, plus `g++ -std=c++17 -Wall -Wextra
-Wpedantic -Wshadow`; ASan and UBSan clean. `ctest -R heat` runs the 2D test and the 1D tests together: 4 of 4 pass.

| Test | Reference | Result |
|---|---|---|
| linear field \( T = 100x \), insulated top and bottom | exact linear profile | max nodal error < 1e-9 |
| convection at one end, \( h = 10 \), \( T_\infty = 20 \) | linear profile, \( T_L = 300/11 \) | max nodal error < 1e-9 |
| \( -\nabla^2 T = 1 \) on the unit square, zero sides, 40 x 40 | double Fourier series, centre 0.0736713 | 0.0737077 (error 3.6e-5) |
| transient sine mode, 40 x 4, \( \Delta t = 10^{-3} \), \( t = 0.1 \) | \( \sin(\pi x)e^{-\pi^2 t} \) | 0.372516 against 0.372708 |
| same, lumped capacity | same | error < 2e-3 |
| centred circular insulated hole, 40 x 40 | symmetry about \( x = 0.5 \) | asymmetry < 1e-10; hole centre inactive |
| rectangular hole at 50, insulated plate, no source | uniform 50 in the solid | error < 1e-9 |
| thickness 3 against thickness 1, source \( 1+x \) | steady solution independent of \( t \) | difference < 1e-10 |
| \( k = 1 + 0.01T \), Dirichlet 0 and 100 | Kirchhoff transform, \( 0.005T^2 + T = 150x \) | max nodal error 5.3e-11 K, 12 Picard iterations |
| input checks | messages | pass (steady without anchor, plate covered by holes, hole outside the plate, corner with two temperatures, non-positive conductivity, zero end time) |

The Fourier reference is the series \( \sum_{m,n\ \mathrm{odd}} \frac{16}{\pi^4 mn(m^2+n^2)}\sin\frac{m\pi}{2}\sin\frac{n\pi}{2} \),
summed to \( m, n < 200 \).

### CalculiX comparison (manual, not in CTest)

`tests/heat2d_calculix/`: the same plate (1 x 1, 20 x 20 grid, thickness 0.1, circular hole r = 0.2 at the centre, source
q = 1, sides at 0, hole at 50) written as a Heat2D script and as a CalculiX `*HEAT TRANSFER, STEADY STATE` deck of C3D8
elements one layer thick. The hole topology is the same (element centroids), so the node sets are the same.
Run: `python3 tests/heat2d_calculix/compare.py <heat2d_run> <workdir>` with `ccx` (CalculiX 2.21, Ubuntu package
`calculix-ccx`) on PATH.

| Case | Result |
|---|---|
| with source q = 1 | 404 nodes; max \( |T_{ccx} - T_{h2d}| \) = 4.5e-6 K on a 50 K field |
| source q = 0 (Dirichlet only) | max difference 4.7e-6 K |

The CalculiX log shows that the first iteration does not converge and the second does (its own residual-flux test). The
difference is nearly uniform over the nodes and is the same with and without the source, so it is attributed to the
convergence tolerance of CalculiX's nonlinear loop, not to the discretisation. This attribution is not proven: the
tolerance was not tightened to test it. The agreement is at the 1e-7 relative level, which is the check's sensitivity.

### Applicability of the thin plate to a 3D body (`assessThinPlate`)

`assessThinPlate(t, k, h_z, Bi_limit)` returns the Biot number \( Bi = h_z (t/2)/k \) on the faces normal to z and
whether it is below the limit. It cannot check that the boundary conditions and sources are independent of z: that
assumption is listed in its notes. Adiabatic z-faces (h_z = 0) give an exact reduction under that assumption.

Check with CalculiX (`tests/heat2d_calculix/thin_plate_check.py`, manual): a 1 x 1 block, thickness 0.1, two element
layers through the thickness, source q = 1, sides at 0, convection h_z on both z-faces. The deviation between the face
and the mid-plane temperature, relative to the maximum temperature:

| h_z | Bi | face-to-mid deviation |
|---|---|---|
| 0.02 | 0.001 | 5.0e-4 |
| 2.0 | 0.1 | 4.8e-2 |
| 20 | 1.0 | 3.3e-1 |

The deviation is about Bi/2 for small Bi. With the default limit of 0.1 the through-thickness deviation can reach about
5 %; a 1 % deviation needs Bi around 0.02. The limit is a choice of the user, not a fixed rule of the code. The 2D model
itself does not include the z-face exchange at all, so the deviation above is the error of ignoring it.

## 5. Script (`analysis heat 2d`)

Parser and runner: `include/OpenModel/Heat2DScript.hpp` (`parseScript`, `runScript`, `resultToCsv`). The header carries
the full syntax; the summary:

    analysis heat 2d
    plate lx=0.2 ly=0.1 nx=40 ny=20 thickness=0.005
    material k=50 rhocp=3.9e6
    hole circle cx=0.1 cy=0.05 r=0.02          # may repeat
    hole rect x0=0.02 y0=0.02 x1=0.04 y1=0.04  # may repeat
    initial T=20
    temperature side=left T=100                # side = left | right | bottom | top
    flux side=right q=500                      # no line = insulated
    convection side=top h=25 Tinf=20 [emissivity=0.8]
    hole temperature T=50                      # or: hole flux q=..., hole convection h=... Tinf=...
    heat q=5000
    job MAIN [solver=internal]
    step steady                                # or: step transient dt=0.01 end=10 [theta=1] [capacity=consistent|lumped]

Every side takes one condition, given once. `plate`, `material`, `initial`, `heat`, `job` and `step` appear once; the
hole condition line appears once and applies to every hole boundary. The values are constant: temperature-dependent
conductivity (`kT=`) and time-dependent values are not available in this script yet. Default `theta` is 1 (implicit
Euler), as in the 1D script. Errors are collected with their line numbers; a model that fails the final check
(`validate`) is reported without a line number.

Tests: `heat2d_script_test` solves the unit square with a source written as a script against the double Fourier
reference (centre 0.0737077 against 0.0736713), checks a circular hole and the hole condition (every active node at 50),
a uniform transient (stays at 100 to 1e-8), the CSV layout, and the error messages with their line numbers.

## 5b. Unstructured mesh and Gmsh (`Heat2DMesh`, `Heat2DGmsh`)

`solveMesh` solves the steady or transient problem on a triangle mesh given by the caller (linear triangles, 3-point
quadrature, theta method with consistent or lumped capacity, boundary edges tagged; a tag selects a condition, a tag not
listed is insulated). It uses the same sparse matrix and
conjugate-gradient solver as the structured solver (`src/Heat2DLinear.hpp`).

* Square split into 2 x 40^2 triangles, source 1, zero sides: centre error 3.6e-5 against the double Fourier series, the
  same as the structured 40 x 40 grid (test `heat2d_mesh_test`).
* `meshAnnulusGmsh` (CMake option `OPENMODEL_BUILD_GMSH_BACKEND`) builds an annulus with the Gmsh OpenCASCADE kernel. The
  test `heat2d_gmsh_test` solves outer R = 1 at 0, inner a = 0.2 at 50, source 1, and compares with the exact radial
  solution. Max nodal error by element size, measured here:

| h | nodes | max error (K) |
|---|---|---|
| 0.20 | 130 | 0.287 |
| 0.10 | 416 | 0.243 |
| 0.05 | 1554 | 0.0459 |
| 0.025 | 5870 | 0.0158 |

The error falls at each step. Between h = 0.05 and 0.025 the observed order is 1.54, not the 2 of the asymptotic
theory: the sizes used are still coarse for the logarithmic gradient at the inner circle. The test checks the order is
above 1.5, which is what was measured, not the asymptotic value.

`meshPlanarStepGmsh(path, size)` imports a STEP file that holds exactly one planar face and meshes it. Boundary edges
of the outer contour get tag 1, the edges of every hole get tag 2. A curve is an outer contour when its bounding box
touches a side of the face's bounding box, so a hole tangent to an outer side is misclassified. Verified here:

* Annulus written to STEP and imported: 1554 nodes at h = 0.05, max error 0.0458556 K, identical to the direct mesh.
* Rectangle 2 x 1 with a circular hole (r = 0.3) imported from STEP, outer at 0 and hole at 50, source 1: boundary values
  exact to 7e-14 K, and the minimum over the nodes is above -1e-9 K, as the minimum principle requires. The maximum is
  50 K on the hole, not above it, since the source lowers the temperature away from the hole.

Not verified: STEP files made by other CAD systems, which can carry trimmed or split faces and tolerance gaps.

Transient on the triangle mesh: \( T = \sin(\pi x) e^{-\pi^2 t} \) on the unit square, Crank-Nicolson, \( \Delta t = 10^{-3} \),
\( t = 0.1 \), 40 x 40 squares split into triangles: max nodal error 2.3e-4 (test `heat2d_mesh_test`). A uniform field with
all sides insulated stays at 100 K to 1e-8 (lumped capacity, implicit).

This is the conforming alternative to the staircase boundary of section 2: the hole boundary is a true circle. A direct
comparison with the staircase on the same geometry was not run (the structured solver only has a rectangle).

## 6. Limits (not done or not verified)

* Staircase boundary for holes in the structured solver: first-order geometric error. Curved holes need the mesh path (5b).
* Structured solver: only the rectangular plate. The axisymmetric (r-z) case is not in this increment.
* Mesh solver: no script, CLI or GUI integration yet.
* Not yet connected to the CLI, the GUI or the viewer. The script runs from C++ only: there is no WebUI handler yet.
* Gmsh path: annulus generator and STEP import of one planar face (section 5b). Files from other CAD systems are not
  tested. Gmsh 4.12.1 with OpenCASCADE is installed here (`libgmsh-dev`), so the path is compiled and tested in this
  environment.
* No time-dependent properties beyond \( T \); \( \rho c_p \) is a function of \( x, y \) only.
* The theta-method stability limit is not checked (as in Heat1D).
* The plate model is thin: no through-thickness gradient. Whether a 3D model can be reduced to it (small Biot number,
  uniform section along z, or axial symmetry) is a check the user makes; the code does not verify it.
* Not compared with CalculiX (`*HEAT TRANSFER`) or with another code.
