# Meshatura con Gmsh

Implementazione: `src/GmshMesher.cpp`, interfaccia in `include/OpenModel/GmshMesher.hpp`, opzioni in `include/OpenModel/Mesher.hpp`, parser delle impostazioni della GUI in `include/OpenModel/GmshSettings.hpp`.

## Fasi di `GmshMesher::buildMesh`

1. **Importazione**: `gmsh::model::occ::importShapes` con `highestDimOnly`; ogni corpo di primo livello diventa un `MeshBody` con la sua dimensione (`bodyDimensions`).
2. **Opzioni globali** di Gmsh: `Mesh.MeshSizeMin` (da `minElementSize`), `Mesh.MeshSizeMax` (da `maxElementSize`), `Mesh.MeshSizeFromPoints` = 1, `Mesh.MeshSizeFromCurvature` (0 o `curvaturePointsPerCircle` se `curvatureAdaptation`), `Mesh.ElementOrder`, `Mesh.RecombineAll`, `Mesh.SecondOrderIncomplete`.
3. **Solidi in contatto**: con `InterfaceMode::Conformal` i solidi vengono frammentati per condividere le facce.
4. **Mesh strutturata** (solo se `structure == Structured`): `buildStructuredModel` classifica ogni solido, poi prepara transfinite o sweep. Se un solido non è né blocco né prisma, si registra il motivo (con `structuredFallback` si passa alla tetraedrica, altrimenti errore).
5. **Impostazioni tetraedriche** (solo se `structure == Unstructured`): `validateTetrahedralOptions`, poi `Mesh.Algorithm`, `Mesh.Algorithm3D`, `Mesh.Optimize`, `Mesh.OptimizeNetgen`, `Mesh.OptimizeThreshold`, `Mesh.Smoothing`, `Mesh.MeshSizeExtendFromBoundary`.
6. **Sizing CAD**: `limitSizeOfThinFaces` (`thinFaceSizeFactor`) e `applyCadFaceSizing` (`cadFaceSizeFactor`, `cadSmallFaceLength`).
7. **Generazione** `gmsh::model::mesh::generate`. Con ricombinazione automatica si provano in ordine gli algoritmi (vedi sotto).
8. **Estrazione** dei nodi e degli elementi (`mapElementType`). Per gli elementi strutturati quadratici l'ordine dei nodi è quello di Abaqus.
9. **Mappatura dei gruppi CAD** (`mapCadGroups`): volumi → solidi, superfici → facce (con audit se `auditAllCadFaces`), curve → spigoli, vertici → nodi.

Le fasi principali scrivono una riga di tempo (`trace`) quando il livello di log è `Info` o superiore; `GmshMesher::log()` restituisce il log anche dopo un errore.

## Mesh strutturata

- **Blocco esaedrico**: sei facce con quattro lati, otto vertici (la prova è topologica). Curve transfinite, superfici e volume danno una griglia di esaedri o tetraedri.
- **Prisma retto**: due facce piane congruenti e parallele, con area per distanza uguale al volume. La faccia di base si estrude per strati.
- Il numero di nodi di ogni curva e di strati è il minimo che rispetta `maxElementSize` (`ceil`).
- Un solido che non è né blocco né prisma non è meshabile in struttura: è il caso di ingranaggi conici, carter e geometrie con raccordi (vedi `STATUS.md`).

### Ricombinazione e fallback (`recombinationAttempts`)

`BlossomFullQuad` (algoritmo 3) richiede un numero **pari** di segmenti sul contorno di ogni faccia. Su alcune facce sweep a dimensioni maggiori questo non vale e Gmsh lancia `1D mesh cannot be divided by 2`.

Con ricombinazione **automatica** il mesher prova in ordine: esaedri 3 → 1 → 0; misti 1 → 0. Ogni fallimento viene registrato nei diagnostici. Con un algoritmo **esplicito** (per gli utenti avanzati) il valore non viene mai cambiato.

Misure sul file IPN100 L1000 (sweep di profilo): algoritmo 3 OK a 1, 2, 3; fallisce a 4, 5, 6 e 8; algoritmi 0 e 1 OK a tutte le dimensioni provate.

## Impostazioni tetraedriche (`TetrahedralGmshOptions` + campi del generate-volume)

Default = default di Gmsh. Ogni chiave è opzionale: assente o vuota = default.

