# One-dimensional heat conduction (`OpenModel::Heat1D`)

Status: **core solver, script and GUI panel implemented and tested**; not yet connected to the CLI or to the CalculiX/Elmer writers.
Header: `include/OpenModel/Heat1D.hpp`. Source: `src/Heat1D.cpp`. Tests: `tests/heat1d_test.cpp` (CTest `heat1d_test`).

The first model is the complete one (every term below switched on). The simplifications are switches of the same
model, so the complete formulation stays the reference and each simpler case is checked against it (see the ladder).

## 1. Problem

Segment \( x \in [0, L] \) with a cross-section \( A(x) \), perimeter \( P(x) \) and time \( t \in [0, t_{\text{end}}] \).
Temperature \( T(x,t) \). The energy balance of a slice gives

\[
\rho c_p A(x)\,\frac{\partial T}{\partial t}
= \frac{\partial}{\partial x}\!\Big(k(x,T)\,A(x)\,\frac{\partial T}{\partial x}\Big)
+ A(x)\,q(x,t)
- P(x)\,\big[\,h_\ell\,(T-T_\infty) + \varepsilon_\ell\,\sigma\,(T^4-T_\infty^4)\,\big]
\]

with initial condition \( T(x,0) = T_0(x) \) and, at each end, one of

| Condition | Expression at \( x = 0 \) (and analogously at \( x = L \)) |
|---|---|
| Temperature | \( T = T_b(t) \) |
| Heat flux | \( -kA\,\partial_x T = A\,q''(t) \), with \( q'' \) entering the domain |
| Convection and radiation | \( -kA\,\partial_x T = A\,\big[h\,(T_\infty - T) + \varepsilon\sigma\,(T_\infty^4 - T^4)\big] \) |

Insulated end: flux with no value. Steady problem: the time derivative is dropped and the source is taken at \( t = 0 \).

**Units.** The caller chooses a consistent set. Radiation requires absolute temperatures (kelvin) and \( \sigma \) in the same
units. Insulated sides: no lateral term.

**Radiation sink.** The radiative exchange uses the ambient temperature of its own exchange as the sink
(\( T^4 - T_\infty^4 \)). A separate sink temperature (for example a sky or an enclosure temperature) is not supported.

## 2. Weak form and discretisation

Uniform mesh of \( N \) linear elements of length \( h = L/N \), nodes \( x_i = i h \). Shape functions
\( N_1 = (1-\xi)/2 \), \( N_2 = (1+\xi)/2 \) on each element, with \( \xi \in [-1,1] \).

The weak form with test functions \( N_i \) leads to the semi-discrete system

\[
C\,\dot{T} + K(T)\,T = g(T,t),
\]

where, element by element and with two-point Gauss quadrature (exact for the polynomial parts when \( A \), \( k \), \( P \)
are constant):

* \( C_{ij} = \int \rho c_p A\, N_i N_j\,dx \) (consistent) or its row-sum lumping \( C_{ii} = \int \rho c_p A\, N_i\,dx \).
* Conduction: \( K^{\text{cond}}_{ij} = \int k(x,T)\,A\,\partial_x N_i\,\partial_x N_j\,dx \).
* Lateral and end exchange: the flux \( a(T)\,(T - T_\infty) \) with
  \( a(T) = h + \varepsilon\sigma\,(T^2 + T_\infty^2)(T + T_\infty) \) (this is exact:
  \( T^4 - T_\infty^4 = (T^2+T_\infty^2)(T+T_\infty)(T-T_\infty) \)).
  It adds \( \int P\,a\,N_iN_j \) to \( K \) (at the ends: \( A\,a \) on the node) and \( \int P\,a\,T_\infty N_i \) to \( g \).
* Source: \( g_i = \int A\,q\,N_i\,dx \). Heat flux at an end: \( g_{\text{end}} \mathrel{+}= A\,q'' \).
* Temperature ends are imposed as Dirichlet conditions on the system (the row becomes the identity).

Because the mesh is uniform and linear, every system is tridiagonal: the solves use the Thomas algorithm (\( O(N) \)).

