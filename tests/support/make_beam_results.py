#!/usr/bin/env python3
"""Writes a small cantilever-beam example: mesh (.inp) plus matching solver results.

Outputs, all in <outdir>:
  beam.inp           Abaqus/CalculiX mesh, C3D4 elements, node ids starting at 1
  beam.frd           CalculiX-style ASCII results (long format), two load steps: DISP and STRESS
  beam.csv           the same last-step displacement and a temperature column as a delimited table
  beam_other.frd     results computed on a different mesh (coordinates shifted) - must be rejected
  beam_modal.frd     the first five bending modes (frequency steps) with DISP per mode
  beam.dat           CalculiX-style text output (*EL PRINT S at one integration point per tetrahedron, times 1 and 2)
  beam_modal.dat     eigenvalue table of the five modes
  beam_elem.csv      per-element stress (value at the element centroid) and a scalar, matched by element id

The numbers are analytic (Euler-Bernoulli bending), not the output of a solver, so the files exercise the
readers and the viewer; they are not a validation of any solver.

usage: make_beam_results.py <outdir> [cells-along-length]
"""
import math
import os
import sys

L, H = 10.0, 1.0           # length along x, square section H x H
E = 210000.0
TIP_LOAD_DEFLECTION = -0.8  # tip deflection of the last step (model units)
NU = 0.3
RHO = 7.85e-9               # tonne/mm^3 (mm, N, tonne, s)
BETA_L = (1.875104069, 4.694091133, 7.854757438, 10.995540735, 14.137168392)  # cantilever beam roots


def fortran_e12(v: float) -> str:
    """E12.5 as written by CalculiX: sign/blank, d.ddddd, E+xx (12 characters)."""
    s = f"{v: .5E}"
    assert len(s) == 12, s
    return s


