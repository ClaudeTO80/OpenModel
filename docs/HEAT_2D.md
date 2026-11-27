# Two-dimensional heat conduction, thin plate (`OpenModel::Heat2D`)

Status: **core solver implemented and tested** (structured grid, holes by element centroid). Not yet connected to the
script, the CLI, the GUI or the Gmsh path. Header: `include/OpenModel/Heat2D.hpp`. Source: `src/Heat2D.cpp`.
Test: `tests/heat2d_test.cpp` (CTest `heat2d_test`).

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

## 5. Limits (not done or not verified)

* Staircase boundary for holes: first-order geometric error. Curved holes are accurate only in the Gmsh path (to do).
* Only the rectangular plate. The axisymmetric (r-z) case is not in this increment.
* Not yet connected to the script (`analysis heat 2d`), the CLI, the GUI or the viewer. The results are in memory only.
* No Gmsh path: a 2D CAD loaded and meshed with Gmsh, and a mesh supplied from outside, are the next step. Gmsh is not
  compilable in the environment where this was written, so that path cannot be verified here.
* No time-dependent properties beyond \( T \); \( \rho c_p \) is a function of \( x, y \) only.
* The theta-method stability limit is not checked (as in Heat1D).
* The plate model is thin: no through-thickness gradient. Whether a 3D model can be reduced to it (small Biot number,
  uniform section along z, or axial symmetry) is a check the user makes; the code does not verify it.
* Not compared with CalculiX (`*HEAT TRANSFER`) or with another code.