## 3. Time integration and non-linearity

Theta method on \( [t_n, t_{n+1}] \), \( \theta \in [0,1] \):

\[
\Big(\tfrac{C}{\Delta t} + \theta K^{k}\Big) T^{k+1}
= \Big(\tfrac{C}{\Delta t} - (1-\theta) K^{n}\Big) T^{n} + (1-\theta)\,g^{n} + \theta\,g^{k},
\]

where \( K^{n}, g^{n} \) are evaluated at \( T^n, t_n \) and \( K^{k}, g^{k} \) at the iterate \( T^k \) and at \( t_{n+1} \).
The coefficients that depend on \( T \) (conductivity, radiation) are **frozen at the iterate** (Picard). The loop stops when
the largest change is below `tolerance * max(1, max|T|)`. At convergence the frozen coefficients are the exact ones, so
the result is the theta method applied to the complete non-linear problem.

| \( \theta \) | Scheme | Note |
|---|---|---|
| 0 | explicit Euler | conditionally stable; **the code does not check the stability limit** |
| 0.5 | Crank-Nicolson | second order in time |
| 1 | implicit Euler | unconditionally stable, first order |

Capacity: `Capacity::Consistent` (default) or `Capacity::Lumped`.

Steady problem: \( K(T)\,T = g(T) \) solved by the same Picard loop. It needs a temperature end, a convective end or a lateral
exchange; otherwise the temperature level is undetermined and the input is refused.

## 4. Options and output

`Options`: `steady`, `theta`, `timeStep` (the step is shortened so that the run ends exactly at `endTime`), `endTime`,
`capacity`, `tolerance` (default \(10^{-10}\)), `maxIterations` (default 100), `recordEvery`.

`Result`: `nodes`, `times`, `temperatures[k][i]` (time \(k\), node \(i\)), `converged`, `maxPicardIterations`, `error`.
Invalid input or a failed Picard loop is reported in `error` (no exception reaches the caller).

## 5. Simplification ladder

Each rung removes something from the complete model. The test column names the check that covers the rung.

| Level | Model | What is removed | Covered by |
|---|---|---|---|
| 0 | complete | — | all tests |
| 1 | constant properties | \( k(x,T) \to k \), \( \rho c_p \) and \( A \) constant | `steady linear`, `fin`, `transient sine` |
| 2 | no radiation | \( \varepsilon = 0 \): the exchange is linear | `fin`, `convection end` |
| 3 | no lateral exchange | \( P = 0 \) or no `lateral` | `convection end`, `heat flux end`, `energy balance` |
| 4 | no temperature dependence in \( k \) | \( k \) a function of \( x \) only | `variable area`, `transient sine` |
| 5 | steady, no source | \( \partial_t = 0 \), \( q = 0 \): linear profile | `steady linear` |
| 6 | constant cross-section and no source, one element | the solver reduces to the textbook formulae | `convection end` (exact with N = 5) |

The non-linear-conductivity and radiation tests run the complete model for those terms (level 0 for them); the other rungs
are checked against closed forms.

## 6. Verification (what was run)

Build: `g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow` clean; ASan and UBSan clean; CMake target `heat1d_test`; the whole
core suite (58 tests) passes.

| Test | Reference | Result |
|---|---|---|
| steady linear, Dirichlet ends | exact linear profile | max nodal error 0 |
| steady, uniform source | \( T = x(1-x) \) | 2.8e-17 (exact at nodes) |
| variable area \( A = 1+x \) | \( T = 100 - 100\ln(1+x)/\ln 2 \) | N = 10: 7.5e-3; N = 40: 4.8e-4 (ratio 15.8: second order) |
| fin, lateral convection, insulated tip, N = 100 | \( \cosh \) profile, \( m = 10 \) 1/m | 1.6e-4 K |
| convection at one end | linear profile, flux balance | 7.1e-15 |
| flux at one end | \( T = 10 + 20x \) | 3.6e-15 |
| radiation at one end (N = 1) | bisection on the balance | \( T_L = 367.955 \) K, bisection 367.955 K; 18 Picard iterations |
| \( k = 1 + 0.01T \) | Kirchhoff transform (exact for this \( k \)) | 9.8e-11 K; 8 Picard iterations |
| transient sine mode, N = 40, \( \Delta t = 10^{-3} \), \( t = 0.1 \) | \( \sin(\pi x)e^{-\pi^2 t} \) | CN 1.9e-4; implicit 1.6e-3; CN lumped 1.9e-4 |
| energy balance, variable \( A \), time-dependent source, insulated | \( \Delta E = \sum \Delta t\,[\theta S^{n+1} + (1-\theta)S^n] \) | residual 6.7e-13 (exact up to round-off) |
| recording, input checks, non-converged Picard | — | pass |

