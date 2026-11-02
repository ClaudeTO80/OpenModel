# Development log (historical, Italian)

Phase-by-phase notes written while the project was developed under its former name **MeshGen** (phases 8–50, CMake versions up to 0.49.0).
Names were replaced mechanically after the rename to OpenModel, so commands and paths are valid, but the version numbers quoted here
belong to the old numbering scheme. For the current state see the top-level [`README.md`](../README.md) and [`CHANGELOG.md`](../CHANGELOG.md).

---

# OpenModel — fase 50 (v0.49.0)

La fase 50 aggiunge il **picking di nodi, elementi e facce** nel viewer per creare gruppi e assegnare vincoli e carichi senza scrivere selettori a mano. Picking su GPU (buffer di id): clic, Ctrl+clic, riquadro (Alt+trascina), propagazione delle facce per angolo; vale solo ciò che si vede (anche con deformata e sezione). La selezione diventa un gruppo di nodi, di elementi o di facce (con elementi di superficie orientati verso l'esterno) e i pulsanti **Vincola / Forza / Pressione** aggiungono la riga `fix` / `force` / `pressure` allo script. MGWV porta gli id con tre chunk opzionali. Dettagli e limiti: [`docs/SOLVER.md`](docs/SOLVER.md).

**Verifica:** `viewer_pick_smoke.js` (31 controlli su pixel reali con oracolo dal `.inp`: nodi visibili 109/189, confermati contando a mano le facce visibili; elementi e facce; riquadro; angolo; deformata; sezione), `mesh_selection_test` (orientamento, quadratici nei due ordinamenti di nodi di mezzeria, mutazione volontaria rilevata), controllo degli id nel writer, `gui_pick_e2e.js` sulla GUI reale con mouse (ogni riga `*DLOAD` confrontata con la numerazione delle facce CalculiX derivata a parte), 38/38 CTest e gli e2e di ribbon, risultati e solver ancora verdi.

**Non verificato:** nessun `.inp` eseguito da un vero CalculiX; i gruppi non si possono eliminare né rinominare; prestazioni con selezioni molto grandi e gli input touch non misurati; nel picking dei nodi non si filtrano per blocco quelli di blocchi nascosti.

---

# OpenModel — fase 49 (v0.48.0)

La fase 49 aggiunge il **lancio del solutore CalculiX**: uno script di analisi breve (materiale, vincoli, forze, pressioni, gravità, passo statico o di frequenza) diventa un `.inp` completo (`AnalysisSetup`), `ccx` parte in background (`ProcessRunner`, log in tempo reale, annullamento con SIGTERM poi SIGKILL) e al termine `job.frd` + `job.dat` si caricano nel viewer. Disponibile nella GUI (scheda **Analisi** e sezione «6 · Analisi e solver») e da CLI (`openmodel write-input`, `openmodel solve`). Sintassi, formato scritto e limiti: [`docs/SOLVER.md`](docs/SOLVER.md).

**Verifica:** `analysis_setup_test` (parser con errori per riga, selettori, mappa delle facce P1–P6/S1–S4 derivata a parte, validazioni, runner con start/errore/cancel/SIGKILL), 6 test CLI con un finto `ccx` (`tests/support/fake_ccx.py`), e `gui_solver_e2e.js` sulla GUI reale (avvio, log, caricamento automatico, modi, annullamento di un solutore che ignora SIGTERM, errore, eseguibile mancante, ribbon); ribbon e risultati ancora verdi.

**Non verificato:** l'input generato non è stato eseguito da un vero CalculiX (non disponibile qui): i test usano un finto `ccx` e controllano l'impianto, non la fisica. Carichi nodali ripartiti in parti uguali (non consistenti per elementi quadratici, con avviso); pressione solo su tetraedri/esaedri; un materiale, un passo; runner solo POSIX (Windows non implementato).

Correzione di un mio errore emerso nei test GUI: un percorso senza estensione veniva trattato in modo ambiguo (file o cartella); ora `.inp` = file, altro = cartella.

---

# OpenModel — fase 48 (v0.47.0)

La fase 48 aggiunge alla GUI una **barra multifunzione (ribbon)** con le azioni principali, in sei schede:

- **File:** modello demo, importa CAD…, apri mesh…, apri risultati…; salva MGMB / INP / MGWV / GLB; salva PNG. Le azioni che richiedono un file aprono prima il selettore di file e partono alla scelta.
- **Geometria:** importa CAD, ritessella, vai ai parametri. **Mesh:** genera mesh, parametri, apri/salva mesh, valida.
- **Risultati:** apri/togli risultati, scelta del campo, colora per campo, deformata on/off e scala ×2 / ÷2, modo precedente/successivo con «modo k / n», animazione.
- **Vista:** +X +Y +Z Iso Inquadra; colore (blocchi, componenti, superfici, uniforme, campo); spigoli (nascosti, superficie, tutti); sezione (on/off, asse, inverti); tema.
- **Verifica:** riepilogo e validazione.

I pulsanti si abilitano secondo il contesto (senza modello, con modello, con risultati, con modi) e quelli a scelta mostrano lo stato attivo; i parametri (percorsi, dimensioni, tolleranze) restano nel pannello laterale. Le azioni sul viewer usano la sua API (`OpenModelViewer.set`). Frecce sinistra/destra cambiano scheda; l'ultima scheda è ricordata.

**Errore preesistente corretto:** premere Invio nel campo del percorso del selettore file non faceva nulla, perché nel gestore inline `list` veniva risolto come `input.list` (la proprietà del `<input>`) e non come la funzione; ora si usa `window.list`.

**Verifica:** 47 controlli nuovi in `gui_ribbon_e2e.js` sulla GUI reale (WebUI + Chromium) e i 28 controlli di `gui_results_e2e.js` ancora verdi; i test GUI esistenti ora cercano i pulsanti dentro `aside`. Non verificato: aspetto su schermi molto piccoli (sotto 850 px la barra scorre in orizzontale), gesti touch, lettori di schermo oltre agli attributi `role`/`aria` impostati.

---

# OpenModel — fase 47 (v0.46.0)

La fase 47 aggiunge il **lettore dei `.dat` di CalculiX** (`CalculixDatImporter`): blocchi nodali (`U`, `NT`...), blocchi per elemento ai punti di integrazione (`S`, `E`...) ridotti a un valore per elemento con la **media aritmetica dei punti** (avviso esplicito; non pesata), e la tabella degli autovalori (frequenze). Si unisce al `.frd` dello stesso run (`--results run.frd --results run.dat`, oppure `run.frd; run.dat` nella GUI): i passi si associano per tempo. Dettagli e limiti: [`docs/FEM_RESULTS.md`](docs/FEM_RESULTS.md).

**Verifica:** 28/28 test CTest con WebGL (nuovi: parsing `.dat` con esponenti Fortran a 3 cifre, media su 2 punti, tabella autovalori con autovalore negativo e coerenza ω² = λ, ordine SXZ/SYZ, unione di set, errori con numero di riga, unione con il `.frd`, due test CLI) e GUI reale con file uniti. Il test ha scoperto un mio errore (più set della stessa grandezza non venivano uniti), corretto.

Limiti: formato scritto dal manuale, da parser pubblici e da estratti pubblicati, non confrontato con un `.dat` prodotto da un vero CalculiX; la media non pesata è approssimata per elementi a 27 punti; una tabella di autovalori da sola non si può visualizzare (senza forme modali).

---

# OpenModel — fase 46 (v0.45.0)

La fase 46 estende i risultati FEM con **tensioni e deformazioni principali, von Mises, risultati per elemento e modi di vibrare**. Dettagli: [`docs/FEM_RESULTS.md`](docs/FEM_RESULTS.md).

- **Tensori:** per ogni tensore di tensione (`SXX…SZX`): von Mises, principali 1/2/3 (Jacobi, ordinate in modo decrescente), Tresca, idrostatica; per ogni tensore di deformazione (`EXX…EZX`, es. `TOSTRAIN`): deformazione equivalente, principali 1/2/3, taglio massimo, volumetrica. `--engineering-strain` se il file contiene γ = 2ε.
- **Risultati per elemento:** tabelle CSV con colonna `element`/`elem`/`eid`, associate per numero di elemento; `WebViewOptions::elementFields`; nel formato MGWV un campo con `"location":"element"` (un valore per triangolo). Nel viewer ogni elemento è colorato in modo uniforme (`[elemento]`).
- **Modi:** i passi di frequenza (ICTYPE 2) del `.frd` sono numerati e associati alla loro frequenza (anche frequenze coincidenti); `WebViewOptions::modes`, chiave MGWV `modes`; il viewer ha selettore del modo, frequenza, campo `|u| (modo)` e animazione. CLI: `--modes N`.
- Test: `fem_results_test` (autovalori in forma chiusa, rotazione casuale, autovalori ripetuti, deformazioni, mappatura elementi verificata sulla geometria scritta, modi e loro selezione), tre test CLI nuovi, `viewer_modal_smoke.js` (pixel reali) e `gui_results_e2e.js` esteso (GUI reale).

**Verifica:** 26/26 test CTest con `-DOPENMODEL_TEST_WEBGL_VIEWER=ON`; `gui_results_e2e.js` sulla GUI reale: tutti i controlli superati. Compilato con GCC; Gmsh, OpenCASCADE, Assimp, TetGen e Netgen non compilati in questa sessione.

Limiti noti: nessun `.frd` modale prodotto da CalculiX reale era disponibile (numerazione dei modi e unità della frequenza verificate solo sulla specifica e su un fixture analitico); i blocchi per elemento dei `.frd` sono ignorati (con avviso) e non esiste un lettore dei `.dat`; i valori per elemento sono piatti, le facce interne mostrano il valore di un solo elemento vicino; i tensori sono negli assi globali; le prestazioni delle nuove funzioni non sono state misurate.

---

# OpenModel — fase 45 (v0.44.0)

La fase 45 aggiunge il **caricamento dei risultati di un calcolo FEM** e la loro visualizzazione sulla mesh. Dettagli, formati e limiti: [`docs/FEM_RESULTS.md`](docs/FEM_RESULTS.md).

- **Lettura dei risultati** (`FemResults.hpp`): `CalculixFrdImporter` (`.frd` ASCII di CalculiX: campi nodali, più passi/incrementi, formato corto e lungo, coordinate del modello) e `CsvResultImporter` (tabella con una riga per nodo; colonne `U_x,U_y,U_z` raggruppate in vettori, `S_xx…S_zx` in tensori). `readResults()` sceglie l'importatore dall'estensione. Binario, risultati per elemento e per punto di integrazione vengono segnalati e non convertiti.
- **Associazione alla mesh** (`ResultBinding.hpp`): `bindResults()` collega i valori ai nodi per **numero di nodo** e, quando il file contiene le coordinate (FRD), verifica che appartengano a questa mesh: risultati calcolati su un'altra mesh sono **rifiutati** invece di essere dipinti sui nodi sbagliati. Genera campi scalari per componente più `|v|` e `von Mises` (tensori `SXX…SZX`), sceglie lo spostamento (`DISP`/`U`) e il passo (predefinito: l'ultimo).
- **Formato MGWV** (retrocompatibile): chunk opzionale `displacement` e chiavi opzionali `deformation` e `results`; i file senza risultati sono invariati.
- **Viewer:** un file con risultati si apre colorato per campo; pannello **Deformata** (interruttore, cursore, fattore numerico; il fattore suggerito porta lo spostamento massimo al 10 % delle dimensioni del modello); il piano di sezione lavora in coordinate indeformate.
- **CLI:** `openmodel export-web <mesh> <out.mgwv> --results <file> [--step N] [--displacement NOME|none] [--no-derived] [--no-coordinate-check]`, `openmodel results-info <file>`.
- **GUI:** sezione «5 · Risultati FEM» (file, passo, carica/rimuovi); i risultati decadono quando la mesh cambia e restano nell'export MGWV.
- Test: `fem_results_test` (parser FRD e CSV, errori, associazione, MGWV), cinque test CLI su un fixture (trave a sbalzo generata da `tests/support/make_beam_results.py`), `viewer_results_smoke.js` (pixel reali nel viewer) e `gui_results_e2e.js` (GUI reale, 21 controlli).

