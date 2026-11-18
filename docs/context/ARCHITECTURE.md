# Architettura

## Mappa dei sorgenti

| Percorso | Contenuto |
|---|---|
| `include/OpenModel/` | Header pubblici della libreria core. La maggior parte dei tipi è nel namespace globale (`MeshData`, `MeshingOptions`, `Mesher`); alcuni moduli usano `namespace OpenModel` (per esempio `FemResults.hpp`, `OpenModel::FlatJson`). Non c'è una regola uniforme: controllare l'header. |
| `src/` | Implementazioni della libreria core. |
| `src/cli/` | Eseguibile `openmodel` (CLI): `info`, `validate-demo`, `export-web`, `write-input`, `solve`, `export-aster`, `solve-aster`, ecc. |
| `src/importers/` | Importatori di file FEM (Abaqus `.inp`, CalculiX `.inp`), con parte comune in `InpImporterCommon.hpp`. |
| `src/webui/` | Eseguibile `openmodel-webui`: `main.cpp` (callback WebUI), `gui.html` (interfaccia, incorporata via CMake), `FlatJson.hpp` (parser della richiesta). Richiede l'SDK WebUI. |
| `fatigue/` | Libreria separata `OpenModel::Fatigue` (include, src, tests), con un proprio `CMakeLists.txt`. Può essere compilata da sola contro un OpenModel installato. |
| `examples/` | Viewer WebGL (`webgl-fem-viewer`, `webgl-viewer`) ed esempi di consumo via CMake. |
| `tests/` | Test C++ del core. `tests/support/` contiene fixture e finti solutori (`fake_ccx.py`, `fake_aster.py`). `tests/webgl/` contiene i test di browser e GUI (opzionali). |
| `benchmarks/` | Benchmark sintetici (opzionali). |
| `docs/` | Documentazione di formati e funzionalità. `docs/index.html` è il manuale HTML. |
| `cmake/` | Template di configurazione del pacchetto, header di versione e file di build info. |

## Tipi principali

- **`CADModel`** (`CADModel.hpp`): interfaccia di un modello CAD. Implementazioni: `OCCModel` (STEP/IGES/BREP con Open CASCADE), `AssimpModel` (formati generici). Espone i sotto-mesh di render (`getRenderMeshes`), i componenti e i gruppi del file.
- **`Mesher`** (`Mesher.hpp`): interfaccia di un mesher. `buildMesh(cad, options, outMesh)` riempie un `MeshData`; `diagnostics()` restituisce note e avvisi. Implementazioni: `GmshMesher` (`GmshMesher.hpp`, `src/GmshMesher.cpp`), `TetGenMesher`, `NetgenMesher`.
- **`MeshingOptions`** (`Mesher.hpp`): tutte le opzioni di meshatura. Contiene le sotto-strutture `StructuredGmshOptions` (mesh strutturata), `TetrahedralGmshOptions` (mesh tetraedrica di Gmsh) e il vettore `additionalFaceGroups`.
- **`MeshData`** (`MeshData.hpp`): la mesh di output. Nodi (`Node`), blocchi di elementi (`ElementBlock`), gruppi fisici (`MeshPhysicalGroup`), gruppi (`MeshGroup`), corpi (`MeshBody`), vincoli (`NodeConstraint`). `MeshGroupRegistry` gestisce i gruppi per nome. `MeshBinaryIO` legge e scrive il formato MGMB.
- **`MeshQuality`** (`MeshQuality.hpp`): metrica di qualità per tipo di elemento (1 = ideale, non positivo = invertito). È la metrica che mostra la GUI, diversa dal `gamma` di Gmsh.
- **`AnalysisSetup`** (`AnalysisSetup.hpp`): modello di analisi (materiali, vincoli, carichi, passi, job) e scrittura del deck per il solutore (`AnalysisModel`, `AnalysisJob`, `AnalysisStep`).
- **`OpenModelProject`** (`OpenModelProject.hpp`): progetto salvato su disco.
- **`FemResults`** (`FemResults.hpp`) e **`ResultBinding`** (`ResultBinding.hpp`): risultati del solutore e loro legame ai nodi della mesh.

## Flusso dati tipico

1. `OCCModel` (o `AssimpModel`) carica il file e produce il CAD in memoria.
2. Un `Mesher` costruisce `MeshData` da `MeshingOptions`. Per Gmsh la mesh viene estratta da Gmsh e rimappata ai gruppi CAD (`mapCadGroups`).
3. `MeshValidator` e `computeMeshQuality` verificano la mesh. `MeshBinaryIO` la salva; `WebViewWriter` e `GltfWriter` la esportano per il viewer.
4. `AnalysisSetup` scrive il deck (CalculiX o Code_Aster); `ProcessRunner` avvia il solutore; `FemResults` legge i risultati e `ResultBinding` li collega alla mesh.

## Dipendenze

- Core: C++17 (`cxx_std_17`), senza dipendenze obbligatorie.
- Opzionali (ciascuna dietro un'opzione CMake, vedi `BUILD_AND_TEST.md`): Gmsh, Open CASCADE, Assimp, TetGen, Netgen, MFEM.
- GUI: WebUI (non incluso nel repo), e un browser basato su Chromium a runtime.
