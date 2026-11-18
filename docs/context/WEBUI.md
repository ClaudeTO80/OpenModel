# GUI (openmodel-webui)

Sorgenti: `src/webui/main.cpp` (backend, callback WebUI), `src/webui/gui.html` (interfaccia incorporata nel binario), `src/webui/FlatJson.hpp` (parser JSON minimale). Richiede l'SDK WebUI per essere compilata (vedi `BUILD_AND_TEST.md`).

## Trasporto

- Il browser (la pagina `gui.html`) chiama i callback con `act("nome", ...)`. Ogni callback è registrato con `webui_bind(window, "openmodel_...", handler)`; sono 39 nel file.
- **La richiesta di mesh è un unico oggetto JSON di stringhe** (`openmodel_generate_volume`). Il backend lo legge con `FlatJson::parseStringObject` in `parseVolumeArgs`. Le chiavi posizionali usate da `kVolumeKeys` sono quelle della mesh strutturata e dei parametri CAD; tutte le altre chiavi dell'oggetto restano in `VolumeArgs::fields`.
- Motivo: le richieste WebUI con più di 17 argomenti posizionali non arrivavano al gestore (osservato in precedenza; la richiesta JSON lo evita). Non aggiungere argomenti posizionali alle callback.
- **Risposta**: `reply(e, ok, message)` restituisce `{"ok": ..., "message": "...", <campi di vista>}`. Ogni errore di un handler deve passare da `reply(e, false, ...)`: un'eccezione non catturata chiude il callback senza risposta e la pagina mostra "Unexpected end of JSON input".

## Impostazioni tetraedriche nella richiesta

Chiavi del JSON (tutte stringhe; assenti o vuote = default):

- CAD: `thinF`, `cadF`, `cadSmall`, `bodyDims`.
- Gmsh tetraedrico: `minSize`, `curvature`, `curvaturePoints`, `alg2D`, `alg3D`, `optimize`, `optimizeNetgen`, `optimizeThreshold`, `smoothing`, `extendBoundary`, `auditCadFaces`.

Il parser è `applyUnstructuredGmshSettings(fields, options, bodyDimensions)` in `include/OpenModel/GmshSettings.hpp`; l'elenco e i default sono in `MESHING_GMSH.md`. Un valore presente ma malformato produce `"Invalid mesh settings: ..."` con il nome della chiave.

Nella GUI, le chiavi si costruiscono nel `const generate = () => act("openmodel_generate_volume", JSON.stringify({...}))`. I checkbox passano come `"1"`/`"0"` con `checkedFlag(id)`. I campi sono nella sezione `data-u="unstructured"` ("Tetrahedra (Gmsh) settings").

## Regole per modificare la GUI

- Ogni chiave del JSON deve coincidere con quella letta dal backend. Verificare entrambi i lati quando si rinomina una chiave.
- `gui.html` non può avere due funzioni o costanti top-level con lo stesso nome (`flag` del viewer e il nuovo helper sono distinti per questo: `checkedFlag`).
- Controllo di sintassi del JavaScript senza browser: estrarre i `<script>` di `gui.html` e lanciare `node --check`.
- Il comportamento della pagina (visibilità, ordine degli argomenti) richiede un test nel browser: `tests/webgl/gui_*.js` con Playwright, dove disponibile.

## Albero della sidebar

- Un solo albero (`#sbTreeBody`, funzione `renderTree()`): le categorie CAD, Mesh, Material, Constraints, Load cases sono nodi collassabili (stato in `localStorage`, chiave `openmodel.sbCollapsed`).
- L'occhio di Mesh, Constraints e Load cases controlla il viewer (`viewerVis` → `applyViewerVis()`). Sotto Material c'è un occhio per materiale (`materialRows()`), che nasconde i gruppi con quel `material` nei metadati MGWV. L'occhio di CAD non ha ancora un layer: gli assi stanno sotto CAD.
- Il "+" di ogni categoria è `SB_ADD`: apre l'azione della categoria, non crea ancora un elemento vuoto nello script.
- Test: `tests/webgl/gui_tree_smoke.js` (serve la pagina via HTTP).

## Viewer

- `examples/webgl-fem-viewer/index.html` è incorporato nel binario come byte array via CMake e caricato come iframe nella GUI.
- Il viewer ha picking di nodi, elementi e facce, vincoli e risultati; i test di riferimento sono `tests/webgl/viewer_*_smoke.js`.