**Verifica di questa fase** (GCC 13.3, Ubuntu 24.04, CMake 3.28; Chromium headless con SwiftShader; WebUI commit 24c7d19): 22/22 test CTest su build pulita con `-DOPENMODEL_TEST_WEBGL_VIEWER=ON` (19 core + 3 WebGL), 20 controlli del viewer sui risultati e 21 della GUI reale superati; il vecchio smoke test del viewer resta verde (8/8). Gmsh, OpenCASCADE, Assimp, TetGen e Netgen non sono stati compilati in questa sessione e non sono toccati dalle modifiche.

Prestazioni (Release, un thread): `.frd` da 139 MB (270.009 nodi, 2 passi) letto in 0,94 s con 98 MB di picco; `export-web` di un `.inp` da 720.000 tetraedri con quei risultati: 3,8 s e 560 MB (2,7 s e 424 MB senza risultati). Frame rate su GPU reale non misurato.

Limiti noti: nessun file `.frd` completo prodotto da una vera esecuzione di CalculiX era disponibile (il solver non è installabile qui): il parser è verificato su file scritti secondo la specifica e su righe reali citate dal lettore `ccx2paraview`, quindi file insoliti restano non provati. Solo valori nodali; nodi senza valore sono disegnati con il minimo del campo (e segnalati); un solo passo per volta nel viewer; i nodi intermedi degli elementi di ordine superiore non sono visibili (come prima); nessuna tensione principale né altre grandezze derivate oltre a `|v|` e von Mises. Il codice resta C++17 come il resto del progetto.

---

# OpenModel — fase 44 (v0.43.0)

La fase 44 collega la mesh alla **struttura del CAD**: componenti, gruppi e continuità tra solidi a contatto.

- **Lettura XDE** (`OCCModel`, STEP e IGES): albero dell'assieme con i nomi scritti nel file, colori, layer e facce con nome. `CADModel` espone `getComponents()` (percorso `Assieme/Parte`, colore, solidi e facce) e `getGroups()` (layer e facce con nome). Se il file non contiene queste informazioni la mesh non ne inventa: restano solo i solidi. Se la lettura XDE fallisce si ricade sul lettore geometrico precedente.
  - Le facce con nome sono lette anche dal nome dell'entità `ADVANCED_FACE`, che XDE ignora (conserva solo i `SHAPE_ASPECT`). Gli esportatori CAD che non scrivono né l'uno né l'altro non producono gruppi di facce.
- **Gmsh → gruppi fisici:** ogni componente foglia diventa un gruppo fisico 3D con il suo percorso; layer e facce con nome diventano gruppi 2D. L'associazione tra entità Gmsh e oggetti CAD è geometrica (un volume appartiene al solido che ne contiene il baricentro di un elemento; una superficie alla faccia su cui giacciono i suoi nodi, purché confini un volume di un solido che possiede quella faccia), quindi vale anche dopo `fragment`. I gruppi finiscono in MGMB, INP e nel viewer.
- **Continuità tra solidi a contatto** (`MeshingOptions::interfaceMode`, solo Gmsh):
  - `Independent` (predefinito): ogni solido è meshato per conto suo, nodi distinti sull'interfaccia;
  - `Conformal`: `fragment` OpenCASCADE, una sola mesh di superficie sull'interfaccia, nodi condivisi.
  - Non implementata: interfaccia *coincidente con nodi duplicati* (per contatto o elementi coesivi).
  - Misura (fixture a 2 scatole + 2 perni, Gmsh 4.12): 44 nodi sul piano di contatto per lato, **0 condivisi** a `Independent`, **44 condivisi** a `Conformal`.
- **TetGen:** non trasferisce gruppi né interfacce e lo dichiara in `diagnostics()`. Rifiuta i CAD con più solidi (un solo PLC non rappresenta solidi a contatto: andava in segfault). Ora salda i vertici duplicati della tessellazione prima di passarli a TetGen e intercetta le eccezioni `int` che TetGen lancia (prima terminavano il processo).
- **Viewer:** albero dei componenti (i nomi sono divisi su `/`), colori dal CAD (campo opzionale `color` dei gruppi in MGWV, non salvato in MGMB), visibilità per componente o per ramo con stato indeterminato sul padre, wireframe che segue il gruppo (nuovo chunk `edgeRegion`, opzionale in lettura). Il viewer si apre colorato per componente quando il file ha gruppi. Nella vista della sola superficie CAD, i gruppi di facce non sono selezionabili (una faccia ha un solo gruppo di regione: vince il componente).
- **GUI:** selettore «Solidi a contatto», messaggio sulla struttura letta dal file, diagnostica del meshatore nel pannello messaggi.
- Test: `occ_structure_test` (assieme XDE scritto e riletto), `gmsh_assembly_test` (gruppi, volumi, nodi condivisi nei due modi), `tests/webgl/gui_assembly_e2e.js` (GUI reale, 21 controlli); `make_assembly_fixture` scrive il STEP di prova.

Limiti noti: la corrispondenza geometrica usa una tolleranza `1e-6 × diagonale` e va verificata su assiemi reali (provata solo sul fixture); `fragment` può fallire o creare facce spurie su CAD sporchi o con sovrapposizioni parziali (le sovrapposizioni totali producono volumi appartenenti a più gruppi, segnalati); IGES usa lo stesso percorso ma non è stato provato; con più istanze della stessa parte i nomi diventano `Parte`, `Parte #2`. Il test GUI è stato eseguito solo su Linux. `Conformal` usa `importShapes` da file: con OpenCASCADE di versione diversa da quella di Gmsh potrebbero esserci differenze di unità.

---

## Fase 43 (v0.42.0)

La fase 43 porta la mesh FEM nel browser: un nuovo formato di rendering compatto (**MGWV**), un writer C++ e un viewer WebGL2 autonomo.

- `WebViewWriter` converte una `MeshData` in MGWV: facce uniche (superficie ed, opzionalmente, interne), spigoli, identificatori di blocco/regione/superficie e campi scalari nodali. Coordinate float32 relative a un'origine, indici u32. Formato: [`docs/MGWV_FORMAT.md`](docs/MGWV_FORMAT.md).
- Viewer in `examples/webgl-fem-viewer/index.html`: un solo file, nessuna dipendenza esterna (funziona offline). I dati restano nelle texture della GPU (nessuna espansione dei vertici in JS), quindi il parsing è zero-copy.
  - colorazione per blocco, regione, superficie, campo scalare o uniforme; visibilità per blocco;
  - wireframe (superficie o tutti gli spigoli), sezione con piano per asse X/Y/Z, viste standard, salvataggio PNG, tema chiaro/scuro;
  - rotazione, spostamento e zoom con mouse e tocco; trascinamento del file sulla pagina o `?url=...`.
- CLI: `openmodel export-web <input.mgmb|input.inp> <output.mgwv> [--no-interior] [--field x|y|z|r]`, `openmodel demo-web <output.mgwv> [celle]`; `mesh-volume` accetta anche uscite `.mgmb` e `.mgwv`. `openmodel info` stampa ora la versione reale.
- Correzioni: `TetGenMesher.cpp` non compilava (header inesistente, API `tetgenio` errate); il link di OpenCASCADE includeva tutti i toolkit, anche quelli Draw/Test; `MeshData::addPhysicalGroup` era quadratico (costruzione di una mesh da 196k tetraedri con un gruppo unico: 6,6 s → 0,04 s).

### GUI (WebUI): `openmodel-webui`

La GUI è stata compilata ed eseguita contro il runtime WebUI reale (commit 24c7d19 del 2026-10-28) e pilotata con un test end-to-end (`tests/webgl/gui_e2e.js`, 17 controlli).

