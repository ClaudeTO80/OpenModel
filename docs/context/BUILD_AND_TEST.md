# Compilazione e test

## Build di base (core, test, CLI)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

I test C++ usano `assert` e sono compilati senza `NDEBUG` anche in Release (`-UNDEBUG`).

## TK4CPP

Con `OPENMODEL_TK4CPP_SOURCE=FETCH` (default) CMake scarica TK4CPP da `OPENMODEL_TK4CPP_GIT_REPOSITORY` al tag `OPENMODEL_TK4CPP_GIT_TAG` e compila solo Process, Encode, Utility e Thread, in versione statica. Con `OPENMODEL_TK4CPP_SOURCE=INSTALLED` si usa un TK4CPP già installato, cercato in `OPENMODEL_TK4CPP_DIR` o in `CMAKE_PREFIX_PATH`. `process_runner_test` e `web_base64_test` usano TK4CPP: vanno eseguiti in entrambe le modalità. Istruzioni complete nel README, sezione TK4CPP.

Opzioni CMake principali (default tra parentesi):

- `OPENMODEL_BUILD_TESTS` (ON), `OPENMODEL_BUILD_CLI` (ON), `OPENMODEL_BUILD_SHARED` (ON), `OPENMODEL_BUILD_FATIGUE` (ON), `OPENMODEL_BUILD_BENCHMARKS` (OFF).
- Backend opzionali, spenti per default: `OPENMODEL_BUILD_GMSH_BACKEND`, `OPENMODEL_BUILD_OCC_BACKEND`, `OPENMODEL_BUILD_ASSIMP_BACKEND`, `OPENMODEL_BUILD_TETGEN_BACKEND`, `OPENMODEL_BUILD_NETGEN_BACKEND`, `OPENMODEL_BUILD_MFEM_BACKEND`.
- `OPENMODEL_BUILD_WEBUI` (OFF): richiede l'SDK WebUI, vedi sotto.
- `OPENMODEL_TEST_WEBGL_VIEWER` (OFF): test di browser, richiede Node.js con Playwright.
- `OPENMODEL_VERSION` (0.1.5): versione del pacchetto.

## Gmsh

```bash
cmake -S . -B build -DOPENMODEL_BUILD_TESTS=ON -DOPENMODEL_BUILD_GMSH_BACKEND=ON \
      -DGMSH_INCLUDE_DIR=/usr/local/include \
      -DGMSH_LIBRARY_RELEASE=/usr/local/lib/libgmsh.so.4.15 -DGMSH_LIBRARY_DEBUG=/usr/local/lib/libgmsh.so.4.15
```

- Si può anche usare `GMSH_ROOT` (prefisso con `include/gmsh.h`, `lib/`, `bin/`).
- L'API C++ di Gmsh deve venire da una build dello stesso compilatore; il controllo `OPENMODEL_GMSH_LINK_CHECK` lo verifica in configurazione.
- Test Gmsh: `gmsh_settings_test` (nessuna dipendenza da Gmsh, è nel gruppo core), `gmsh_structured_test`, `gmsh_integration_test`, `gmsh_assembly_test`, `gmsh_step_face_mapping_test`, `gmsh_posthoc_face_mapping_test`.
- Verificato in questo ambiente: Gmsh 4.15.2 installato con `pip`; la libreria C++ è `/usr/local/lib/libgmsh.so.4.15` con header in `/usr/local/include`. Il wheel Python richiede in più `libglu1-mesa`, `libxft2`, `libxrender1`, `libxcursor1`, `libxinerama1`, `libxfixes3`.

## WebUI (GUI desktop)

- `src/webui/main.cpp` e `gui.html` si compilano solo con l'SDK WebUI: `-DOPENMODEL_BUILD_WEBUI=ON -DOPENMODEL_WEBUI_ROOT=/percorso/sdk`.
- In ambienti senza SDK (come quello di sviluppo usato finora) il backend **non viene compilato**: le modifiche a `main.cpp` vanno verificate sulla macchina con l'SDK. Il JavaScript di `gui.html` si può controllare con Node: estrarre i `<script>` e fare `node --check`.
- `gui.html` e il viewer sono incorporati nel binario tramite CMake (`gui_html.inc`, `viewer_html.inc`).

## Verifiche consigliate per tipo di modifica

| Modifica | Verifica minima |
|---|---|
| Parser, tipi, utilità del core | `ctest -L core` |
| Mesher Gmsh | `gmsh_structured_test`, `gmsh_integration_test`, e un driver o un test sul modello reale |
| Opzioni tetraedriche | `gmsh_settings_test` e un run sul modello con `MeshQuality` |
| GUI / callback | `node --check` sullo script, e build con l'SDK WebUI |
| Viewer | `tests/webgl` con Playwright (se disponibile) |

## Note pratiche

- Un driver di prova si linka contro `libopenmodel_gmsh.a` e `libOpenModelCore.a` della build, più `libgmsh.so`. I driver usati finora sono stati compilati con `-std=c++23`; il progetto è C++17, quindi un driver da tenere come riferimento va ricompilato con `-std=c++17`.
- Le build in `build/` sono locali e non vanno committate (vedi `.gitignore`).