def main() -> None:
    outdir = sys.argv[1]
    nx = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    ny = nz = 2
    os.makedirs(outdir, exist_ok=True)

    sx, sy, sz = nx + 1, ny + 1, nz + 1
    nid = lambda i, j, k: 1 + i + sx * (j + sy * k)
    coords = {}
    for k in range(sz):
        for j in range(sy):
            for i in range(sx):
                coords[nid(i, j, k)] = (L * i / nx, H * j / ny - H / 2, H * k / nz - H / 2)

    tets = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                a, b, c, d = nid(i, j, k), nid(i + 1, j, k), nid(i, j + 1, k), nid(i + 1, j + 1, k)
                e, f, g, h = nid(i, j, k + 1), nid(i + 1, j, k + 1), nid(i, j + 1, k + 1), nid(i + 1, j + 1, k + 1)
                for t in ((a, b, d, h), (a, d, c, h), (a, c, g, h), (a, g, e, h), (a, e, f, h), (a, f, b, h)):
                    tets.append(t)

    with open(os.path.join(outdir, "beam.inp"), "w") as f:
        f.write("*HEADING\nCantilever beam example\n*NODE\n")
        for n, (x, y, z) in coords.items():
            f.write(f"{n}, {x:.9g}, {y:.9g}, {z:.9g}\n")
        f.write("*ELEMENT, TYPE=C3D4, ELSET=BEAM\n")
        for n, t in enumerate(tets, 1):
            f.write(f"{n}, {t[0]}, {t[1]}, {t[2]}, {t[3]}\n")

    def displacement(x, y, z, factor):
        s = x / L
        uy = factor * TIP_LOAD_DEFLECTION * s * s * (3 - s) / 2  # downward bending, tip = TIP_LOAD_DEFLECTION
        slope = factor * TIP_LOAD_DEFLECTION * 3 * s * (2 - s) / (2 * L)
        ux = -slope * y                                         # section rotation shortens the upper fibre
        return ux, uy, 0.0

    def stress_xx(x, y, factor):
        # bending stress ~ -y * M(x) ; M decreases linearly to zero at the tip
        return factor * 400.0 * (1 - x / L) * (-y / (H / 2))

    def stress_xy(x, y, factor):
        # parabolic shear profile, constant along the length (tip load), zero at the free fibres
        return factor * 60.0 * (1 - (2 * y / H) ** 2)

    def strain_tensor(x, y, factor):
        sxx, sxy = stress_xx(x, y, factor), stress_xy(x, y, factor)
        exx = sxx / E
        return (exx, -NU * exx, -NU * exx, sxy * (1 + NU) / E, 0.0, 0.0)   # tensor shear = tau / (2G)

    def mode_shape(n, x):
        """Normalised (tip = 1) bending mode n of a cantilever and its slope, x in [0, L]."""
        b = BETA_L[n] / L
        sig = (math.cosh(BETA_L[n]) + math.cos(BETA_L[n])) / (math.sinh(BETA_L[n]) + math.sin(BETA_L[n]))
        phi = lambda t: math.cosh(b * t) - math.cos(b * t) - sig * (math.sinh(b * t) - math.sin(b * t))
        dphi = lambda t: b * (math.sinh(b * t) + math.sin(b * t) - sig * (math.cosh(b * t) - math.cos(b * t)))
        return phi(x) / phi(L), dphi(x) / phi(L)

    def frequency(n):
        I, A = H ** 4 / 12, H * H
        return BETA_L[n] ** 2 / (2 * math.pi * L * L) * math.sqrt(E * I / (RHO * A))

    with open(os.path.join(outdir, "beam.frd"), "w") as f:
        f.write("    1CBEAM\n    1UDATE  31.10.2026\n")
        f.write("    2C" + " " * 6 + f"{len(coords):12d}" + " " * 37 + "1\n")
        for n, (x, y, z) in coords.items():
            f.write(f" -1{n:10d}{fortran_e12(x)}{fortran_e12(y)}{fortran_e12(z)}\n")
        f.write(" -3\n")
        f.write("    3C" + " " * 6 + f"{len(tets):12d}" + " " * 37 + "1\n")
        for n, t in enumerate(tets, 1):
            f.write(f" -1{n:10d}{4:5d}{0:5d}{1:5d}\n -2" + "".join(f"{v:10d}" for v in t) + "\n")
        f.write(" -3\n")
        for step, factor in ((1, 0.5), (2, 1.0)):
            def block_header():
                f.write(f"  100CL{step:3d}01{fortran_e12(float(step))}{len(coords):12d}" + " " * 20 + f"{1:2d}{step:5d}" + " " * 10 + f"{1:2d}\n")
            block_header()
            f.write(" -4  DISP        4    1\n")
            for i, name in enumerate(("D1", "D2", "D3"), 1):
                f.write(f" -5  {name:<8}{1:5d}{2:5d}{i:5d}{0:5d}{0:5d}\n")
            f.write(f" -5  {'ALL':<8}{1:5d}{2:5d}{0:5d}{0:5d}{1:5d}ALL\n")
            for n, (x, y, z) in coords.items():
                ux, uy, uz = displacement(x, y, z, factor)
                f.write(f" -1{n:10d}{fortran_e12(ux)}{fortran_e12(uy)}{fortran_e12(uz)}\n")
            f.write(" -3\n")
            block_header()
            f.write(" -4  STRESS      6    1\n")
            for i, name in enumerate(("SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX"), 1):
                f.write(f" -5  {name:<8}{1:5d}{4:5d}{i:5d}{i:5d}{0:5d}\n")
            for n, (x, y, z) in coords.items():
                f.write(f" -1{n:10d}" + fortran_e12(stress_xx(x, y, factor)) + fortran_e12(0.0) + fortran_e12(0.0)
                        + fortran_e12(stress_xy(x, y, factor)) + fortran_e12(0.0) + fortran_e12(0.0) + "\n")
            f.write(" -3\n")
            block_header()
            f.write(" -4  TOSTRAIN   6    1\n")
            for i, name in enumerate(("EXX", "EYY", "EZZ", "EXY", "EYZ", "EZX"), 1):
                f.write(f" -5  {name:<8}{1:5d}{4:5d}{i:5d}{i:5d}{0:5d}\n")
            for n, (x, y, z) in coords.items():
                f.write(f" -1{n:10d}" + "".join(fortran_e12(v) for v in strain_tensor(x, y, factor)) + "\n")
            f.write(" -3\n")
        f.write(" 9999\n")

    with open(os.path.join(outdir, "beam.csv"), "w") as f:
        f.write("node,U_x,U_y,U_z,Temp\n")
        for n, (x, y, z) in coords.items():
            ux, uy, uz = displacement(x, y, z, 1.0)
            f.write(f"{n},{ux:.6g},{uy:.6g},{uz:.6g},{20 + 5 * x / L:.4g}\n")

    # Vibration modes: header ICTYPE 2 (frequency), value = frequency, one DISP block per mode.
    with open(os.path.join(outdir, "beam_modal.frd"), "w") as f:
        f.write("    1CBEAM\n    1UDATE  31.10.2026\n")
        f.write("    2C" + " " * 6 + f"{len(coords):12d}" + " " * 37 + "1\n")
        for n, (x, y, z) in coords.items():
            f.write(f" -1{n:10d}{fortran_e12(x)}{fortran_e12(y)}{fortran_e12(z)}\n")
        f.write(" -3\n")
        for mode in range(len(BETA_L)):
            f.write(f"  100CL{mode + 1:3d}01{fortran_e12(frequency(mode))}{len(coords):12d}" + " " * 20 + f"{2:2d}{mode + 1:5d}" + " " * 10 + f"{1:2d}\n")
            f.write(" -4  DISP        4    1\n")
            for i, name in enumerate(("D1", "D2", "D3"), 1):
                f.write(f" -5  {name:<8}{1:5d}{2:5d}{i:5d}{0:5d}{0:5d}\n")
            f.write(f" -5  {'ALL':<8}{1:5d}{2:5d}{0:5d}{0:5d}{1:5d}ALL\n")
            for n, (x, y, z) in coords.items():
                uy, slope = mode_shape(mode, x)
                f.write(f" -1{n:10d}{fortran_e12(-slope * y)}{fortran_e12(uy)}{fortran_e12(0.0)}\n")
            f.write(" -3\n")
        f.write(" 9999\n")

    # Per-element table: stress at the element centroid (same analytic field) and a scalar.
    with open(os.path.join(outdir, "beam_elem.csv"), "w") as f:
        f.write("element,stress_xx,stress_yy,stress_zz,stress_xy,stress_yz,stress_zx,part\n")
        for n, t in enumerate(tets, 1):
            cx = sum(coords[v][0] for v in t) / 4
            cy = sum(coords[v][1] for v in t) / 4
            f.write(f"{n},{stress_xx(cx, cy, 1.0):.6g},0,0,{stress_xy(cx, cy, 1.0):.6g},0,0,{1 if cx < L / 2 else 2}\n")

    # CalculiX text output: element stresses at the (single) integration point of each tetrahedron, plus the
    # displacements of a node set, for the two times of beam.frd. Layout follows the CalculiX manual: header,
    # blank line, rows, blank line; stress components in the order sxx syy szz sxy sxz syz.
    with open(os.path.join(outdir, "beam.dat"), "w") as f:
        for step, factor in ((1, 0.5), (2, 1.0)):
            f.write(f"\n displacements (vx,vy,vz) for set NALL and time {float(step):13.7E}\n\n")
            for n, (x, y, z) in coords.items():
                ux, uy, uz = displacement(x, y, z, factor)
                f.write(f"{n:10d}{ux:14.6E}{uy:14.6E}{uz:14.6E}\n")
            f.write(f"\n stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set EALL and time {float(step):13.7E}\n\n")
            for n, t in enumerate(tets, 1):
                cx = sum(coords[v][0] for v in t) / 4
                cy = sum(coords[v][1] for v in t) / 4
                f.write(f"{n:10d}{1:4d}{stress_xx(cx, cy, factor):14.6E}{0.0:14.6E}{0.0:14.6E}{stress_xy(cx, cy, factor):14.6E}{0.0:14.6E}{0.0:14.6E}\n")
            f.write("\n")

    with open(os.path.join(outdir, "beam_modal.dat"), "w") as f:
        f.write("\n     E I G E N V A L U E   O U T P U T\n\n MODE NO    EIGENVALUE                       FREQUENCY\n")
        f.write("                                     REAL PART            IMAGINARY PART\n")
        f.write("                           (RAD/TIME)      (CYCLES/TIME     (RAD/TIME)\n\n")
        for m in range(len(BETA_L)):
            omega = 2 * math.pi * frequency(m)
            f.write(f"{m + 1:7d}{omega * omega:16.7E}{omega:16.7E}{frequency(m):16.7E}{0.0:16.7E}\n")
        f.write("\n")

    # Same file written for a mesh whose coordinates are shifted: the binder must refuse it.
    with open(os.path.join(outdir, "beam.frd")) as src, open(os.path.join(outdir, "beam_other.frd"), "w") as dst:
        in_nodes = False
        for line in src:
            if line.startswith("    2C"):
                in_nodes = True
            elif in_nodes and line.startswith(" -3"):
                in_nodes = False
            elif in_nodes and line.startswith(" -1"):
                x = float(line[13:25]) + 3.0
                line = line[:13] + fortran_e12(x) + line[25:]
            dst.write(line)

    print(f"wrote {len(coords)} nodes, {len(tets)} tetrahedra, modes {[round(frequency(m), 1) for m in range(len(BETA_L))]} Hz to {outdir}")


if __name__ == "__main__":
    main()