- **Correzione critica:** la pagina non includeva `<script src="webui.js">`, quindi nessun pulsante funzionava (l'errore veniva inghiottito e il messaggio restava vuoto). Ora la pagina attende anche la connessione WebSocket.
- **Flusso completo:** scelta del CAD (con navigatore di file lato server), tessellazione, **scelta del meshatore** (Gmsh, TetGen; Netgen compare ma resta disattivato se non compilato), parametri (dimensione massima/minima, ordine, adattamento alla curvatura; i campi cambiano col meshatore), meshatura, **visualizzazione con il viewer MGWV integrato** (a ogni passo mostra la superficie CAD o la mesh volumetrica, con sezione, wireframe e colorazione per patch), validazione.
- **Salvataggio e caricamento:** salva MGMB, esporta INP, MGWV e GLB; carica `.mgmb` e `.inp` e li visualizza.
- Le operazioni sono serializzate (una alla volta) e gli errori sono mostrati in rosso.
- `TetGenMesher`: il vincolo di volume era `elementSize²`; ora deriva dalla lunghezza dello spigolo (`L³/(6√2)`), quindi `elementSize` è davvero una lunghezza. A parità di valore la mesh TetGen risulta più fitta di prima.
- Le pagine HTML sono incorporate nell'eseguibile (nessun file da installare).

Limiti della GUI: nessuna barra di avanzamento (la pagina mostra "Operazione in corso…" ma non l'avanzamento); Netgen non è stato provato; il navigatore di file lavora sul disco della macchina che esegue la GUI (adatto all'uso locale, non a un server condiviso); il rendering è stato verificato solo in Chromium headless con SwiftShader.

Uso:

```bash
openmodel mesh-volume part.step part.mgwv 0.5     # CAD -> Gmsh -> MGWV
openmodel export-web model.mgmb model.mgwv --field z
# aprire examples/webgl-fem-viewer/index.html nel browser e scegliere model.mgwv
```

**Verifica di questa fase** (GCC 13.3, Ubuntu 24.04; Gmsh 4.12, TetGen 1.5, Assimp 5.3, OpenCASCADE 7.6): 17/17 test CTest con tutti i backend attivi. Lo smoke test del viewer (`tests/webgl/viewer_smoke.js`, opzionale con `-DOPENMODEL_TEST_WEBGL_VIEWER=ON`, richiede Node.js e Playwright) controlla sui pixel il rendering, le modalità di colore, la sezione e la visibilità dei blocchi in Chromium headless.

Prestazioni misurate (singolo thread): scrittura MGWV di 196k tetraedri 0,3–0,4 s, 1,57 M tetraedri 3,4 s (≈460k elementi/s). Il viewer carica un file da 103 MB (3,17 M triangoli) in 1,2 ms di parsing e 313 ms di upload, ma **solo con rendering software SwiftShader**: i frame rate su GPU reali non sono stati misurati.

Limiti noti: la sezione mostra le facce interne tagliate, non un tappo pieno; nessun picking dei singoli elementi; elementi di ordine superiore disegnati solo con i nodi d'angolo; gli spigoli condivisi tra blocchi seguono il blocco di indice minore; offset a 32 bit (payload < 4 GiB); il writer usa circa 32 byte per occorrenza di faccia in memoria temporanea; mancano ancora colorazione per qualità degli elementi e caricamento parallelo/mmap dell'MGMB.


## Fase 42 (v0.41.0) — importazione mesh FEM

La fase 42 introduce la prima API di importazione per mesh provenienti da solver FEM, mantenendo il core indipendente dai programmi esterni.

- `OpenModel::MeshImporter` definisce l'interfaccia astratta: `formatName`, `canRead` e `read`.
- `AbaqusInpImporter` e `CalculixInpImporter` importano il sottoinsieme documentato del formato keyword `.inp`.
- Diagnostiche strutturate con stato, codice, messaggio e numero di riga.
- Importazione transazionale: la mesh di destinazione viene sostituita solo quando lettura e validazione terminano correttamente.
- Node set ed element set sono importabili; gli element set vengono rappresentati come gruppi fisici quando richiesto.
- API compatibile con C++17 e nessuna dipendenza esterna aggiuntiva.

La prima versione non interpreta ancora `*INCLUDE`, righe di connettività continuate, set definiti per riferimento ad altri set, né le opzioni avanzate specifiche dei solver. I tipi di elemento non riconosciuti producono un errore esplicito anziché essere convertiti silenziosamente. Gli importatori ANSYS CDB e Nastran sono pianificati per le fasi successive.
Documentazione: [`docs/MESH_IMPORTERS.md`](docs/MESH_IMPORTERS.md).

Esempio:

```cpp
#include <OpenModel/MeshImporter.hpp>

OpenModel::AbaqusInpImporter importer;
MeshData mesh;
auto result = importer.read("model.inp", mesh);
if (!result) {
    for (const auto& diagnostic : result.diagnostics)
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
}
```

## Fase 41 (v0.40.0) — preparazione del reader MGMB al caricamento parallelo

La fase 41 separa il parsing e la validazione dell'header/directory dalla decodifica delle sezioni. Il formato binario resta **MGMB 1.0** e il comportamento pubblico del reader non cambia.

- `parseFileLayout` valida header, directory, offset, dimensioni, sezioni mancanti/duplicate e sovrapposizioni prima di leggere i payload.
- Il layout restituito contiene metadati e offset già validati.
- La base resta C++17; la lettura parallela sarà introdotta con un meccanismo compatibile con lo standard.

## Fase 40 (v0.39.0) — formato binario MGMB

La fase 40 introduce il formato binario nativo **MGMB 1.0** per il dump e il ripristino di `MeshData`.

- Header e directory a sezioni con offset/dimensioni espliciti e campi numerici little-endian.
- Nodi e coordinate scritti in array contigui; gli ID esterni sono conservati.
- Blocchi di elementi con codici di tipo stabili e connettività a 32 o 64 bit per blocco.
- Node set e gruppi fisici persistiti, inclusi tag e nomi dei materiali.
- Reader con controlli di limiti, conteggi, sovrapposizione delle sezioni, indici e invarianti strutturali.
- Scrittura su file temporaneo e pubblicazione al completamento.
- Test round-trip, mesh vuota, firma corrotta e file troncato.
- `openmodel-io-benchmark` misura dimensione del file, tempo mediano di lettura/scrittura e throughput.

API:

```cpp
#include <OpenModel/MeshBinaryIO.hpp>

MeshBinaryIO::write("model.mgmb", mesh);
MeshData restored = MeshBinaryIO::read("model.mgmb");
```

Formato dettagliato: [`docs/MGMB_FORMAT.md`](docs/MGMB_FORMAT.md).

Il formato 1.0 è non compresso e non include ancora checksum: questa scelta evita CPU extra nel percorso veloce, ma significa che la corruzione dei byte non è rilevata da un checksum dedicato. Il reader continua a verificare la struttura e gli indici.

# OpenModel — fase 34

## Fase 34 (v0.33.0) — riduzione della memoria temporanea nella topologia

La fase 34 limita le allocazioni temporanee durante l'estrazione delle facce e riduce la sovra-prenotazione della tabella hash che associa le chiavi canoniche alle facce uniche:

- le connettività delle facce in elaborazione restano in un array a capacità fissa sullo stack; il `std::vector` pubblico viene creato solo quando viene scoperta una faccia nuova;
- il confronto dell'orientamento lavora direttamente sull'array temporaneo;
- il rilevamento del nodo intermedio di uno spigolo quadratico non crea più un vettore temporaneo per ogni lato della faccia;
- la tabella `faceLookup` riserva una stima lineare prudente basata sul numero di elementi, invece del limite superiore di tutte le occorrenze. La tabella può crescere automaticamente se la mesh lo richiede.

L'API pubblica `MeshFace`/`MeshEdge` resta invariata; sono preservati l'ordinamento dei nodi delle facce, i gruppi fisici, le adiacenze, i tipi quadratici e le diagnostiche topologiche. La stima `estimated_retained_capacity_bytes` misura la memoria dei vettori mantenuti e quindi non rileva direttamente la riduzione delle allocazioni temporanee o dei bucket hash.

### Verifica e benchmark

Build Release GCC 14.2, Linux x86-64, sette ripetizioni e mediana, confronto eseguito nella stessa sessione:

| Griglia | Elementi | Fase 33 | Fase 34 | Variazione fase 34 |
|---|---:|---:|---:|---:|
| 8 × 8 × 8 | 3.072 | 2,641 ms | 2,320 ms | −12,2% |
| 16 × 16 × 16 | 24.576 | 24,269 ms | 24,099 ms | −0,7% |

I tempi sono sensibili al carico del sistema: il miglioramento misurato è evidente sul caso 8³ ma modesto sul 16³, perciò non va interpretato come una garanzia generale. In entrambi i casi la topologia prodotta è identica nei conteggi e senza facce non-manifold. Tutti i 10 test core passano. I backend opzionali che richiedono SDK esterni non sono stati verificati in questa build.

Comandi usati:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON -DOPENMODEL_BUILD_BENCHMARKS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/openmodel-benchmark 8 7
./build/openmodel-benchmark 16 7
```

---

# OpenModel — fase 33

## Fase 33 (v0.32.0) — riduzione delle allocazioni topologiche e stima della memoria

La fase 33 riduce le allocazioni temporanee nella costruzione della topologia. Le chiavi canoniche delle facce sono ora strutture inline a capacità fissa (fino a nove nodi, coprendo le facce supportate), invece di `std::vector` allocati per ogni lookup. La lookup dei gruppi di superficie legge direttamente la connettività del blocco, evitando una copia temporanea dei nodi. Le capacità dei contenitori di output vengono riservate con stime più prudenti per limitare la sovra-allocazione su mesh grandi.

Il benchmark riporta inoltre `estimated_retained_capacity_bytes`: stima del payload trattenuto dalle capacità dei vettori principali e dei vettori annidati di mesh/topologia. Non include overhead dell'allocatore, bucket delle tabelle hash, oggetti stringa completi o memoria temporanea di picco; non va interpretata come RSS o misura esatta del consumo di processo.

### Compilazione ed esecuzione

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON -DOPENMODEL_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel 2
ctest --test-dir build-bench --output-on-failure
./build-bench/openmodel-benchmark 16 7
```

Confrontare la mediana dei tempi e la stima di memoria sulla stessa macchina e con la stessa configurazione. L'ottimizzazione elimina le allocazioni delle chiavi temporanee, ma `MeshFace` e `MeshEdge` conservano ancora vettori pubblici per i nodi e gli elementi incidenti: la fase non è una riprogettazione completa del modello dati né una misura della memoria di picco.

---

### Confronto controllato con la fase 32

Build Release GCC 14.2, stesso ambiente Linux, sette ripetizioni e mediana. Le misure possono variare con il carico del sistema; il confronto è stato eseguito nella stessa sessione.

| Griglia | Elementi | Fase 32: topologia | Fase 33: topologia | Variazione |
|---|---:|---:|---:|---:|
| 8 × 8 × 8 | 3.072 | 3,675 ms | 2,448 ms | −33,4% |
| 16 × 16 × 16 | 24.576 | 51,073 ms | 28,244 ms | −44,7% |

La stima di capacità trattenuta dal benchmark della fase 33 è circa 2,06 MB per la griglia 8³ e 16,44 MB per la griglia 16³. È una stima parziale del payload dei vettori, non RSS e non memoria di picco; il confronto di memoria assoluto con la fase 32 non è disponibile perché quella versione non stampava questa metrica.

## Fase 32 (v0.31.0) — ottimizzazione della topologia e benchmark ripetibili

Questa fase sostituisce le mappe ordinate usate per la deduplicazione di facce e spigoli con lookup hash-based, preservando l’ordine deterministico dei vettori di output. Le chiavi delle facce sono ancora canonizzate ordinando gli indici dei nodi; la semantica della topologia resta invariata. Sono inoltre aggiunti benchmark a ripetizioni multiple con mediana dei tempi, per ridurre l’influenza delle oscillazioni di una singola esecuzione. Nessuna dipendenza esterna è stata aggiunta.

- Genera una griglia tetraedrica strutturata con decomposizione conforme dei cubi in sei tetraedri.
- Riporta numero di nodi/elementi, facce e spigoli derivati, facce di bordo/interne/non-manifold e problemi di validazione.
- Misura tempo trascorso e throughput per costruzione, topologia e validazione.
- Accetta una dimensione configurabile, da 1 a 32 suddivisioni per asse; il valore predefinito è 8. Le dimensioni alte possono richiedere molta memoria e tempo.
- Il target è disabilitato per impostazione predefinita e non viene installato come parte della libreria.

### Compilazione ed esecuzione

```bash
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMODEL_BUILD_TESTS=OFF \
  -DOPENMODEL_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel 2
./build-bench/openmodel-benchmark          # griglia 8 x 8 x 8, 5 ripetizioni
./build-bench/openmodel-benchmark 20       # griglia 20 x 20 x 20, 5 ripetizioni
./build-bench/openmodel-benchmark 16 9     # griglia 16 x 16 x 16, 9 ripetizioni
```

Il programma stampa righe `stage,median_seconds,elements_per_second`, utilizzabili per confronti fra revisioni o compilatori. Per confronti seri, usare la stessa macchina, compilatore, configurazione, dimensione della griglia e carico di sistema; ripetere le misure e confrontare la mediana. I tempi sono indicativi e non sono risultati universali né una garanzia di scalabilità su mesh CAD reali.

### Misure comparative della fase 32

Build Release con GCC 14.2, Linux x86-64; sette esecuzioni per revisione e mediana dei tempi di costruzione topologica:

| Griglia | Elementi tetraedrici | Fase 31 (v0.30.0) | Fase 32 (v0.31.0) | Riduzione del tempo |
|---|---:|---:|---:|---:|
| 8 × 8 × 8 | 3.072 | 6,967 ms | 3,399 ms | 51,2% |
| 16 × 16 × 16 | 24.576 | 74,713 ms | 36,115 ms | 51,7% |

Sono risultati di benchmark sintetico sulla stessa macchina, non una garanzia per altri sistemi o per mesh CAD reali. La costruzione della mesh e la validazione non sono l'obiettivo diretto di questa ottimizzazione.

# OpenModel — fase 30

## Fase 30 (v0.29.0) — topologia quadratica e gruppi di bordo

- Estrazione delle facce per tetraedri quadratici (Tet10), esaedri quadratici (Hex20/Hex27) e prismi quadratici (Prism15), oltre agli elementi lineari già supportati.
- Identificazione delle facce quadratiche Tri6, Quad8 e Quad9; le chiavi di condivisione includono tutti i nodi della faccia, non soltanto i vertici.
- Associazione dei gruppi fisici 2D alle facce volumetriche quando connettività e nodi coincidono esattamente.
- Conteggio degli spigoli appartenenti al bordo, rilevamento degli spigoli di bordo aperti/non-manifold, conservazione dei nodi intermedi sugli spigoli quadratici e diagnostica delle incongruenze.
- Segnalazione delle facce condivise con orientamento uguale nei due elementi adiacenti.
- Lookup dei gruppi di superficie precalcolato per evitare una scansione di tutte le superfici per ogni faccia volumetrica.
- Test aggiunti per due tetraedri quadratici adiacenti, gruppi fisici di bordo e facce non-manifold.

Le facce quadratiche sono rappresentate con i vertici iniziali, poi i nodi intermedi degli spigoli in ordine perimetrale e infine l'eventuale nodo centrale della faccia. La copertura di elementi quadratici segue le convenzioni di connettività standard assunte dal framework; va verificata contro ogni backend nativo.

## Fase 29 (v0.28.0) — test d'integrazione dei backend

Questa fase rende più chiaro e ripetibile il collaudo dei backend opzionali, senza introdurre dipendenze native obbligatorie nel core.

- **Assimp:** il test non si limita più a verificare il fallimento su un file inesistente. Genera un GLB reale con `GltfWriter`, lo rilegge con Assimp e verifica mesh, indici, coordinate finite e diagnostica dopo un caricamento fallito.
- **TetGen:** aggiunto un test end-to-end che tetraedralizza una superficie chiusa in memoria, controlla la connettività, esegue la validazione strutturale e geometrica e verifica il percorso di errore per parametri invalidi.
- **Gmsh:** mantenuto il test CAD end-to-end che crea/esporta un solido STEP, genera mesh lineari e quadratiche, valida ed esporta Abaqus INP.
- **Open CASCADE:** mantenuto il test che crea un solido, lo serializza in STEP, lo reimporta e controlla la triangolazione.
- **MFEM:** test di conversione verso la mesh nativa e verifica di attributi per volume e bordo.
- **Netgen:** il test viene etichettato come smoke test se non è fornito un fixture CAD; con `OPENMODEL_NETGEN_TEST_STEP` è riconosciuto come test con geometria reale.
- **CTest:** i test core e quelli d'integrazione hanno etichette separate; i test opzionali vengono creati soltanto quando `OPENMODEL_BUILD_TESTS=ON`.

### Esecuzione dei test

Test del core:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -L core
```

Test di tutti i backend che sono stati configurati con SDK reali:

```bash
ctest --test-dir build --output-on-failure -L integration
```

Il comando CTest esegue soltanto i test registrati nella configurazione corrente: non installa né scarica le dipendenze opzionali. Per esempio, il test Gmsh richiede `OPENMODEL_BUILD_GMSH_BACKEND=ON`, mentre il test TetGen richiede `OPENMODEL_BUILD_TETGEN_BACKEND=ON`. Netgen senza fixture CAD verifica soltanto il percorso di smoke test e non dimostra la generazione su una geometria reale.

La build standard resta indipendente da Gmsh, Open CASCADE, Assimp, Netgen, MFEM, TetGen e WebUI. I test d'integrazione nativi devono essere eseguiti su ogni piattaforma di rilascio con le versioni effettive delle librerie che si intende distribuire.

---

## Fase 28 (v0.27.0) — validazione geometrica e qualità della mesh

La fase 28 rafforza `MeshValidator` con controlli espliciti e metriche geometriche:

- rifiuto di tolleranze e soglie non finite o fuori intervallo;
- identificazione di elementi che ripetono un nodo di vertice;
- diagnostica dei nodi non utilizzati (avviso, configurabile);
- controllo di degenerazione per segmenti, triangoli e quadrilateri lineari/quadratici usando i vertici;
- calcolo dell'angolo minimo dei triangoli;
- qualità mean-ratio dei tetraedri, con valore ideale 1 per un tetraedro regolare;
- distinzione fra orientamento negativo, degenerazione e qualità sotto soglia;
- nuovi contatori nel report di validazione.

La qualità geometrica dei tetraedri usa i quattro vertici: per i tetraedri quadratici non valuta la curvatura introdotta dai nodi intermedi. Per esaedri, prismi e piramidi vengono controllate le ripetizioni dei nodi di vertice, ma non è ancora implementata una misura completa del Jacobiano/qualità. La fase 28 non dichiara quindi copertura geometrica completa di tutte le topologie.

## Fase 27 (v0.26.0) — workflow CAD → mesh FEM coerente

- L'esportazione GLB e INP dalla GUI accetta un percorso di destinazione configurabile.
- Lo stato del workbench mostra anche quando la superficie o la mesh volumetrica non sono disponibili e distingue la mesh CAD corrente da quella dimostrativa.
- Una ritessellazione CAD riuscita invalida la mesh volumetrica precedente: non è più possibile esportare accidentalmente un volume mesh obsoleto dopo aver modificato la discretizzazione della superficie.
- I messaggi di esportazione riportano il percorso effettivo e i conteggi scritti.
- CLI e GUI riportano la versione 0.26.0.

La fase 27 consolida il flusso applicativo, ma non sostituisce i test d'integrazione con SDK nativi: la generazione volumetrica reale richiede una build con Gmsh e il supporto CAD appropriato.

## Verifica end-to-end CAD → mesh FEM → Abaqus INP

Questa fase rafforza il test d'integrazione del backend Gmsh. Quando Gmsh è disponibile, il test crea un solido cubico con il kernel Open CASCADE di Gmsh, lo esporta in STEP e lo reimporta attraverso `GmshMesher`. Verifica quindi:

- generazione di tetraedri lineari e quadratici;
- generazione della sola mesh superficiale;
- controllo della mesh tramite `MeshValidator`, compresa la qualità minima dei tetraedri;
- esportazione Abaqus/CalculiX INP e presenza degli elementi `C3D4` e `C3D10`;
- corrispondenza tra conteggi di nodi/elementi del modello e quelli riportati dal writer;
- comportamento transazionale in caso di parametri non validi: la mesh preesistente non viene modificata;
- messaggi diagnostici quando il meshing fallisce.

Il test d'integrazione richiede `OPENMODEL_BUILD_GMSH_BACKEND=ON` e una libreria Gmsh compilata con supporto Open CASCADE per leggere/scrivere STEP. La configurazione standard del core non richiede Gmsh e rimane indipendente dalle dipendenze esterne.

## Build e test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Per includere il test CAD reale, con Gmsh installato:

```bash
cmake -S . -B build-gmsh \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMODEL_BUILD_TESTS=ON \
  -DOPENMODEL_BUILD_GMSH_BACKEND=ON
cmake --build build-gmsh --parallel 2
ctest --test-dir build-gmsh --output-on-failure
```

I test C++ basati su `assert` vengono compilati con `NDEBUG` disabilitato anche in Release, così le verifiche non vengono eliminate dalla configurazione di ottimizzazione.

## Workbench desktop WebUI e importazione CAD

La GUI `openmodel-webui` è stata trasformata da demo API a workbench CAD leggero:

- campo per il percorso del file sorgente e comando di importazione;
- selezione automatica del backend in base all'estensione: STEP/STP, IGES/IGS e BREP/BRP tramite Open CASCADE; altri formati tramite Assimp;
- controlli di deflessione lineare e angolare e comando di ritessellazione;
- anteprima 3D WebGL incorporata con rotazione tramite trascinamento, zoom con rotella e reset con doppio clic;
- stato della superficie, numero di parti/vertici/triangoli ed esportazione GLB;
- messaggio esplicito quando un backend opzionale non è stato compilato.

L'anteprima trasferisce al browser i triangoli in JSON e limita il rendering a 150.000 triangoli per evitare allocazioni eccessive; l'esportazione GLB conserva comunque la mesh completa. Per modelli molto grandi è preferibile visualizzare una versione decimata o utilizzare il GLB in un viewer dedicato.

La GUI distingue la mesh di superficie importata dalla mesh volumetrica FEM: non esporta impropriamente una superficie CAD come mesh volumetrica Abaqus. Il pulsante INP/validazione riguarda ancora la mesh tetraedrica dimostrativa; la generazione volumetrica da CAD richiede un backend meshing integrato nel workflow.

### Build

```bash
# Core, CLI e test
cmake -S . -B build -DOPENMODEL_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# GUI WebUI, dopo aver installato il WebUI SDK
cmake -S . -B build-gui \
  -DOPENMODEL_BUILD_WEBUI=ON \
  -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk
cmake --build build-gui --target openmodel-webui

# GUI con importazione di mesh/formati supportati da Assimp
cmake -S . -B build-assimp-gui \
  -DOPENMODEL_BUILD_WEBUI=ON \
  -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk \
  -DOPENMODEL_BUILD_ASSIMP_BACKEND=ON

# GUI con importazione CAD STEP/IGES via Open CASCADE
cmake -S . -B build-occ-gui \
  -DOPENMODEL_BUILD_WEBUI=ON \
  -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk \
  -DOPENMODEL_BUILD_OCC_BACKEND=ON
```

Per abilitare entrambi i backend, impostare entrambe le opzioni. Le librerie native devono essere disponibili a CMake. La GUI richiede il WebUI SDK reale; non viene vendorizzato nel repository.

**Verifica di questa fase:** core e CLI compilati con GCC 14.2, 10/10 test superati. Il sorgente WebUI è stato controllato sintatticamente con un header stub compatibile, ma il WebUI SDK non è installato in questo ambiente e la GUI non è stata compilata né eseguita contro il runtime reale. Anche i backend Assimp/Open CASCADE richiedono test su installazioni native.

---


## Struttura del progetto

Il progetto è stato rinominato **OpenModel** e organizzato secondo una struttura C++ convenzionale:

```text
OpenModel/
├── CMakeLists.txt
├── include/
│   └── OpenModel/       # API pubbliche (.hpp)
├── src/               # implementazioni (.cpp)
├── tests/             # test automatici
└── README.md
```

Gli header pubblici si includono come `#include <OpenModel/MeshData.hpp>`. Il core viene compilato nella libreria statica `OpenModelCore`; il target CMake utilizzabile dai progetti è `OpenModel::Core`.

Configurazione, compilazione e test:

```bash
cmake -S . -B build -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Installazione:

```bash
cmake --install build --prefix <prefix>
```

## Pacchetto CMake installabile (fase 21)

Il progetto installa un package CMake relocatable per il core, inclusi `OpenModelConfig.cmake`, `OpenModelConfigVersion.cmake` e `OpenModelTargets.cmake`. La versione del package è `0.20.0`; la compatibilità di versione usa `SameMajorVersion`. Il target importato è `OpenModel::Core`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --prefix install
```

Esempio di utilizzo da un progetto esterno (anche dopo aver spostato la cartella di installazione):

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApplication LANGUAGES CXX)
find_package(OpenModel 0.20 CONFIG REQUIRED)
add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE OpenModel::Core)
```

Gli header pubblici si includono con `#include <OpenModel/MeshData.hpp>`. Per individuare un'installazione non standard, usare `CMAKE_PREFIX_PATH` oppure `OpenModel_DIR` (la cartella contenente `OpenModelConfig.cmake`). La libreria core non richiede dipendenze native esterne.