The energy balance holds exactly for the discrete scheme because the rows of \( C \) sum to \( \int \rho c_p A\,N_j \) and the
rows of the conduction matrix sum to zero; the test checks this identity, not a physical value.

The tests use the solver's own quadrature and nodes in the energy check; they do not validate against an external code.

## 7. Limits (not done or not verified)

* One uniform mesh; no quadratic elements; no adaptive or non-uniform mesh.
* \( \rho c_p \) does not depend on \( T \) (the capacity matrix is built once per run).
* No check of the explicit stability limit (\( \theta < 1/2 \)); the user chooses the step.
* A separate radiation sink temperature is not supported.
* Not compared with CalculiX (`*HEAT TRANSFER`) or with a second code. That comparison is the next step to put the
  numbers on an independent footing.
* Script parser done (`Heat1DScript`), with one or more materials along the axis; no GUI, no CLI command and no viewer
  output yet.
* A material interface is a node: a segment boundary cannot fall inside an element.
* Only 1D along the axis (the flow and the geometry are along the axis; the section is given by A and P). The 2D/3D
  formulation and the FEM mesh of OpenModel are out of scope: the 1D model has no mesh and runs only with the internal
  solver.
* Time dependence and amplitudes are not available in the 1D script: every value is constant.

## 8. Decisions taken

1. **Dedicated analysis** `analysis heat 1d`, with its own script (section 9), not the mesh-based analysis script.
2. **Solver**: the internal solver is the only one for 1D models. CalculiX and Elmer are for 3D mesh models (to be
   defined in a later step).
3. **Scope of 1D**: flows and geometry along the axis. Sections: rectangle, circle, tube, regular polygon, and a table
   for A(x) and P(x) by segments.
4. **GUI**: all the options of the backend (to be implemented; not verifiable here, since the WebUI SDK is absent).

## 9. Script (`analysis heat 1d`)

Parser and runner: `include/OpenModel/Heat1DScript.hpp` (`parseScript`, `runScript`, `resultToCsv`). Tests:
`tests/heat1d_script_test.cpp`. The header carries the full syntax; the summary:

    analysis heat 1d                          # length and elements come from the segments
    section rect a=0.1 b=0.1                  # A = a b, P = 2(a+b)
    section circle r=0.01                     # A = pi r^2, P = 2 pi r
    section tube ro=0.02 ri=0.01              # A = pi(ro^2 - ri^2), P = 2 pi (ro + ri): both surfaces exposed
    section polygon sides=6 r=0.05            # regular polygon, r = circumradius
    section table x=0,0.5,1 A=... P=...       # A(x), P(x) piecewise linear; x from 0 to the length
    material id=ALU k=200 rhocp=2.4e6         # constant conductivity; or kT=300:200,400:190 (table, see below)
    material id=STEEL k=50 rhocp=3.9e6
    segment material=ALU length=0.02 elements=20   # segments follow the axis from x = 0; they give the length
    segment material=STEEL length=0.05 elements=40
    initial T=20
    temperature end=left T=100               # or flux end=... q=..., convection end=... h=... Tinf=... [emissivity=...]
    lateral h=10 Tinf=20 [emissivity=0]
    heat q=5000                               # volumetric source
    job MAIN [solver=internal]
    step steady                               # or: step transient dt=0.01 end=10 [theta=0.5] [capacity=lumped]

