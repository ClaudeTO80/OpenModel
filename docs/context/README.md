# Contesto per sviluppatori e assistenti (OpenModel)

Questi file riassumono il progetto per chi lo modifica con l'aiuto di un assistente, così che una nuova sessione non debba rileggere tutto il repository. Sono una guida, non la specifica: per i formati e la fisica vale la documentazione in `docs/` e `docs/index.html`; per il codice vale il codice.

| File | Quando leggerlo |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Prima di toccare un modulo: mappa dei sorgenti, tipi principali, dipendenze. |
| [BUILD_AND_TEST.md](BUILD_AND_TEST.md) | Prima di compilare o di lanciare test: opzioni CMake, etichette, ambiente Gmsh e WebUI. |
| [MESHING_GMSH.md](MESHING_GMSH.md) | Per qualsiasi lavoro sulla mesh: fasi di `GmshMesher::buildMesh`, struttura, impostazioni tetraedriche, fallback. |
| [WEBUI.md](WEBUI.md) | Per modifiche alla GUI o ai suoi callback: contratto della richiesta di mesh, limiti di WebUI. |
| [CONVENTIONS.md](CONVENTIONS.md) | Prima di scrivere codice o consegnare modifiche: stile, test, patch e commit. |
| [STATUS.md](STATUS.md) | Per sapere cosa è verificato, cosa no e quali problemi sono aperti. |

Regola generale: un'affermazione su un comportamento va verificata con il codice o con un test eseguito, non con questi file, che possono invecchiare. Quando un file contraddice il codice, il codice è corretto e il file va aggiornato.