CPack genera archivi installabili ZIP e TGZ contenenti libreria, header e file di configurazione CMake. Per generarli: `cpack --config build/CPackConfig.cmake` (su multi-config, specificare anche `-C Release`). Il pacchetto installato include il core; i backend opzionali restano compilabili separatamente ma non sono ancora esportati come componenti binari relocatable, perché richiedono la gestione esplicita delle dipendenze native di ciascun backend.

Esempio di consumer minimo disponibile in `examples/cmake-consumer/`.

## Organizzazione delle implementazioni C++

Le API pubbliche sono dichiarate negli header; i corpi dei metodi sono compilati nei corrispondenti file `.cpp`. Il nucleo è una libreria statica CMake (`openmodel_core`), quindi gli eseguibili devono collegarsi al target invece di includere soltanto gli header. I backend opzionali hanno target statici separati e si compilano solo quando la relativa opzione CMake è attiva.

File principali del core: `MeshData.cpp`, `MeshValidator.cpp`, `MeshTopology.cpp`, `SurfaceMesh.cpp`, `SurfaceRefiner.cpp`, `AbaqusInpWriter.cpp`, `GltfWriter.cpp` e `CADModel.cpp`.

Opzioni dei backend: `OPENMODEL_BUILD_ASSIMP_BACKEND`, `OPENMODEL_BUILD_GMSH_BACKEND`, `OPENMODEL_BUILD_OCC_BACKEND`, `OPENMODEL_BUILD_NETGEN_BACKEND`, `OPENMODEL_BUILD_MFEM_BACKEND` e `OPENMODEL_BUILD_TETGEN_BACKEND`. Le dipendenze native devono essere installate per abilitare ciascun backend.