Each command appears once, except `material` and `segment`, which repeat, and each end takes one temperature, flux or
convection line. Errors are collected with their line numbers.

Temperature-dependent conductivity. `kT=T1:k1,T2:k2,...` replaces `k=`: the conductivity is interpolated linearly in the
temperature (at least two pairs, temperatures strictly increasing, conductivities positive), and it is held at its end
values outside the table. The solver evaluates it at the Gauss points with the temperature of the current Picard
iterate. Tests: a linear table reproduces the Kirchhoff exact solution (error 1e-10 K, 8 iterations); a table with a kink
converges monotonically and needed 31 iterations. Not supported yet: a temperature-dependent heat capacity, and a table
that is steep enough to need many iterations is not checked beyond the iteration limit.

Materials. Each segment names a material and is meshed with its own number of elements; the interfaces between segments
are nodes, so the conductivity jumps exactly at a node. A single material can be given with the shorthand
`thermal k=... rhocp=...` and `analysis heat 1d length=L elements=N` (one segment over the whole length). The two forms
do not mix. The model does not check that the material properties of the segments are physically reasonable beyond
being positive. Test: a composite wall of two materials with the interface on a node is exact at the nodes (error
1.4e-14); the same wall written in the script gives the same result. The tube's perimeter counts both surfaces as exposed to the lateral exchange; this is an assumption of the
model, to be stated in the GUI as well.

Verification of the parser: the section areas and perimeters against their formulas (hexagon and square as exact
values), a steady flux bar against its exact profile (error 2.8e-14), a source case (0.25 at x = 0.5 exact), a transient
tube against the Fourier series (0.77479 against 0.77231: the lumped capacity error at 20 elements), and about thirty
error cases checked by message and line number.

### Primitives in a chain (`shape=` on a segment)

A segment may carry its own simple section, so a chain of primitives is a list of segments:

```
segment material=STEEL length=0.5 elements=20 shape=rect a=0.02 b=0.02
segment material=STEEL length=0.3 elements=6 shape=tube ro=0.05 ri=0.02
```

The keys are those of the section line (`rect a b`, `circle r`, `tube ro ri`, `polygon sides r`). A segment without a
shape takes the section line, which may be a table over the whole length. The section line can be left out when every
segment has a shape. Assumptions that still hold: the section is constant inside a segment and jumps at the interface
node; the lateral exchange uses the perimeter of the segment where the node lies; the end convection uses the area of
the end segment. Checked by a steady chain against the series resistances `sum L_i / (k_i A_i)` (error 3e-14 K) and
by a mixed case with a global rectangle and a circle on one segment.

## 10. GUI panel

The Analysis tab of the ribbon has the button **Heat 1D…**, which opens the section **Heat conduction 1D** in the
analysis overlay. It holds:

* the script, with every command listed under it (the same syntax as section 9); **Template** fills a transient example;
* **Run heat analysis**, which calls the handler `openmodel_heat1d_run` with the script text. The handler parses and
  runs the script on the internal solver and returns the result as JSON, without touching the structural model;
* the errors of the parser, each with its line number, or of the run; the previous result is hidden on an error;
* the **Primitive chain** block: materials (id, k, rhocp) and primitives (material, shape, dimensions, length,
  elements) in the order along the axis; **Set chain in script** replaces the material, segment, section and thermal
  lines of the script with the chain (after a confirmation) and keeps the other lines;
* the time selector (the recorded times), the temperature along the axis for the chosen time (SVG plot), and
  **Download CSV** with the same long-format CSV as `resultToCsv`.

The chain block builds the material and segment lines; the other commands are written in the script text. Checked by
`tests/webgl/gui_heat1d_smoke.js` (run with `node tests/webgl/gui_heat1d_smoke.js src/webui/gui.html
examples/webgl-fem-viewer/index.html`), which uses a stub bridge. The handler itself is compiled only inside the WebUI
build, which is not available in every environment; its JSON output was checked against the real core with a harness.
