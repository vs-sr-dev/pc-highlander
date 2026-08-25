# 01 — Inventario del materiale

## 1.1 Il dump del sorgente

Origine: **"Contents of 1.44M floppy — Matt's Backup — 13 Jul 95"** (`Info.txt`).
È un backup su floppy della postazione di lavoro di Matthew Jesson, non un
export ordinato del repository di progetto.

Struttura (identica in `Home/` e `Home2/`, verificata con `diff -rq`: **i due ZIP
sono bit-identici**, stesso contenuto, stesso timestamp — uno dei due è ridondante):

```
DISK1/        6 file   — file di lavoro dell'editor di mappe
HIGH/       152 file   — l'albero di sviluppo del gioco
```

Totale ≈ **47.000 righe** di sorgente.

| Estensione | File | Righe | Cos'è |
|---|---:|---:|---|
| `.S` | 20 | 9.296 | assembly 68000 (Motorola/Madmac) |
| `.GAS` | 26 | 23.109 | assembly **GPU** Jaguar (Tom, RISC) |
| `.DAS` | 3 | 754 | assembly **DSP** Jaguar (Jerry, RISC) |
| `.INC` | 26 | 12.570 | header + **dati** (modelli 3D, tabelle) |
| `.MAC` | 3 | 1.279 | macro (script VM, item, utility) |
| `.SCT` | 3 | ~350 | script di gioco (linguaggio proprietario) |
| `.TXT` | ~45 | — | note di design, appunti, TODO fra sviluppatori |
| `.RGB`/`.TGA` | 4 | — | bitmap (font, barra vita, schermata pausa) |
| `.LIB` | 2 | 17 | stub delle librerie Cinepak (68k + GPU) |

### Date dei file
Da dicembre 1994 a **13 luglio 1995**. Il gioco è uscito a fine 1995: questo è
uno snapshot **work-in-progress**, non il build finale. Conseguenza pratica
importante → i *block offset* in `CDLINK.INC` (generati il 4 luglio 1995) **non
sono garantiti corrispondere** al disco retail.

### Cosa NON c'è (e serve)

| Mancante | Gravità | Note |
|---|---|---|
| `jaguar.inc`, `cd.inc`, `blit.inc`, `gpu.inc` | bassa | header standard dell'SDK Atari Jaguar, pubblicamente reperibili |
| **Map Editor / Map Tool** (M. Jesson) | media | genera `CDLINK.INC`, `WORLD.INC`, i file `.MAP` e il layout del CD |
| **SKELSKIN.EXE** (C. Lowe & M. Jesson) | media | convertitore modelli 3D Studio → formato Jaguar |
| 43 dei 45 file `.MAP` | media | presenti solo `DUN1.MAP` e `DUN2.MAP` |
| scene 3D Studio (`.3DS`) dei fondali | alta | i fondali sono renderizzati offline; recuperabili solo dal CD |
| **tutti i dati binari del gioco** | — | vivono sul CD (vedi doc 04) |
| il compilatore degli script (`.SCT` → bytecode) | media | ma il linguaggio è interamente documentato in `OPCODES.INC` + `SCRIPT.MAC` |

### Note di design incluse (miniera d'oro)
`NOTES.TXT`, `CACHE.TXT`, `VOLUMES.TXT`, `PCOL.TXT`, `SMOOTH.TXT`, `SAVE.TXT`,
`NVRAM2.TXT`, `COLLIDE*.TXT`, `COMBAT*.TXT`, `SCRIPT*.TXT`, `THOUGHTS.TXT`,
`TODO.TXT`, `BUGSNOTE.TXT` — descrivono in inglese discorsivo gli algoritmi di
collisione, combattimento, salita scale, caching delle scene, salvataggi.
Da leggere prima di riscrivere i moduli corrispondenti.

---

## 1.2 Le due immagini disco

| File | Dimensione | Verdetto |
|---|---:|---|
| `Highlander - The Last of the MacLeods (USA).jcd` | 456.126.464 B | **buona** |
| `Highlander.jcd` | 427.819.008 B | **rovinata — da scartare** |

### Perché la seconda è inutilizzabile
Scansionata byte per byte: su 427 MB solo **22,3 MB non sono zero**, distribuiti
in 12 blocchi di esattamente 2 MB allineati a confini di 2 MB:

```
0x02A00000..0x02C00000   0x03600000..0x03800000   0x05A00000..0x05C00000
0x06A00000..0x06C00000   0x09000000..0x09200000   0x0E600000..0x0E800000
0x11400000..0x11600000   0x13C10000..0x13E00000   0x17600000..0x176A0000
0x176B0000..0x17800000   0x18600000..0x18800000   0x19600000..0x19800000
```

Il pattern (solo l'ultimo buffer di ogni finestra di lettura è stato scritto)
è la firma di un rip fallito. Non ha nemmeno l'header `JCD` né la firma `ATRI`
del boot Jaguar CD. **Risposta alla domanda "perché hanno dimensioni diverse":
non sono due versioni, una è semplicemente corrotta.**

### La ISO buona
* Header container: magic ASCII `JCD\0` a offset 0, seguito da una tabella di
  **9 record da 12 byte** (una per traccia) con timestamp MSF e offset dei dati
  nel file. I dati della traccia 2 iniziano a `0x0000BBEE` e sono chiaramente
  PCM 16 bit.
* Firma `ATRI` (header di boot Jaguar CD) presente a `0x017A5A7E`.
* La decodifica completa dell'header `JCD` e la mappatura tracce → *data type*
  è il primo lavoro della sessione 2 (vedi doc 04).
