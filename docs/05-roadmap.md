# 05 — Strategia di porting e roadmap

## 5.1 Decisioni prese (sessione 1)

| Decisione | Scelta |
|---|---|
| Linguaggio | **C99** |
| Piattaforma | **SDL3** (video, audio, input), build con **CMake** |
| Rendering | **rasterizer software**, framebuffer 320x200 a 16 bit + Z-buffer, upscale via texture SDL |
| Fedelta' | **prima fedele, poi opzioni** — il gioco originale e' il riferimento; migliorie solo come flag disattivabili |
| Repo | **BYOA stretto** — nessun sorgente 1995, nessuna ISO, nessun asset |

### Perche' rasterizer software
Il motore originale disegna facce **flat-shaded** con test Z contro uno Z-buffer
caricato dal disco. Riprodurlo in software significa avere il *ground truth*
pixel per pixel: se un frame non combacia con l'originale in emulatore, il bug
e' nostro. Il costo computazionale e' irrilevante — parliamo di poche centinaia
di poligoni per frame su 64.000 pixel. L'accelerazione GPU resta un'opzione
futura, sopra un motore gia' corretto.

## 5.2 Cosa NON va portato

Grossa parte del sorgente originale esiste solo per aggirare i limiti
dell'hardware. Va letta per capire *cosa* fa, non trascritta:

* il kernel di overlay GPU e tutta la coreografia col blitter (§2.2);
* l'allocatore a chunk da 448 byte e la sua garbage collection;
* la coda CD a stati con priority-wait e la commutazione dati/Red Book;
* il triplo buffer di scena con precaricamento — su PC gli asset stanno tutti
  in RAM;
* i disassemblati del CD BIOS e della libreria Cinepak (`CDINIT*.GAS`,
  `CINELIST.GAS`);
* il decoder Cinepak scritto a mano: contenitore FILM piu' codec standard.

Restano invece **da riprodurre fedelmente**: aritmetica intera e formati in
virgola fissa (s15.0 / s1.14 / 8.8), ordine delle operazioni nel game loop,
semantica esatta della VM script, algoritmi di collisione e combattimento,
curve di animazione e tweening.

## 5.3 Roadmap per fasi

### Fase 0 — Analisi (sessione 1) — FATTO
Inventario, architettura, formati, layout CD documentati.

### Fase 1 — Il disco si apre
Decodifica del container `.jcd`, mappatura data type -> traccia -> settore,
verifica che gli offset di `DATA.INC` valgano anche sul disco retail.
Tool: `tools/jcd/` (lettore container + dump tracce).
**Criterio di riuscita:** `BO_CINEPAK_TITLES` punta a un header `FILM` valido.

### Fase 2 — Gli asset escono
Estrattori per cinepak, scene (fondale + Z), modelli, animazioni, set,
character sheet, suoni, Red Book. Output in `assets/` con manifest JSON
nominato secondo `CDLINK.INC` / `DATA.INC` / `WORLD.INC`.
**Criteri di riuscita:** i fondali si aprono come PNG; il modello della
bottiglia estratto dal CD coincide con `MERLOT79.INC` del sorgente.

### Fase 3 — Si vede qualcosa
Finestra SDL3, framebuffer 320x200, conversione CRY/RGB16 -> RGB888, un
visualizzatore che mostra un fondale con il suo Z-buffer e ci fa ruotare dentro
un modello estratto, illuminato e Z-testato.
**Criterio di riuscita:** il modello passa correttamente dietro agli elementi
del fondale.

### Fase 4 — Il mondo esiste
Porting delle strutture dati (WST, ACT, CIT, DDA, character sheet), del
caricamento dei set, della mesh di collisione e della ricerca del triangolo
(`FINDTRI`), del movimento del personaggio con altezza del terreno e gestione
scale (`SMOOTH.TXT`). Camera fissa che cambia attraversando le linee-evento.
**Criterio di riuscita:** si cammina in `DUN1` e la camera cambia dove deve.

### Fase 5 — Il gioco si muove
Sistema di animazione con tweening, collisioni personaggio-personaggio,
combattimento (`PCOL.TXT`), IA (`AICTRL.GAS`), raccolta oggetti e inventario.
**Criterio di riuscita:** ci si batte con un Hunter e uno dei due muore.

### Fase 6 — Il gioco racconta
VM degli script (i 60+ opcode), compilatore `.SCT` (per rigenerare gli script
dal sorgente disponibile e per debug), eventi di scena, world state bit,
trigger dei filmati.
**Criterio di riuscita:** il menu principale funziona e parte l'intro.

### Fase 7 — Il gioco suona e parla
Mixer PCM a 16 voci, Red Book dalle tracce audio della ISO, playback Cinepak
sincronizzato, tre volumi separati.

### Fase 8 — Rifinitura
Salvataggi, menu pausa, HUD e barra vita, font, schermata crediti,
gestione NTSC/PAL, opzioni moderne come flag.

## 5.4 Struttura del repository

```
docs/          documentazione tecnica (questo materiale)
tools/         estrattori e utility, C99, indipendenti dal motore
  jcd/         lettore del container .jcd
  extract/     estrattori per data type
src/
  main.c       game loop
  game/        WST / ACT / CIT / set / scene / eventi
  anim/        animazione, tweening, collisioni, combattimento
  r3d/         rasterizer flat + Z-buffer
  script/      VM
  media/       Cinepak, Red Book, PCM
  platform/    SDL3
assets/        (gitignore) output degli estrattori
Highlander/    (gitignore) materiale originale dell'utente
```

## 5.5 Rischi noti

| Rischio | Mitigazione |
|---|---|
| Gli offset di luglio '95 non valgono sul disco retail | Fase 1 li verifica subito; in caso negativo si ricostruisce la mappa per scansione delle firme (header `FILM`, header modelli) |
| Formato scena/Z-buffer non documentato nel sorgente | I 110 blocchi per scena sono un vincolo forte; validazione visiva su fondali riconoscibili |
| Il sorgente e' un WIP, non il codice della release | Va trattato come **specifica di progetto**, non come oracolo. Dove diverge dal disco, vince il disco |
| Mancano i tool di conversione asset (Map Tool, SKELSKIN) | Non servono: leggiamo i dati gia' convertiti dal CD. Servirebbero solo per rigenerare contenuti nuovi |
| CRY vs RGB16 con VARMOD | Da chiarire sperimentalmente sui fondali estratti; `CRYRGB.TXT` e le costanti colore dei modelli danno riferimenti incrociati |
