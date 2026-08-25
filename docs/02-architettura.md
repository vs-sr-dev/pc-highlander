# 02 — Architettura del motore

## 2.1 I tre processori

Il Jaguar ha un 68000 a 13,3 MHz e due RISC a 26,6 MHz (GPU "Tom", DSP "Jerry").
Highlander li usa così:

| Processore | Ruolo |
|---|---|
| **68000** | orchestratore. Non fa lavoro pesante: gestisce il game loop, chiama il CD BIOS (le uniche routine che *devono* girare sul 68k), sposta parametri, riavvia il gioco al game-over. |
| **GPU** | **tutto il gioco**. Motore 3D, animazione, collisioni, combattimento, IA, eventi di scena, VM degli script, controller CD, testo. 23k righe di codice GPU. |
| **DSP** | mixer audio PCM (`WAVE.DAS`), lettura joypad (`JOYPAD.DAS`), audio dei filmati (`CINEDSP.DAS`). |

Vincolo brutale: la GPU ha **4 KB di RAM interna**, condivisa fra codice, stack
e variabili. Perciò il motore è spezzato in circa venti "overlay" che vengono
**blittati dentro la GPU uno alla volta** e concatenati.

## 2.2 Il kernel GPU (`KERNEL.GAS`)

Il kernel vive stabilmente in cima alla GPU RAM (ultimi 268 byte) e non viene
mai sovrascritto. In fondo alla GPU RAM ci sono 6 long di parametri
(`PARAM1`..`PARAM6`); i primi 400 byte sono riservati alle routine CD.

Protocollo:

1. la GPU gira in loop leggendo `PARAM6`;
2. il 68000 scrive in `PARAM6` l'indirizzo in RAM principale di un blocco
   `[dest_addr.l][size.l][codice...]`;
3. il kernel usa il **blitter** per copiare quel codice nella GPU e ci salta
   dentro;
4. il modulo, finito, riazzera `PARAM6` e torna al kernel.

Il 68000 dal canto suo chiama `kernwait` (aspetta che `PARAM6` torni 0).

> Per il port questo diventa banalmente una sequenza di chiamate a funzione.
> Tutta la coreografia di overlay e blitter **non va riprodotta**: era un
> workaround per i 4 KB.

## 2.3 Il game loop (`MAIN.S`, etichetta `Gameloop`)

```
Gameloop:
  setvars
  loop:
    input()      ; fase 1
    update()     ; fase 2
    goto loop
```

**`input()` — in ordine:**

| Passo | Modulo GPU | Cosa fa |
|---|---|---|
| 1 | `SCRIPTCODE` | esegue un tick di tutti i processi della VM script |
| 2 | `EVENT` | verifica i cerchi-evento della scena contro la posizione del giocatore; può lanciare cinepak / redbook / cambio camera / cambio personaggio |
| 3 | (68k) | `cineplayer` se un Cinepak è stato richiesto |
| 4 | (68k) | `cdloader`: coda richieste CD, gestisce lo switch fra modalità dati e Red Book |
| 5 | `EV2` | seconda passata eventi per i caricamenti immediati |
| 6 | `NEWSCENE` / `CHARNEWSCN` | se la scena è cambiata: ricostruzione della draw list, precaricamento delle scene vicine, eventuale cambio di *set* |
| 7 | (68k) | `readpad`, `movemod`, `movemo2`, `dispfrd` (joypad verso intenzione di movimento) |

**`update()` — in ordine:**

| Passo | Modulo GPU | Cosa fa |
|---|---|---|
| 1 | `ANIMCODE` | avanza le animazioni, applica gli spostamenti radice, risolve la collisione con la mesh di collisione, calcola l'altezza del terreno |
| 2 | `PPCOLL` | collisioni personaggio-personaggio e risoluzione del combattimento |
| 3 | `ENGINE` | rasterizzazione 3D dei personaggi nel back buffer con Z-buffer |
| 4 | `BITMAP` | bitmap 3D (sprite con valore Z) sopra la scena |
| 5 | `TEXTS` | testo e HUD |
| 6 | `COLLECT` | logica di raccolta oggetti e swap dei buffer a fine frame |

