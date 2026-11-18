# Stato: cosa è verificato e cosa no

Aggiornato al 18 novembre 2026, sul ramo `main` (commit `b72cedf`). Ogni voce indica la prova disponibile.

## Verificato (compilato ed eseguito)

- Core e test di `ctest -L core`, incluso `gmsh_settings_test` (parser delle impostazioni, valori validi e rifiutati).
- Gmsh 4.15.2 con i test `gmsh_structured_test` e `gmsh_integration_test` (linea, quadratici, superficie).
- Fallback della ricombinazione: su IPN100 L1000, con driver che usa la libreria compilata. Algoritmo 3 fallisce a 4, 5, 6 e 8; con il fallback la mesh si completa.
- Impostazioni tetraedriche: su OilPan a dimensione 4, HXT (`alg3D=10`) porta la qualità minima a 0,299 contro 0,072 del default (metrica `computeMeshQuality`).
- JavaScript di `gui.html`: sintassi controllata con Node.

## Non verificato

- **`main.cpp` (backend WebUI)**: non compilato, perché l'SDK WebUI non è presente nell'ambiente di sviluppo. Le modifiche al parsing della richiesta JSON e alla chiamata `applyUnstructuredGmshSettings` sono state controllate solo sul diff.
- **Comportamento della pagina GUI** (campi, visibilità, invio della richiesta): non provato nel browser.
- **Fisica dei risultati FEM** con le mesh prodotte: nessun solutore reale eseguito sulle mesh tetraedriche dei modelli in `models/`.
- **Mesh tetraedriche per FEM**: la qualità è misurata, ma non ci sono risultati di analisi che la colleghino a un'accuratezza.
- Il test della ricombinazione di Blossom full-quad (fallback) non dimostra ancora il caso di fallimento (vedi `CONVENTIONS.md`).

## Problemi aperti

1. **Mesh strutturata di geometrie complesse**: `Helical Bevel Gear.STEP` e `OilPan.stp` non sono né blocchi né prismi (150 e 126 facce, superfici B-spline, coniche, sferiche, toroidali). La meshatura strutturata pura richiede la suddivisione in sottovolumi (per esempio `occ::twist` per i denti elicoidali), lavoro non iniziato. Oggi questi modelli vanno in tetraedri.
2. **Nodi fuori dalla superficie curva**: segnalato per il bevel gear, ma non riprodotto con gmsh standalone (deviazione massima 8·10⁻⁵ mm). Serve la riproduzione con i parametri e il log dell'app. Esiste anche la possibilità che il problema venga dal percorso strutturato e non dalla tetraedrica.
3. **Sliver nell'oil pan**: con le impostazioni di default restano tre elementi con qualità sotto 0,10 a dimensione 4. L'ottimizzazione (attiva di default) porta la qualità minima da 0,014 (senza) a 0,072; con HXT non resta alcun elemento sotto 0,10 nella misura fatta.
4. **Metrica di qualità**: la GUI usa `computeMeshQuality`, mentre le valutazioni fatte in conversazione usavano `gamma` di Gmsh. I valori non sono confrontabili; per le soglie FEM serve scegliere una sola metrica e documentarla.
5. **Subdivisione (`Mesh.SubdivisionAlgorithm = 2`)**: produce elementi invertiti sull'oil pan (minimo dello Jacobiano −55,8). Non è esposta in GUI e non va esposta senza un controllo di qualità.
6. **Mesh strutturata con Blossom full-quad**: fallisce su contorni con un numero dispari di segmenti. Il fallback risolve nel caso misurato; la causa (numero di segmenti di bordo non pari) non è corretta alla radice.
7. **Prismi nel risultato con Blossom**: a dimensioni grandi il fallback produce anche prismi (2004 su IPN a dimensione 6). Non è stata valutata la qualità di questi elementi.
8. **CHANGELOG** senza voci per le modifiche recenti (fallback, impostazioni tetraedriche).
9. **Doppio standard**: il progetto usa C++17, le preferenze di sviluppo indicano C++23 (vedi `CONVENTIONS.md`).
10. **Patch 0004 e funzionalità WebUI**: la richiesta JSON è stata integrata; restano da riportare su `main` le altre callback posizionali, che il file dichiara da estendere.

## Storico rilevante

- `390ea5d`: richiesta di mesh come oggetto JSON (patch 0004).
- `b4aea4a`: fallback Blossom → Simple nella ricombinazione automatica.
- `b72cedf`: impostazioni tetraedriche esposte in GUI e gestite nel backend.
