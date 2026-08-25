# Sessione 1 — 25 agosto 2026

**Obiettivo:** conoscenza del materiale e analisi preliminare. Nessun codice.

## Fatto

* Inventariato il dump del sorgente: floppy da 1.44 MB, "Matt's Backup",
  13 luglio 1995. 158 file, ~47.000 righe.
* Verificato che `Home/` e `Home2/` (e i due ZIP corrispondenti) sono
  **bit-identici**: uno dei due e' ridondante.
* Ricostruita l'architettura del motore: 68000 orchestratore, GPU che esegue
  tutta la logica in overlay da 4 KB, DSP per l'audio.
* Documentate tutte le strutture dati principali (WST / ACT / CIT / DDA,
  character sheet, modelli, animazioni, set, mesh di collisione, `.MAP`).
* Documentato per intero il linguaggio di script e i suoi 60+ opcode.
* Ricostruito lo schema di indirizzamento del CD: `(data type, block offset)`
  con data type usato come indice di traccia relativo alla seconda sessione.
* **Chiarito il mistero delle due ISO:** `Highlander.jcd` (427 MB) e' un rip
  corrotto — 22 MB di dati reali su 427, in 12 blocchi da 2 MB allineati, resto
  zeri, niente header `JCD`, niente firma `ATRI`. Usare solo la USA da 456 MB.
* Iniziata la decodifica dell'header container `JCD` (9 record di traccia).
* Creato il repo `pc-highlander` con politica BYOA stretta.

## Deciso

* C99 + SDL3 + CMake, rasterizer software.
* Fedelta' all'originale come priorita', migliorie solo come flag.
* Il sorgente 1995 resta locale, fuori dal repo.

## Aperto

1. **Header `JCD`** non ancora decodificato con certezza. Nove tracce contro
   tredici data type: da capire se il retail organizza i dati diversamente, se
   piu' data type condividono una traccia, o se la lettura dell'header e'
   ancora imprecisa.
2. **Formato della scena** (110 blocchi): suddivisione fra fondale, Z-buffer ed
   eventuale header ancora ignota.
3. **CRY vs RGB16 con VARMOD**: da determinare sperimentalmente.
4. Da recuperare gli header dell'SDK Atari mancanti (`jaguar.inc`, `cd.inc`,
   `blit.inc`, `gpu.inc`) — servono solo come riferimento per costanti hardware,
   non per il build.

## Prossima sessione

Fase 1 della roadmap: aprire il container `.jcd`, mappare le tracce, e
verificare se gli offset di `DATA.INC` (4 luglio 1995) valgono sul disco
retail. Test decisivo: `BO_CINEPAK_TITLES = 83298` deve puntare a un header
`FILM`.