## 2.4 Video

* Risoluzione **320x200, 16 bit** (`VIDSTUFF.INC`).
* `VMODE = $6C7` — RGB16 con bit **VARMOD** attivo: il framebuffer è misto
  **CRY / RGB16** pixel per pixel (vedi `CRYRGB.TXT`, dove si discute
  esplicitamente la resa dello shading nelle due modalità).
* Doppio buffer (`buff1`/`buff2`, 320 KB l'uno inclusi Z e dati) più tre buffer
  di scena (`Scenea`/`Sceneb`/`Scenec` con i rispettivi `ZBuffa`/`ZBuffb`/`ZBuffc`)
  per il caching.
* NTSC **e** PAL supportati (`VIDINIT.S`): timing 60/50 Hz, la variabile
  `framerate` è usata come moltiplicatore nelle animazioni, quindi la logica è
  già indipendente dal frame rate per costruzione.
* Barra della vita: 40 pixel in basso (`BARHEIGHT`).

## 2.5 Il motore 3D (`3DENGINE.GAS`, 3.414 righe)

L'autore ha lasciato la documentazione di progetto nell'header del file.
In sintesi:

* Sistema di coordinate destrorso, interi 16 bit (s15.0 per le posizioni,
  s1.14 per le matrici di rotazione).
* Matrici **3x3 più traslazione** (niente 4x4: l'autore scrive che gli otto tick
  in più per moltiplicazione non valevano la pena).
* Pipeline per poliedro: copia matrice istanza, concatena con la matrice di
  vista, trasforma i vertici, proiezione prospettica; poi per ogni faccia:
  trasforma la normale, **backface culling** (Z minore o uguale a 0), clipping
  2D, **illuminazione flat** (un colore per faccia), scan-conversion col
  **blitter** scrivendo bitmap **e** Z-buffer.
* Illuminazione: **ambiente più 4 luci puntiformi** (RGB, X, Y, Z ciascuna,
  18 long in tutto).
* Bit di status per modello: bit 0 = invisibile, bit 2 = salta illuminazione
  (i personaggi resi volutamente "cartoony" non sono illuminati, con un
  risparmio notevole).
* Limiti: meno di 128 vertici e meno di 255 facce per modello, massimo 32
  vertici per faccia.

`FORMMAT.GAS` costruisce a ogni frame le matrici di istanza dalle tre rotazioni
(ordine: **Y azimuth, poi X elevation, poi Z twist**).

`3DBITMAP.GAS` aggiunge bitmap a 16 bit con un valore Z, fuse nella scena.

## 2.6 Composizione con il fondale

È il cuore visivo del gioco: il fondale caricato dal CD porta con sé il proprio
**Z-buffer pre-calcolato** dalla scena 3D Studio originale. Il motore copia
fondale e Z nei buffer di lavoro (`BlitZCopy` in `LIBRARY.S`) e poi disegna i
personaggi col normale test di profondità. Risultato: i personaggi passano
correttamente **dietro** agli elementi del fondale, a costo zero di geometria.

## 2.7 Mappa della memoria (2 MB, da `VIDSTUFF.INC`)

Dall'alto verso il basso:

```
$200000  top
         stack 1K, buffer CD 148K
         buff1  320K  (back buffer 1 + Z + dati)
         buff2  320K  (back buffer 2 + Z + dati)
         scratch 32K + scratch2/nvram 16K
         buff4/buff5/zbuff  384K  (3 buffer di scena + Z)
         vlist/vlist2/origsave/svlist  6K
         snd        128K   campioni audio
         modelspc   128K   modelli
         animspc    196K   animazioni
         charlgcs    48K   logiche personaggio
         bitmaps     32K   bitmap 3D        <- inizio area a chunk
         setdata     32K+  dati del set
         cdq        1.25K  coda CD
         activchar   16K   Active Character Table
         chartbl      8K   Character Instance Table
         draw_data_area      4K
         instance_data_area 16K
         ws / cs     16K + 16K  world state / character sheet
         ...codice 68000 e copie residenti di codice GPU/DSP
```

L'area gestita dinamicamente usa **chunk da 448 byte** con una word di
management per chunk (bit alto = libero, resto = indice del chunk
"proprietario"). Garbage collection a ogni cambio di set. Vincolo documentato:
i dati dei character sheet devono stare nei primi 700 chunk.

## 2.8 Audio

* **`WAVE.DAS`** — mixer PCM sul DSP, 16 voci (`waveEntries`), API a comandi
  (`wavePlay`, `waveStop`, `waveSetVolume`, ...) documentata in `NOTES.TXT`.
* **Red Book** — il parlato è audio CD vero e proprio, indirizzato per block
  offset su una traccia dedicata (`BO_CDAUDIO_LINE005` fino a `LINE100`).
  Il `cdloader` deve commutare fra modalità dati e modalità audio, ed è la
  parte più delicata del codice 68k.
* **Cinepak** — audio a 22.252 Hz in ingresso risampionato a 21.867 Hz
  (`CINEPAK.INC`); c'è persino una costante di *drift* per la sincronizzazione.
* Tre volumi separati (sfx / background / CD) regolabili dal menu pausa
  (`VOLUMES.TXT`).

## 2.9 Filmati (`CINEPAK.S`, `CINEDSP.DAS`, `CINELIST.GAS`)

* Codec **Cinepak**, contenitore **FILM** di Atari: chunk `FILM`, `FDSC`,
  `CTAB`, `STAB`, header da 64 byte, sync `'1111'`.
* 320x240 a 16 bit, letti in streaming dal CD a blocchi da 2352 byte.
* Il player commuta `VMODE` fra RGB16 e CRY16 a seconda del filmato.
* Nota per il port: è lo stesso formato gestito dal demuxer `segafilm`
  (`film_cpk`) di FFmpeg, e il codec Cinepak è supportato ovunque. Non serve
  riscrivere nulla da zero.

## 2.10 File del sorgente, per ruolo

**68000**
`HIGH1.S` (entry point, init video), `MAIN.S` (game loop, variabili globali,
tabelle), `INTSERV.S` (interrupt), `VIDINIT.S` (NTSC/PAL), `JLISTER.S` (object
list), `CLEARJAG.S` / `ZAP.S` (clear memoria), `JOY.S` (joypad, pausa, volumi,
cheat), `GPU.S` / `LIBRARY.S` (interfaccia GPU, `BlitZCopy`), `CDLOADER.S`
(loop CD lato 68k), `NVRAM.S` (salvataggi), `CINEPAK.S` (player FMV),
`OBJECT.S`, `SHEET.S` (character sheet), `ITEMS.S`, `WORLD.S`, `INITTBL.S`,
`LETTERS.S` (font), `CBAR.S` (gradiente barra vita).

**GPU**
`KERNEL.GAS`, `3DENGINE.GAS`, `3DBITMAP.GAS`, `FORMMAT.GAS`, `ANIM.GAS` (+
`COLLIDE.GAS`, `FINDTRI.GAS` inclusi), `COMBAT.GAS`, `AICTRL.GAS`, `EVENT.GAS`,
`SCNLOGIC.GAS`, `SETLOGIC.GAS`, `SCENEPR.GAS`, `COLLECT.GAS` (+ `CSHCODE.GAS`),
`SHOWT.GAS` (testo), `SCRIPT.GAS` (VM), `CDCONTRO.GAS` (controller CD),
`GPUCTRL.GAS`, `CINESTUB.GAS`, `ROOTER.GAS` (test radice quadrata),
`ANDY.GAS` (versione precedente di `ANIM.GAS`, tenuta come riferimento).

**DSP**
`WAVE.DAS`, `JOYPAD.DAS`, `CINEDSP.DAS`.

**Disassemblati di codice Atari** (non scritti da Lore)
`CDINIT.GAS`, `CDINITM.GAS`, `CDINITF.GAS`, `CINELIST.GAS` — listati prodotti
col debugger (`Db: lg <indirizzo>`) del CD BIOS e della libreria Cinepak.
