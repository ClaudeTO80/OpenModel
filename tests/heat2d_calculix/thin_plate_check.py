"""Manual check of the thin-plate criterion (Heat2D assessThinPlate), needs ccx on PATH.

3D block 1 x 1 x T, two element layers through the thickness. Temperature on the lateral sides (z-independent),
source q, convection h on both z-faces (Tinf = 0). Reports max |T(face) - T(mid-plane)| / max T for several h."""
import subprocess
LX = LY = 1.0; NX = NY = 10; T = 0.1; K = 1.0; Q = 1.0
hx, hy = LX/NX, LY/NY
def fmt(ids):
    ids = list(ids)
    return "\n".join(", ".join(str(i) for i in ids[k:k+16]) for k in range(0, len(ids), 16))
def build(h):
    nodes2d = [(i, j) for j in range(NY+1) for i in range(NX+1)]
    n2 = len(nodes2d)
    # layer L (0 = bottom face, 1 = mid-plane, 2 = top face); id = L*n2 + index + 1
    nid = {(L, p): L*n2 + k + 1 for L in range(3) for k, p in enumerate(nodes2d)}
    els = [(i, j) for j in range(NY) for i in range(NX)]
    with open("blk.inp", "w") as f:
        f.write("*NODE\n")
        for L, z in enumerate((0.0, T/2, T)):
            for p in nodes2d:
                f.write(f"{nid[(L,p)]}, {p[0]*hx:.12g}, {p[1]*hy:.12g}, {z:.12g}\n")
        f.write("*ELEMENT, TYPE=C3D8, ELSET=SOLID\n")
        e = 0
        for (i, j) in els:
            for L in range(2):
                e += 1
                q = [(i,j),(i+1,j),(i+1,j+1),(i,j+1)]
                f.write(f"{e}, " + ", ".join(str(nid[(L,p)]) for p in q) + ", " + ", ".join(str(nid[(L+1,p)]) for p in q) + "\n")
        f.write("*MATERIAL, NAME=M1\n*CONDUCTIVITY\n" + f"{K}\n*SOLID SECTION, ELSET=SOLID, MATERIAL=M1\n")
        f.write("*STEP\n*HEAT TRANSFER, STEADY STATE\n*DFLUX\nSOLID, BF, " + f"{Q}\n")
        sides = [p for p in nodes2d if p[0] in (0, NX) or p[1] in (0, NY)]
        f.write("*NSET, NSET=SIDE\n" + fmt([nid[(L,p)] for L in range(3) for p in sides]) + "\n")
        f.write("*NSET, NSET=Nall\n" + fmt(sorted(nid.values())) + "\n")
        f.write("*BOUNDARY\nSIDE, 11, 11, 0.0\n")
        if h > 0:
            # Film on the element faces: F1 = nodes 1-4 (z = 0, bottom layer), F2 = nodes 5-8 (z = T, top layer).
            # Element ids: column k gives bottom layer 2k+1 and top layer 2k+2.
            f.write("*FILM\n")
            for k in range(len(els)):
                f.write(f"{2*k+1}, F1, 0.0, {h}\n")
                f.write(f"{2*k+2}, F2, 0.0, {h}\n")
        f.write("*NODE PRINT, NSET=Nall\nNT\n*END STEP\n")
    subprocess.run(["ccx", "-i", "blk"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Tn = {}; inb = False
    for line in open("blk.dat"):
        if "temperatures for set" in line: inb = True; continue
        if inb:
            p = line.split()
            if len(p) == 2:
                try: Tn[int(p[0])] = float(p[1])
                except ValueError: pass
            elif p: inb = False
    tmax = max(Tn.values())
    dev = max(abs(Tn[nid[(0,p)]] - Tn[nid[(1,p)]]) for p in nodes2d)
    return dev / tmax, tmax
for h in (0.02, 2.0, 20.0):
    bi = h*(T/2)/K
    rel, tmax = build(h)
    print(f"h_z={h:6.2f}  Bi={bi:7.4f}  face-to-mid relative deviation={rel:.4e}  (Tmax {tmax:.4f})")