## Materiali PBR estesi: normal map, metallic-roughness, emissive e trasparenza

Questa fase completa il modello dati dei materiali di rendering e l'esportazione glTF/GLB per i principali slot del workflow metallic-roughness.

### Campi aggiunti

In `RenderSubMesh` e `SurfaceMeshPart` sono disponibili:

- `normalImage`, `normalImageMimeType`, `normalImageName`;
- `metallicRoughnessImage`, `metallicRoughnessImageMimeType`, `metallicRoughnessImageName`;
- `emissiveImage`, `emissiveImageMimeType`, `emissiveImageName`;
- `emissiveFactor` RGB;
- `alphaMode` (`OPAQUE`, `MASK`, `BLEND`), `alphaCutoff` e `doubleSided`.

`SurfaceMesher` trasferisce tutti questi campi senza perdere i metadati del materiale. Il writer incorpora le immagini PNG/JPEG nel file `.gltf` o nel JSON di un `.glb`, crea gli oggetti `images` e `textures` e li collega a `normalTexture`, `metallicRoughnessTexture`, `emissiveTexture` e `pbrMetallicRoughness.baseColorTexture`. Vengono esportati anche i fattori emissivi e i flag di trasparenza/doppia faccia.

### Importazione Assimp

`AssimpModel` prova a leggere normal map, emissive map e la texture `UNKNOWN` (usata da alcuni importer per la mappa metallic-roughness glTF). Importa inoltre fattore emissivo, materiale double-sided e le proprietà alpha mode/cutoff quando sono esposte dal formato e dalla versione Assimp. L'esposizione delle texture dipende dal formato sorgente e dall'importer: le texture di roughness e metalness separate non vengono fuse automaticamente in una singola mappa glTF packed, e il mapping di `UNKNOWN` non è uniforme tra gli importer.

### Validazione e limiti

- Tutte le texture richiedono UV0.
- Il writer accetta solo PNG/JPEG e controlla la firma iniziale e la coerenza del MIME type; non decodifica completamente i file immagine.
- `emissiveFactor` deve essere finito e non negativo; `alphaMode` e `alphaCutoff` vengono validati.
- Le immagini non sono deduplicate: lo stesso file usato da più slot può essere incorporato più volte.
- Le mappe PBR sono trasferite dai backend solo se il formato e l'importer le espongono. La compilazione nativa di Assimp non è stata verificata in questo ambiente.

### Test fase 18

`gltf_writer_test` ora verifica i riferimenti alle quattro texture, `emissiveFactor`, `alphaMode`, `doubleSided` e il rifiuto di un alpha mode non valido. Configurazione e compilazione CMake C++17 completate; **8/8 test core superati**. I backend esterni Assimp, Open CASCADE, Gmsh, Netgen e MFEM non sono stati compilati.

---

## Fase 16 — texture base-color glTF/GLB e buffer portabili

La rappresentazione della superficie e l'esportatore glTF supportano ora immagini di texture base-color incorporate, senza file immagine esterni. `SurfaceMeshPart` e `RenderSubMesh` espongono `baseColorImage` (byte dell'immagine codificata), `baseColorImageMimeType`, `baseColorImageName`, oltre ai fattori PBR. `SurfaceMesher` trasferisce questi metadati dai backend CAD alla mesh di rendering.

### Texture supportate

- MIME type ammessi: `image/png` e `image/jpeg`.
- La validazione controlla la firma iniziale dei byte, la presenza di UV0 e la coerenza del MIME type; non sostituisce un decoder PNG/JPEG e non verifica integralmente la validità del file immagine.
- Le immagini sono incorporate nel JSON come `data:image/png;base64,...` o `data:image/jpeg;base64,...` e collegate a `pbrMetallicRoughness.baseColorTexture`.
- La stessa modalità funziona in `.gltf` e `.glb`; nel GLB il buffer geometrico è nel chunk BIN, mentre le immagini restano URI data incorporati nel chunk JSON.
- Il writer esporta una texture per parte. Il dedup delle immagini identiche non è ancora implementato.

Esempio di impostazione dei dati (i byte devono contenere un file PNG o JPEG reale):

```cpp
SurfaceMeshPart part;
part.name = "housing";
part.positions = {0,0,0, 1,0,0, 0,1,0};
part.texcoords = {0,0, 1,0, 0,1};
part.indices = {0,1,2};
part.baseColorImageMimeType = "image/png";
part.baseColorImageName = "housing-basecolor.png";
part.baseColorImage = pngBytes; // std::vector<unsigned char> letto dal file
```

I backend che popolano `RenderSubMesh` possono fornire gli stessi campi; il trasferimento `RenderSubMesh` → `SurfaceMeshPart` è automatico. L'implementazione non estrae ancora automaticamente immagini e materiali dai file sorgente Assimp/Open CASCADE: in questa fase la sorgente deve valorizzare i campi.

### Correzioni di robustezza

- I float nel buffer glTF vengono ora serializzati esplicitamente in little-endian, come richiesto dal formato, invece di copiare direttamente la rappresentazione nativa della macchina.
- Le normali esplicite non finite vengono rifiutate.
- I test verificano texture PNG, riferimenti `images`/`textures`/`baseColorTexture`, texture in GLB e input non validi.

### Verifiche fase 16

Configurazione e compilazione CMake C++17 completate; **8/8 test core superati**. Inoltre, un controllo separato ha analizzato il JSON generato con un parser JSON e verificato header/versione/lunghezza del contenitore GLB e la presenza del chunk BIN. I backend nativi opzionali (Open CASCADE, Assimp, Gmsh, Netgen, MFEM) non sono stati compilati in questa sessione.

---

## Fase 15 — raffinamento locale conforme, UV/materiali PBR e GLB

Questa fase estende la mesh grafica senza introdurre dipendenze dal solutore FEM.

### Raffinamento superficiale locale

`SurfaceRefiner.hpp` ora marca i singoli spigoli che violano `maxEdgeLength` e/o `maxNormalAngleDegrees`. Ogni spigolo condiviso viene creato una sola volta e tutte le facce incidenti utilizzano lo stesso punto medio; ciò evita T-junction nel singolo `SurfaceMeshPart` senza suddividere uniformemente l'intera parte. La suddivisione locale può produrre 2, 3 o 4 triangoli per triangolo originale a seconda degli spigoli marcati. Il criterio angolare resta una stima basata sulle normali ai vertici, non una misura esatta della curvatura CAD. La geometria viene suddivisa linearmente e non viene proiettata nuovamente sulla superficie CAD.

Il raffinamento interpola anche UV0 se presenti e interpola/normalizza le normali. `maxLevels` continua a limitare il numero di iterazioni e `limitReached` segnala se restano spigoli che superano le soglie.

### Coordinate UV e materiale PBR

`SurfaceMeshPart` ora contiene `texcoords` (UV0, due float per vertice), `baseColorFactor`, `metallicFactor` e `roughnessFactor`. I valori sono validati ed esportati dal writer glTF. Le UV sono importate da Assimp quando il formato sorgente le fornisce e da Open CASCADE quando la triangolazione contiene UV nodes. Le geometrie CAD STEP/IGES non garantiscono che texture o materiali di visualizzazione siano presenti: il framework non ricostruisce automaticamente immagini texture o l'intero materiale CAD.

### Esportazione GLB

`GltfWriter::writeGlbFile("model.glb", surface)` produce un contenitore glTF 2.0 binario con chunk JSON e BIN, senza buffer Base64 esterno. `writeFile` continua a generare un `.gltf` autonomo con buffer incorporato. Posizioni, normali, UV0, indici e fattori PBR vengono esportati; le coordinate e le unità del modello non vengono convertite. Restano fuori dall’ambito della fase 15 il caricamento delle immagini texture, l’alpha mode configurabile e l’importazione completa dei materiali CAD; l’incorporamento delle immagini viene aggiunto nella fase 16.

### Verifiche fase 15

I test core C++17 sono stati compilati con CMake e **8/8 test sono passati**. `gltf_writer_test` copre UV0, fattori PBR, esportazione GLB e controlli di input. È stata inoltre verificata la struttura del contenitore GLB (header, lunghezze, chunk JSON/BIN e intervalli dei buffer view) con un controllo separato. Non sono state compilate le dipendenze native opzionali Open CASCADE, Assimp, Gmsh, Netgen o MFEM in questa sessione; la lettura delle UV dai rispettivi backend va quindi confermata su installazioni native.

---

# OpenModel — fase 14

Fase 7 introduce l'esportazione del modello `MeshData` in formato Abaqus `.inp`, utilizzabile anche con CalculiX per il sottoinsieme di tipi di elemento supportato.

## Nuovo writer

- `AbaqusInpWriter.hpp/.cpp`: writer compilato, senza dipendenze esterne oltre al core.
- Preserva gli ID esterni dei nodi e degli elementi; traduce la connettività dagli indici interni agli ID dei nodi.
- Esporta i blocchi di elementi come `*ELEMENT` con `ELSET` corrispondente.
- Esporta i node set definiti con `MeshData::addNodeToSet()` e `MeshData::addNodeSet()` come `*NSET`.
- Controlla la mesh prima di scrivere e rifiuta ID elemento duplicati, ID pari a zero, coordinate non finite, tipi non supportati e collisioni nei nomi dopo la normalizzazione.
- Scrive prima in memoria e non apre il file di destinazione se la validazione iniziale fallisce.

## Tipi supportati