| Chiave GUI/JSON | Campo C++ | Gmsh | Default | Valori |
|---|---|---|---|---|
| `minSize` | `minElementSize` | `Mesh.MeshSizeMin` | 0 (nessun minimo) | ≥ 0, ≤ dimensione massima |
| `curvature` | `curvatureAdaptation` | `Mesh.MeshSizeFromCurvature` | 1 | 0 o 1 |
| `curvaturePoints` | `curvaturePointsPerCircle` | (valore di `MeshSizeFromCurvature`) | 12 | ≥ 3 |
| `alg2D` | `algorithm2D` | `Mesh.Algorithm` | 6 (Frontal-Delaunay) | 1, 2, 5, 6, 7, 8, 9 |
| `alg3D` | `algorithm3D` | `Mesh.Algorithm3D` | 1 (Delaunay) | 1, 4, 7, 10 (10 = HXT) |
| `optimize` | `optimize` | `Mesh.Optimize` | 1 | 0 o 1 |
| `optimizeNetgen` | `optimizeNetgen` | `Mesh.OptimizeNetgen` | 0 | 0 o 1 |
| `optimizeThreshold` | `optimizeThreshold` | `Mesh.OptimizeThreshold` | 0,3 | (0, 1] |
| `smoothing` | `smoothingSteps` | `Mesh.Smoothing` | 1 | intero ≥ 0 |
| `extendBoundary` | `extendSizeFromBoundary` | `Mesh.MeshSizeExtendFromBoundary` | 1 | 0 o 1 |
| `auditCadFaces` | `auditAllCadFaces` | (audit OpenModel) | 1 | 0 o 1 |
| `thinF` | `thinFaceSizeFactor` | (sizing OpenModel) | 0 (off) | ≥ 0 |
| `cadF` | `cadFaceSizeFactor` | (sizing OpenModel) | 0 (off) | ≥ 0 |
| `cadSmall` | `cadSmallFaceLength` | (sizing OpenModel) | 0 | ≥ 0 |
| `bodyDims` | `bodyDimensions` | (corpi) | vuoto | `3,2,...` (una voce per corpo) |

Le chiavi sono nella richiesta JSON di `openmodel_generate_volume` (vedi `WEBUI.md`). `applyUnstructuredGmshSettings` le legge; un valore presente ma malformato è rifiutato con il nome della chiave.

Non esposte in GUI (volutamente): `Mesh.SubdivisionAlgorithm` (vedi sotto), `Mesh.RecombineAll` per la mesh tetraedrica, `incompleteSecondOrder` (riguarda solo esaedri e prismi).

### Risultati misurati (qualità minima del tetraedro, metrica del workbench)

| Modello | Dimensione | Default | `optimize=0` | `alg3D=10` (HXT) |
|---|---|---|---|---|
| `OilPan.stp` | 4 | 0,072 (3 elementi sotto 0,10) | 0,014 (216 sotto 0,10) | 0,299 (nessuno sotto 0,10) |
| `Helical Bevel Gear.STEP` | 4 | 0,319 | — | 0,413 |

Il tempo di meshatura con HXT è stato circa 5 s sull'oil pan e 2 s sul bevel gear (default 7,6 s e 4,0 s).

Metodo di misura della deviazione: per ogni nodo di superficie, distanza dal punto della superficie CAD con `gmsh::model::getValue` alle coordinate parametriche del nodo. Risultati: bevel gear 8·10⁻⁵ mm, oil pan 7·10⁻³ mm su un modello di circa 320 × 106 × 146 mm.

### Subdivisione (`Mesh.SubdivisionAlgorithm = 2`)

Trasforma i tetraedri in esaedri. Sull'oil pan a dimensione 4 dà 195 316 esaedri con elementi invertiti (minimo dello Jacobiano −55,8). Non va esposta senza un controllo di qualità.

## Test

- `gmsh_structured_test`: blocchi, prismi, secondo ordine, sizing sulle facce CAD, fallback.
- `gmsh_integration_test`: mesh end-to-end lineare e quadratica, superficie.
- `gmsh_settings_test` (core): parser delle impostazioni e rifiuto di valori malformati.
- Il fallback della ricombinazione non ha ancora un test di regressione: su una barra semplice l'algoritmo 3 non fallisce; serve un profilo come l'IPN (il test opzionale `OPENMODEL_GMSH_TEST_STEP` è il punto di partenza).
