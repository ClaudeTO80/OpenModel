# Convenzioni di lavoro

## Codice

- **Standard**: C++17 (`cxx_std_17` in CMake). Non usare funzionalità C++20 o C++23 (per esempio `std::map::contains`, `std::expected`) finché il progetto non alza lo standard: CONTRIBUTING.md lo dice esplicitamente. Le preferenze personali di chi sviluppa indicano C++23 con `std::expected`: quando le due cose non coincidono, vale il progetto, e la scelta va dichiarata nella descrizione della modifica.
- **Lingua**: codice, commenti e messaggi di log in inglese; commenti in stile Doxygen (`/** ... */`, `@param`, `@return`, `@brief`).
- **Header pubblici** in `include/OpenModel/`, implementazioni in `src/`. Un backend opzionale resta dietro la sua opzione `OPENMODEL_BUILD_*_BACKEND`.
- **Errori**: le funzioni di meshatura riportano l'errore con `lastError()` e `fail(...)`, non con eccezioni verso la GUI. La GUI risponde sempre con `reply(e, false, msg)`.
- **Gmsh è globale**: `GmshMesher` usa un mutex e una sessione (`GmshSession`); le opzioni di Gmsh sono globali e vanno impostate dentro la sessione. Quando un tentativo fallisce e se ne prova un altro, pulire la mesh con `gmsh::model::mesh::clear()` prima di riprovare.
- **Compatibilità**: il formato MGMB/MGWV resta compatibile (`docs/MGMB_FORMAT.md`, `docs/MGWV_FORMAT.md`); dati nuovi in blocchi opzionali.

## Test

- Ogni cambiamento di comportamento ha un test (CONTRIBUTING.md). Test in `tests/*.cpp` con `assert` o con una funzione `require` che lancia un'eccezione.
- Quando un test fallisce, stabilire prima se è il test o il codice a essere sbagliato, e dirlo nella descrizione.
- Un test che non dimostra il caso che dice di coprire va corretto: per il fallback della ricombinazione una barra semplice non fallisce, quindi il test di quella forma non verificava nulla e va sostituito con un caso reale.
- Dichiarare cosa **non** è stato verificato, per esempio la compilazione di `main.cpp` senza SDK WebUI.

## Flusso di consegna

- Il repository su GitHub è accessibile in lettura. Le modifiche si consegnano come **patch** su `main` (`git format-patch`), applicabili con `git am`.
- Commit con i trailer di attribuzione richiesti dal progetto:

  ```text
  Co-Authored-By: Claude Haiku 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01LeJWDQt4zkdCrDNNpcxwUe
  ```

  Per le descrizioni di pull request, il trailer è `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- Prima di una patch: `git fetch origin` e rigenerare la patch sopra `origin/main`, perché altri interventi (per esempio la richiesta JSON) possono cambiare le stesse righe. Una patch che non applica su `origin/main` non va consegnata.
- Non committare le build (`/build*/` è in `.gitignore`).

## Documentazione

- `CHANGELOG.md`: aggiungere una voce in `[Unreleased]` per ogni cambiamento visibile (Keep a Changelog). Oggi le modifiche recenti alla mesh tetraedrica e al fallback **non** hanno ancora una voce: da fare.
- `docs/DEVELOPMENT_LOG.md` è storico (nomi e numerazioni vecchi): non aggiornarlo per le nuove modifiche.
- `README.md`: la sezione «Gmsh meshing controls» deve restare allineata con `MeshingOptions`.
- Questo file e gli altri in `docs/context/` sono per il contesto degli assistenti: aggiornarli quando cambiano l'architettura, le convenzioni o lo stato dei problemi.