| `ElementType` | Abaqus type |
|---|---|
| `Triangle3` | `S3` |
| `Triangle6` | `S6` |
| `Quadrilateral4` | `S4` |
| `Quadrilateral8` | `S8` |
| `Quadrilateral9` | `S9` |
| `Tetrahedron4` | `C3D4` |
| `Tetrahedron10` | `C3D10` |
| `Hexahedron8` | `C3D8` |
| `Hexahedron20` | `C3D20` |
| `Prism6` | `C3D6` |
| `Prism15` | `C3D15` |
| `Pyramid5` | `C3D5` |

I tipi lineari 1D e `Hexahedron27` sono rifiutati esplicitamente perché non è garantita una mappatura compatibile con il target Abaqus/CalculiX scelto. Verificare sempre la disponibilità dei tipi di elemento nella versione specifica del solutore.



## Fase 14 — raffinamento superficiale controllabile

`SurfaceRefiner.hpp` aggiunge un raffinamento triangolare indipendente dal backend CAD e dalla mesh volumetrica FEM. Usa suddivisione conforme 1-to-4 e condivide i vertici medi degli spigoli, evitando T-junction all'interno di ciascuna parte.

Parametri:

- `maxEdgeLength`: lunghezza massima desiderata degli spigoli; `0` disabilita il criterio.
- `maxNormalAngleDegrees`: soglia angolare tra normali ai vertici; `0` disabilita il criterio. È un indicatore geometrico approssimato, non una stima esatta della curvatura CAD.
- `maxLevels`: numero massimo di iterazioni. Il report segnala `limitReached` se la soglia è ancora violata.

Per garantire la conformità senza introdurre T-junction, se un triangolo viola un criterio viene suddivisa l'intera parte in quella iterazione. È semplice e robusto, ma può raffinare più triangoli del necessario. La suddivisione non ricostruisce la superficie CAD esatta: inserisce punti medi lineari sugli spigoli. Per oggetti curvi, la qualità iniziale della tessellazione CAD resta quindi importante.

Esempio:

```cpp
#include "SurfaceRefiner.hpp"

SurfaceRefineOptions refineOptions;
refineOptions.maxEdgeLength = 0.5;
refineOptions.maxNormalAngleDegrees = 15.0;
refineOptions.maxLevels = 4;
const auto report = SurfaceRefiner{}.refine(surface, refineOptions);
if (!report.success) {
    // report.error descrive l'input o l'opzione non valida
} else if (report.limitReached) {
    // È stato raggiunto maxLevels prima di soddisfare tutti i criteri
}
```

Il raffinamento conserva i nomi e i metadati delle parti; le normali mancanti vengono generate e quelle interpolate sono normalizzate. UV e materiali non fanno ancora parte di `SurfaceMesh`.

### Test fase 14

`surface_refiner_test` controlla la suddivisione 1-to-4, il calcolo delle normali mancanti, il raggiungimento del limite di livelli e il rifiuto delle opzioni non valide senza modificare la mesh in caso di errore.

## Fase 13 — esportazione glTF per rendering web e desktop

`GltfWriter.hpp` esporta `SurfaceMesh` in un file glTF 2.0 (`.gltf`) autonomo: il buffer binario è incorporato nel JSON come data URI Base64, quindi non è necessario distribuire un file `.bin` separato.

- Un `SurfaceMeshPart` diventa un mesh e un nodo glTF distinti; il nome viene preservato ed escapato correttamente nel JSON.
- Esporta posizioni `float32`, normali `float32` e indici triangolari `uint32`, con buffer views e accessors conformi alla struttura glTF 2.0.
- Se le normali non sono presenti, le calcola accumulando le normali delle facce sui vertici; tutte le normali vengono normalizzate.
- Inserisce bounding box min/max per gli attributi POSITION e un materiale PBR neutro predefinito.
- Controlla mesh vuote, array malformati, coordinate/normali non finite e indici fuori intervallo.
- Non modifica unità, sistema di coordinate o orientamento: il modello è esportato nelle coordinate ricevute.

Esempio:

```cpp
#include "GltfWriter.hpp"

SurfaceMesh surface;
// surface viene riempita da SurfaceMesher::build(...)
const auto result = GltfWriter::writeFile("pawn.gltf", surface);
if (!result.success) {
    std::cerr << result.error << '\n';
}
```

Il file può essere caricato da viewer glTF e da librerie come Three.js. Il writer non converte automaticamente materiali CAD, texture UV o texture immagini: la fase corrente esporta un materiale PBR neutro e la geometria essenziale. L'incorporamento Base64 è comodo per distribuire un singolo file ma aumenta la dimensione testuale rispetto a un `.gltf` con `.bin` separato o a un `.glb` binario.

### Test fase 13

`gltf_writer_test` verifica la generazione del file, il formato glTF 2.0, gli attributi geometrici, il buffer incorporato, l'escaping del nome, il calcolo delle normali mancanti e il rifiuto di input vuoti/non validi.

## Fase 12 — scelta tra mesh di superficie e di volume, controlli di discretizzazione

La libreria ora espone `MeshDimension::{Surface, Volume}` in `MeshingOptions` e introduce una rappresentazione grafica indipendente dal modello FEM:

- `SurfaceMesh.hpp`: `SurfaceMesh` e `SurfaceMeshPart` conservano posizioni, normali, indici triangolari e metadati delle parti CAD, senza imporre elementi volumetrici.
- `SurfaceMesher.hpp` (contenuto in `SurfaceMesh.hpp`): esegue la tessellazione richiesta al `CADModel`, valida gli array geometrici e produce la mesh di superficie. Non genera tetraedri.
- `OpenModel` rifiuta esplicitamente richieste non volumetriche, evitando di generare una mesh 3D quando è stata richiesta una superficie.
- `GmshMesher` sceglie la dimensione 2 o 3 in base a `MeshingOptions::dimension` e applica le opzioni di dimensione e adattamento alla curvatura disponibili in Gmsh.
- `CADModel` espone un overload di `tessellate(linearDeflection, angularDeflection)` compatibile con i modelli preesistenti. `OCCModel` usa entrambi i parametri nella tessellazione Open CASCADE.

### Parametri disponibili

- `dimension`: `Surface` oppure `Volume`.
- `elementSize`: dimensione target legacy per il generatore volumetrico e fallback per le dimensioni Gmsh.
- `linearDeflection`: tolleranza di corda della tessellazione CAD; non equivale a una lunghezza massima del lato triangolare.
- `angularDeflection`: tolleranza angolare in radianti, usata da `OCCModel`.
- `minElementSize` e `maxElementSize`: limiti di dimensione per Gmsh; `maxElementSize == 0` usa `elementSize`.
- `curvatureAdaptation` e `curvaturePointsPerCircle`: attivano e parametrizzano il controllo dimensionale da curvatura in Gmsh.
- `order`: ordine degli elementi per backend che lo supportano.

Esempio di mesh grafica ottenuta da un modello CAD già caricato:

```cpp
MeshingOptions options;
options.dimension = MeshDimension::Surface;
options.linearDeflection = 0.05;
options.angularDeflection = 0.25;
SurfaceMesh surface;
auto report = SurfaceMesher{}.build(cadModel, options, surface);
if (!report.ok) {
    // report.error contiene la causa
}
```

Per una mesh volumetrica con Gmsh, impostare `dimension = MeshDimension::Volume`, `elementSize` e, se necessario, `minElementSize`, `maxElementSize` e i controlli di curvatura. Per la sola superficie con Gmsh, impostare `dimension = MeshDimension::Surface`.

### Limiti importanti

`SurfaceMesher` non effettua un raffinamento triangolare generico dopo la tessellazione: chiede al backend CAD di tessellare e valida/copia il risultato. Con `AssimpModel`, che legge geometrie già triangolate, i parametri di tessellazione non rimodellano la mesh importata. `linearDeflection` e `angularDeflection` sono passati a Open CASCADE; i limiti di dimensione e l'adattamento quantitativo da curvatura sono applicati dal backend Gmsh. Il backend TetGen continua a produrre mesh volumetriche tetraedriche e non applica automaticamente tutti i nuovi parametri di tessellazione. Non è stata eseguita una compilazione nativa dei backend Gmsh/Open CASCADE in questo ambiente.

### Test fase 12

Aggiunto `surface_mesher_test`: verifica la copia dei triangoli e dei metadati, il passaggio dei parametri di tessellazione e il rifiuto di indici non validi e tolleranze errate. Tutti i sei test del core senza dipendenze esterne passano.

## Fase 11 — topologia derivata: facce, spigoli e interfacce materiali

Il nuovo `MeshTopology.hpp` ricava una rappresentazione topologica senza modificare `MeshData`:

- individua facce triangolari e quadrilatere dei volumi supportati;
- raggruppa le facce condivise usando la connettività nodale, anche quando l'orientamento locale è opposto;
- classifica ogni faccia come `Boundary` (un elemento incidente), `Internal` (due elementi) o `NonManifold` (più di due elementi);
- costruisce gli spigoli univoci e conserva gli elementi e le facce incidenti;
- identifica le interfacce materiali interne quando i due elementi sono assegnati a gruppi volumetrici con nomi materiale non vuoti e distinti.

Esempio:

```cpp
MeshTopology topology;
const auto report = topology.build(mesh);
if (!report.success()) {
    // report.errors contiene le ragioni del fallimento
}
for (const auto& face : topology.faces()) {
    if (face.classification == MeshFaceClass::Boundary) {
        // faccia esterna del dominio volumetrico
    }
    if (MeshTopology::isMaterialInterface(face, mesh)) {
        // interfaccia tra materiali nominati differenti
    }
}
```

### Limiti topologici

La fase 11 estrae facce solo da elementi volumetrici lineari `Tetrahedron4`, `Hexahedron8`, `Prism6` e `Pyramid5`. Per evitare risultati fuorvianti, gli elementi volumetrici quadratici e gli altri volumi non supportati producono un errore. L'identità delle facce è basata sugli indici dei nodi di vertice; non vengono generate facce quadratiche con nodi midside. I gruppi materiali sono interpretati solo come metadati: la funzione d'interfaccia richiede che entrambi gli elementi abbiano almeno un gruppo volumetrico con `materialName` e considera interfaccia solo l'assenza di un nome materiale condiviso. Non calcola normali, orientamento geometrico, superfici esterne di mesh non manifold o contatti tra superfici non conformi.

I node set restano gestiti da `MeshData::addNodeToSet()` / `addNodeSet()` e vengono esportati dal writer Abaqus come `*NSET`; la fase 11 li copre assieme alla topologia derivata, senza introdurre un formato di vincoli o condizioni al contorno.

### Test fase 11

`mesh_topology_test` costruisce due tetraedri adiacenti e verifica 6 facce esterne, 1 faccia interna, 9 spigoli unici e il riconoscimento dell'interfaccia tra due materiali. I test non richiedono Gmsh, MFEM, Netgen o OpenCASCADE.

## Fase 10 — trasferimento dei gruppi fisici

### Gmsh

`GmshMesher.hpp` ora importa anche i gruppi fisici definiti nel modello Gmsh:

- interroga i gruppi fisici nelle dimensioni supportate;
- conserva tag e nome del gruppo;
- associa al gruppo gli ID degli elementi appartenenti alle entità Gmsh che lo compongono;
- estrae gli elementi di dimensione massima e quelli di bordo di codimensione uno, così da poter rappresentare gruppi volumetrici e superfici di bordo in 3D, oppure superfici e bordi lineari in 2D;
- se nomi uguali esistono in dimensioni diverse, aggiunge un suffisso dimensione/tag per rispettare l'unicità globale richiesta da `MeshData`.

Le entità geometriche di dimensione inferiore alla codimensione uno non vengono importate come elementi. I gruppi fisici sotto tale dimensione sono quindi ignorati dall'adattatore. L'adattatore continua a richiedere un file CAD importabile tramite il kernel OpenCASCADE di Gmsh; non è un importatore generico di file `.msh`.

### MFEM

`MFEMMesh::createFromData()` gestisce ora anche segmenti lineari come bordi di una mesh 2D (`AddBdrSegment`). Le entità di dimensione inferiore non rappresentabili come elementi di bordo nella dimensione richiesta vengono ignorate; i gruppi fisici di questi elementi non possono quindi essere trasferiti in attributi MFEM. I gruppi sovrapposti della stessa dimensione restano un errore esplicito, perché un singolo attributo MFEM non può codificarli senza perdita.

### Netgen

L'adattatore Netgen continua a generare mesh volumetriche, ma l'API C `nglib` usata qui non espone un'interfaccia generale e stabile per recuperare nomi di gruppi fisici e associazioni originali. La fase 10 non inventa questi metadati: la propagazione dei domini e dei gruppi di bordo Netgen resta da implementare con un'interfaccia Netgen/versione specifica e verificata.

### Test e limiti di verifica

I test senza dipendenze esterne coprono la struttura della mesh, la validazione dei gruppi fisici e l'esportazione Abaqus/CalculiX. La compilazione dell'adattatore Gmsh è stata verificata sintatticamente contro dichiarazioni mock delle API usate, non contro una vera installazione Gmsh; il test d'integrazione Gmsh e il test MFEM non sono stati eseguiti perché le librerie native non sono presenti nell'ambiente. Prima dell'uso in produzione, compilare e provare le opzioni backend contro le versioni effettive delle dipendenze.

## Esempio

```cpp
MeshData mesh;
auto n0 = mesh.addNode(10, 0.0, 0.0, 0.0);
auto n1 = mesh.addNode(20, 1.0, 0.0, 0.0);
auto n2 = mesh.addNode(30, 0.0, 1.0, 0.0);
auto n3 = mesh.addNode(40, 0.0, 0.0, 1.0);
mesh.addElement("Volume", ElementType::Tetrahedron4, 100, {n0,n1,n2,n3});
mesh.addNodeSet("FixedSupport", {n0,n2});
const auto result = AbaqusInpWriter::writeFile("mesh.inp", mesh);
if (!result.success) {
    for (const auto& error : result.errors) std::cerr << error << '\\n';
}
```

Il writer esporta direttamente i keyword nodali, gli elementi e i set, senza introdurre un `*PART` privo della relativa istanza/assembly. Non genera automaticamente materiali, sezioni, vincoli o step di analisi: questi dati non fanno parte del modello mesh e vanno aggiunti in uno strato dedicato alla preparazione del modello di calcolo.

## Build e test

```bash
cmake -S . -B build -DOPENMODEL_BUILD_SMOKE_TEST=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Il test `abaqus_writer_test` verifica ID non consecutivi, connettività, set nodali, set di elementi, mappatura dei tipi e rifiuto di ID elemento duplicati.

## Limiti

- Il modello attuale non conserva ancora gruppi di facce di bordo separati dai blocchi di elementi triangolari/quadrilateri.
- I nomi vengono normalizzati in maiuscolo e caratteri alfanumerici/underscore; collisioni dopo la normalizzazione causano un errore invece di essere risolte silenziosamente.
- Il writer non è un parser `.inp` e non effettua round-trip da file.
- Il test genera e controlla il file, ma non lancia un eseguibile CalculiX: l'integrazione con il solutore va verificata separatamente.

## Fase 8 — adattatore MFEM

La fase 8 rende operativo `MFEMMesh::createFromData(const MeshData&, int)`: costruisce una `mfem::Mesh` in memoria senza file temporanei. L'adattatore resta opzionale e il core non dipende da MFEM.

### Topologie supportate dall'adattatore

- Mesh 2D: elementi lineari `Triangle3` e `Quadrilateral4`.
- Mesh 3D: elementi volumetrici lineari `Tetrahedron4` e `Hexahedron8`.
- Mesh 3D: blocchi `Triangle3` e `Quadrilateral4` importati come elementi di bordo.
- Coordinate finite e indici compatibili con gli indici `int` di MFEM.
- I tipi quadratici e le altre topologie sono rifiutati esplicitamente, non convertiti in modo approssimato.

Gli attributi MFEM sono assegnati per blocco nell'ordine della mesh sorgente. MFEM usa indici di vertice locali e non conserva automaticamente gli ID esterni di `MeshData` né i node set; tali metadati vanno mantenuti separatamente se necessari. La conversione non deduce automaticamente i bordi esterni dai volumi: esporta solo gli elementi triangolari/quadrilateri esplicitamente presenti nei blocchi sorgente.

### Compilazione opzionale

```bash
cmake -S . -B build -DOPENMODEL_BUILD_MFEM_BACKEND=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake cerca un target MFEM esportato dal pacchetto oppure `mfem.hpp` e la libreria `mfem`; se necessario, impostare `MFEM_ROOT`. `mfem_adapter_test` controlla la conversione di una mesh tetraedrica con una faccia di bordo, di una superficie quadrilatera 2D e il rifiuto di una topologia quadratica non supportata.

**Verifica ambiente:** il test MFEM viene compilato ed eseguito solo se MFEM è installato e si abilita `OPENMODEL_BUILD_MFEM_BACKEND`. L'assenza della dipendenza non impedisce di compilare ed eseguire i test del core.

## Fase 9 — gruppi fisici, regioni e attributi

La fase 9 aggiunge un modello comune per raggruppare elementi per dimensione topologica, utile per rappresentare regioni di volume, patch di bordo e insiemi di elementi da trasferire ai backend.

### API nuova

```cpp
mesh.addPhysicalGroup("SteelRegion", 3, 7, {100, 101}, "Steel");
mesh.addPhysicalGroup("FixedFace", 2, 11, {200, 201});
```

- `MeshPhysicalGroup` contiene nome, dimensione topologica (`0..3`), tag positivo, ID degli elementi e nome materiale opzionale.
- I tag sono univoci per dimensione; i nomi dei gruppi sono univoci.
- La validazione controlla che gli elementi referenziati esistano e abbiano la dimensione indicata.
- `materialName` è metadato, non una definizione di proprietà costitutive.
- I gruppi possono contenere elementi di blocchi diversi, purché la dimensione topologica sia coerente.

### Esportazione Abaqus/CalculiX

Il writer genera un `*ELSET` per ciascun gruppo fisico e annota dimensione/tag/materiale nei commenti. I gruppi non sostituiscono le sezioni o le definizioni `*MATERIAL`: queste richiedono un livello successivo dedicato al modello di analisi.

### Conversione MFEM

Se un elemento appartiene a un gruppo fisico della sua dimensione, il tag del gruppo diventa l'attributo MFEM. Per evitare perdita silenziosa d'informazione, la conversione rifiuta un elemento assegnato a più gruppi della stessa dimensione: un singolo attributo MFEM non può rappresentare gruppi sovrapposti. Gli elementi non raggruppati ricevono l'attributo predefinito per blocco.

### Test

`physical_groups_test` controlla la validazione dimensionale e l'esportazione dei set. `mfem_adapter_test` verifica anche il trasferimento degli attributi quando MFEM è disponibile.

**Verifica eseguita in questo ambiente:** build del core e 4 test automatici superati. MFEM non è stato compilato né eseguito in questo ambiente, quindi il test MFEM resta da eseguire su un sistema con la libreria installata.

## Fase 17 — importazione materiali e viewer PBR

Questa fase estende `AssimpModel` e aggiunge un viewer browser per controllare gli export glTF/GLB.

### Materiali Assimp

- Importa il colore base (o, in fallback, il colore diffuse) e i fattori metallic/roughness quando esposti dal formato e dalla versione di Assimp.
- Cerca la texture base-color e ripiega sulla texture diffuse.
- Carica immagini PNG/JPEG esterne risolvendo i percorsi relativi rispetto alla cartella del modello.
- Legge texture incorporate compresse (`aiTexture` con `mHeight == 0`); il MIME type viene determinato dall'hint o dalla firma dei byte.
- Ignora in modo controllato texture non supportate/non leggibili; non converte le texture incorporate non compresse (pixel grezzi) in PNG.
- Applica le trasformazioni gerarchiche dei nodi alle posizioni e la matrice inversa-trasposta alle normali. Le UV restano invariate.
- Espone l'errore dell'ultimo caricamento tramite `CADModel::getLastError()`.

Il supporto ai materiali dipende dalle informazioni effettivamente presenti nel file e dal formato importato. Non tutti i formati hanno un modello PBR equivalente a glTF; non vengono ancora importate normal map, metallic-roughness map, emissive map, alpha mode o sampler personalizzati.

### Viewer WebGL

Aprire `examples/webgl-viewer/index.html` tramite un server HTTP locale, quindi selezionare o trascinare un file `.glb`/`.gltf`. Il viewer usa Three.js con illuminazione, tone mapping e controlli orbitali; le texture incorporate nel GLB vengono gestite dal loader glTF. La pagina importa Three.js da jsDelivr e quindi necessita di connessione Internet. Per uso offline occorre servire localmente i moduli Three.js.

Esempio di server locale dalla cartella del progetto:

```sh
python -m http.server 8000
```

Aprire quindi `http://localhost:8000/examples/webgl-viewer/`.

### Limiti di verifica

La suite core non richiede Assimp e continua a testare solo componenti indipendenti dalle dipendenze esterne. La compilazione nativa di `AssimpModel` e la verifica visiva in un browser dipendono dall'ambiente e non sono dichiarate superate se non vengono eseguite esplicitamente. I percorsi texture esterni sono letti dal filesystem locale; non sono risolti URL remoti.

## Fase 22 — CLI, libreria statica/condivisa e GUI WebUI

Il progetto fornisce tre prodotti distinti:

- `OpenModel::Core`: libreria statica (`libOpenModelCore.a` su Linux; estensione equivalente sulle altre piattaforme);
- `OpenModel::Shared`: libreria condivisa (`libOpenModel.so` su Linux, `.dylib` su macOS e DLL su Windows);
- `openmodel`: applicazione a riga di comando, installata in `bin`.

Entrambe le librerie sono compilate dagli stessi sorgenti core. La libreria condivisa abilita l'esportazione automatica dei simboli su Windows tramite CMake. La CLI usa il target statico per semplificare l'esecuzione senza configurare il percorso della libreria dinamica.

### CLI

```text
openmodel info
openmodel validate-demo
openmodel demo-inp output.inp
openmodel demo-gltf output.gltf
openmodel demo-glb output.glb
```

I comandi `demo-*` generano modelli dimostrativi, non importano un modello CAD arbitrario. Il comando `demo-inp` usa un tetraedro volumetrico; i comandi glTF producono una superficie triangolare minima. Questi esempi consentono di verificare il collegamento alla libreria e i writer installati.

### GUI desktop WebUI

La GUI è opzionale per mantenere il core e la CLI privi di dipendenze UI. È basata sull'SDK WebUI (header C `webui.h` e libreria WebUI) e include pulsanti per creare una mesh dimostrativa, visualizzare lo stato, validare la mesh ed esportare i formati Abaqus/CalculiX e GLB.

Per abilitarla, installare il WebUI SDK e configurare il percorso d'installazione:

```bash
cmake -S . -B build \
  -DOPENMODEL_BUILD_WEBUI=ON \
  -DOPENMODEL_WEBUI_ROOT=/path/to/webui
cmake --build build --target openmodel-webui
```

Su Windows, `OPENMODEL_WEBUI_ROOT` deve indicare una distribuzione WebUI compatibile con l'architettura e il runtime usati per compilare OpenModel. L'applicazione salva `openmodel-demo.inp` e `openmodel-demo.glb` nella directory di lavoro del processo. La GUI non è compilata per default e non viene distribuita nei pacchetti core, perché dipende dal binario WebUI scelto dall'utente.

### Build e installazione

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOPENMODEL_BUILD_CLI=ON -DOPENMODEL_BUILD_SHARED=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
```

Per produrre soltanto la libreria statica e la CLI, impostare `-DOPENMODEL_BUILD_SHARED=OFF`. Il package CMake installato esporta `OpenModel::Core` e, se la libreria condivisa è stata costruita, anche `OpenModel::Shared`. Per un'applicazione che desidera collegarsi alla shared library, usare `target_link_libraries(app PRIVATE OpenModel::Shared)`.

La GUI usa il binding WebUI `webui.call(...)` per invocare callback C++ registrate con `webui_bind`. Non è stato possibile compilare o eseguire la GUI in questo ambiente perché l'SDK WebUI non è installato; il core statico, il core condiviso e la CLI sono invece compilati e verificati.


## Fase 24 — generazione volumetrica CAD

Quando `OPENMODEL_BUILD_GMSH_BACKEND=ON` è attivo, la GUI WebUI espone il comando **Genera mesh volumetrica CAD**. Il comando usa il backend Gmsh per importare il file CAD dalla sorgente corrente, genera elementi 3D di ordine 1 o 2 con dimensione minima/massima configurabile, valida il risultato e lo rende disponibile per l'esportazione Abaqus/CalculiX INP. Senza Gmsh, il comando restituisce una diagnostica e la demo tetraedrica resta utilizzabile.

È disponibile anche `openmodel mesh-volume <file.step> <output.inp> <max-element-size> [order]` nella CLI quando il backend Gmsh è compilato. Il comando CLI è limitato ai formati CAD leggibili da `gmsh::model::occ::importShapes`; i formati triangolati Assimp non sono automaticamente sorgenti valide per il mesher volumetrico.

Esempio build: `cmake -S . -B build -DOPENMODEL_BUILD_GMSH_BACKEND=ON -DOPENMODEL_BUILD_WEBUI=ON -DOPENMODEL_WEBUI_ROOT=/path/to/webui-sdk`.


## Gmsh SDK: Windows precompilato e selezione Debug/Release

Il backend Gmsh può usare direttamente l'SDK/binari precompilati ufficiali su Windows; non è necessario compilare Gmsh dai sorgenti. In CMake è disponibile la cache `GMSH_INCLUDE_DIR`, che deve puntare alla cartella contenente `gmsh.h`, oltre a `GMSH_ROOT` per indicare la radice dell'archivio SDK estratto.

Esempio con un SDK precompilato che contiene `include\gmsh.h`, `lib\gmsh.lib` e `bin\gmsh.dll`:

```powershell
cmake -S . -B build-gmsh -G "Visual Studio 17 2022" -A x64 `
  -DOPENMODEL_BUILD_GMSH_BACKEND=ON `
  -DGMSH_ROOT="C:/SDK/gmsh" `
  -DGMSH_INCLUDE_DIR="C:/SDK/gmsh/include" `
  -DGMSH_LIBRARY_RELEASE="C:/SDK/gmsh/lib/gmsh.lib"
cmake --build build-gmsh --config Release
```

Se si dispone anche di una libreria di debug separata, impostare `GMSH_LIBRARY_DEBUG` al relativo `.lib`; CMake userà quella per `Debug` e `GMSH_LIBRARY_RELEASE` per `Release`. Le configurazioni `RelWithDebInfo` e `MinSizeRel` vengono mappate a `Release`. Se si dispone di una sola variante, questa viene usata per tutte le configurazioni.

```powershell
-DGMSH_LIBRARY_DEBUG="C:/SDK/gmsh/lib/Debug/gmshd.lib" `
-DGMSH_LIBRARY_RELEASE="C:/SDK/gmsh/lib/Release/gmsh.lib"
```

Per i pacchetti Windows basati su DLL, OpenModel cerca automaticamente `gmsh.dll`/`libgmsh.dll` sotto `bin` e copia la DLL accanto agli eseguibili generati. Se la DLL ha un nome o una posizione differente, specificare `GMSH_RUNTIME_LIBRARY_DEBUG` e/o `GMSH_RUNTIME_LIBRARY_RELEASE`. Se il runtime non viene trovato, l'applicazione deve poter trovare la DLL tramite `PATH`.

`GMSH_INCLUDE_DIR`, `GMSH_LIBRARY_DEBUG`, `GMSH_LIBRARY_RELEASE` e i percorsi delle DLL sono cache variable modificabili anche da CMake GUI/Visual Studio. Se non vengono forniti percorsi espliciti, OpenModel prova prima i target esportati dai package CMake Gmsh, poi la ricerca manuale sotto `GMSH_ROOT`.

## Fase 25 — verifica d'integrazione

Il test `gmsh_integration_test` è stato esteso per verificare importazione di un fixture STEP, generazione di mesh volumetriche lineari e quadratiche, mesh di superficie, validazione geometrica e scrittura INP. L'integrazione reale viene eseguita solo se si configura `OPENMODEL_BUILD_GMSH_BACKEND=ON` con Gmsh e supporto Open CASCADE disponibili. La suite core è stata eseguita in Release con 10/10 test superati. Il pacchetto CMake è stato installato e verificato con consumer esterni che collegano sia `OpenModel::Core` sia `OpenModel::Shared`.

## Fase 26 — Gmsh SDK e configurazioni multi-config

Il backend Gmsh supporta percorsi espliciti per gli header, la libreria di Debug, la libreria di Release e le DLL runtime. Questo permette di usare l'SDK Windows precompilato senza ricompilare Gmsh. In assenza di librerie distinte, la variante disponibile viene riutilizzata per tutte le configurazioni. La configurazione del core non richiede Gmsh.


## Fase 28 — verifiche

I test di validazione includono orientamento dei tetraedri, elementi degeneri, coordinate non finite, ID duplicati, qualità tetraedrica, angolo minimo triangolare, nodi ripetuti, nodi non utilizzati e opzioni non valide.

## Fase 35 (v0.34.0) — misure RSS del processo nel benchmark

Il benchmark ora riporta, oltre alla stima delle capacità dei vettori, snapshot della memoria residente del processo (RSS) dopo costruzione della mesh, costruzione della topologia e validazione, più il picco RSS del processo. Su Linux usa `/proc/self/statm` per l'RSS corrente e `getrusage` per il picco; su Windows usa `GetProcessMemoryInfo`; su macOS riporta il picco disponibile tramite `getrusage`, mentre l'RSS corrente non è disponibile e resta zero. Il picco è cumulativo per l'intero processo e per tutte le ripetizioni: non è un picco isolato per fase. Gli snapshot sono misure di processo, non attribuzioni precise alle singole strutture, e possono variare con allocatore, sistema operativo e build.

Esempio: `openmodel-benchmark 16 7`. Confrontare le nuove misure RSS solo su stesso sistema, build e carico; usare `estimated_retained_capacity_bytes` per la stima del payload delle capacità e RSS per il consumo residente osservato, senza confondere le due metriche.

## Fase 36 (v0.35.0) — profilazione del traffico di allocazione

Il benchmark ora misura anche, per costruzione della mesh, costruzione della topologia e validazione, il numero mediano di chiamate a `operator new`/`operator new[]` e i byte cumulativi richiesti. Queste metriche rendono visibile il traffico di allocazione temporaneo che non emerge dalla sola stima delle capacità mantenute. La sonda è limitata al benchmark e non cambia l'API della libreria.

Limiti: i contatori intercettano le allocazioni C++ ordinarie visibili tramite `new`/`new[]`, ma non le allocazioni dirette via `malloc`, allocatori personalizzati o tutte le allocazioni interne del runtime. I byte sono cumulativi e non rappresentano memoria trattenuta né picco RSS. Il benchmark stampa la mediana per ogni fase; confrontare versioni solo con stessa piattaforma, compilatore, configurazione e dimensione della griglia.

## Phase 37 — OpenModel 0.36.0

- Cache immutable volume-face templates per supported element type and return them by const reference. This removes repeated construction/copying of nested template vectors when topology is rebuilt or meshes contain multiple blocks of the same type.
- Reserve a small expected adjacency capacity for each edge before inserting incident element IDs, reducing vector growth/reallocation when edge incidence increases.
- Preserve the public `MeshFace`/`MeshEdge` representation and topology semantics.
- The benchmark's allocation probe can quantify cumulative `new`/`new[]` requests for the topology stage. Compare multiple repetitions and distinguish cumulative allocation traffic from retained memory and RSS.



## Phase 38 — topology adjacency allocation tuning (v0.37.0)

The edge-to-element incidence vector now reserves six entries when a unique edge is first created, instead of four. In conforming tetrahedral grids, edges commonly touch more than four volume elements; this avoids the frequent capacity growth step from four to eight while avoiding a worst-case reserve based on all face occurrences. The public API and edge incidence semantics are unchanged.

Validation: dependency-free core test suite and synthetic tetrahedral-grid benchmark. Allocation counters count ordinary C++ `new`/`new[]` requests; RSS is process-level and allocator-dependent. Results should be compared on the same machine and build.


## Phase 39 — physical surface lookup sizing (v0.38.0)

The surface-face lookup now reserves buckets based on the number of explicit surface elements that are actually referenced by dimension-2 physical groups, rather than every surface element in the mesh. This reduces avoidable hash-table capacity for meshes where physical groups cover only a subset of the imported surface. The lookup keeps an explicit load-factor policy and the public topology API is unchanged.

Validation: the dependency-free core test suite and synthetic topology benchmark should be run for the target build. The change is most beneficial when many explicit surface elements are not assigned to physical groups; meshes without such surface elements may show no measurable difference.

Esempio benchmark I/O (build con `-DOPENMODEL_BUILD_BENCHMARKS=ON`):

```bash
openmodel-io-benchmark 16 5
```

Risultati indicativi osservati in Release con GCC 14.2 su Linux x86-64 (5 ripetizioni, griglie sintetiche; non sono confronti con un formato testuale):

| Griglia | Nodi | Elementi | File MGMB | Scrittura mediana | Lettura mediana |
|---|---:|---:|---:|---:|---:|
| 8³ | 729 | 3.072 | 97.406 byte | 0,336 ms | 0,258 ms |
| 16³ | 4.913 | 24.576 | 747.390 byte | 1,389 ms | 1,511 ms |

Questi numeri misurano il round-trip MGMB e il filesystem locale in quell'ambiente. Non consentono ancora di affermare quanto sia più veloce rispetto a un parser testuale: per quello serve un confronto controllato con un writer/reader testuale equivalente.
