# 04 — Layout del CD e piano di estrazione asset

## 4.1 Come il gioco indirizza i dati

Il Jaguar CD **non ha filesystem**: si legge per timecode MSF assoluto. Highlander
costruisce sopra questo un indirizzamento a due livelli:

```
(data type, block offset)  ->  timecode MSF  ->  CD_read
```

* Il **data type** (0-12, vedi doc 03 §3.8) è un **indice di traccia relativo**.
  Commento testuale in `CDCONTRO.GAS`:
  `;N.B. TRACK OFFSET FOR DATA == DATA TYPE`
* Il **block offset** è lo scostamento in blocchi dall'inizio di quella traccia.

La conversione avviene in `GetTrack` (`CDCONTRO.GAS`, riga ~2210):

1. scorre la **TOC** del CD BIOS (`CD_toc`), saltando il primo record, finché
   il primo byte del secondo long di un record vale 1 — cioè finché trova
   **la prima traccia della seconda sessione**;
2. `record = TOC_base + data_type * 8`, da cui legge il timestamp MSF di quella
   traccia;
3. converte MSF in blocchi: `min*60*75 + sec*75 + frame`;
4. somma il block offset richiesto;
5. sottrae un **fudge factor di 4** blocchi (il commento cita i 150 blocchi di
   silenzio e 6 blocchi di pre-read);
6. riconverte in MSF e lo passa a `CD_read`.

**Conseguenza pratica per il port:** ci serve solo, per ciascun data type, il
**settore iniziale assoluto** della traccia corrispondente. Da lì tutti i
`BO_*` di `DATA.INC` e `CDLINK.INC` diventano indici diretti.

## 4.2 La coda CD

`CDCONTRO.GAS` (GPU) e `CDLOADER.S` (68k) implementano una coda circolare da 64
entry (1 KB, `cdq`), 16 byte per entry:

```
cdqState     .w   UNLOADED $01 / LOADING $02 / LOADED $04 / PROCESSED $08
                  WAIT $10 / PRIORITY_WAIT $80
cdqTrack     .w   = data type
cdqBlock     .l   block offset
cdqBuffer    .l   (solo per le scene)
cdqBufferPtr .w   1 = a, 2 = b, 3 = c
cdqEntry     .w   posizione nel sheet, per il post-processing
cdqSheet     .w   sheet in cui registrare il riferimento
cdqReserved  .w
```

Due puntatori (`currcd`, `entercd`) girano nel buffer circolare. Le richieste
possono essere promosse a *priority wait* quando la scena serve subito.

Il `cdloader` gestisce anche la commutazione fra **modalità dati** (`CD_mode` 3,
doppia velocità) e **Red Book** (`CD_mode` 0, singola velocità + `CD_jeri`).

## 4.3 Le tabelle di offset già in nostro possesso

`DATA.INC` e `CDLINK.INC` sono file **generati** dal Map Tool il 4 luglio 1995 e
contengono tutti gli offset simbolici:

| Tabella | Entry | Esempio |
|---|---:|---|
| `BO_LOGICS_*` | 5 | `BO_LOGICS_1 = $0` (completa), `BO_LOGICS_3B = $40` (solo stand) |
| `BO_MODEL_*` | 26 | `BO_MODEL_QUENTIN = $0`, passo $10 |
| `BO_ANIM_*` | 38 | include le animazioni "forzate" delle cutscene |
| `BO_SOUND_*` | 25 | passi ambientali per superficie, ambienti, versi |
| `BO_CDAUDIO_LINE*` | 88 | dialoghi parlati, `LINE005 = 150` ... `LINE100 = 26653` |
| `BO_CINEPAK_*` | 20 (+6 alias) | `BUZZ 0`, `TITLES 83298`, `TURRET 89842` |
| `BO_SET_*` | 45 | passo $10 |
| `BO_SCENE_*` | 594 | passo $6e = 110 blocchi per scena |
| `SCENE_*` | 594 | numero logico di scena, non offset |

**110 blocchi per scena** è la dimensione fissa di un fondale con Z. Da
verificare in sessione 2 se sono blocchi da 2048 o 2352 byte, e come sono
suddivisi fra immagine, Z-buffer ed eventuale header (una scena 320x200 a 16 bit
occupa 128.000 byte per l'immagine; 110 blocchi da 2048 fanno 225.280 byte, che
è compatibile con immagine + Z parzialmente compresso o a risoluzione ridotta).

**Attenzione:** questi offset sono dello snapshot 4 luglio 1995, non del disco
retail. Vanno **verificati** contro la ISO, non assunti.

## 4.4 L'immagine disco

Usare **solo** `Highlander - The Last of the MacLeods (USA).jcd` (456 MB).
L'altra è un rip corrotto (doc 01 §1.2).

Header container:

```
offset 0x00  "JCD\0"
offset 0x04  00 00 01 09     (probabile: sessioni / numero di tracce)
offset 0x08  02 2B 1D 1D
offset 0x0C  9 record da 12 byte, uno per traccia, primo byte = numero traccia
```

Record letti (byte grezzi):

```
tr 1: 01 000000 00 02132F 00000003
tr 2: 02 02132F 01 00012D 0000BBEE
tr 3: 03 02192D 01 002340 0000BE13
tr 4: 04 030324 01 10192E 0000EE51
tr 5: 05 131F09 01 001831 00061CC5
tr 6: 06 13393C 01 001C1D 00063DF0
tr 7: 07 141C11 01 162934 00066421
tr 8: 08 2B0B46 01 000724 000D8CB8
tr 9: 09 2B1521 01 000043 000D96C6
```

L'ultima colonna è l'**offset dei dati della traccia nel file**: a `0x0000BBEE`
c'è chiaramente PCM 16 bit, coerente con una traccia audio. La firma `ATRI`
del boot Jaguar CD è a `0x017A5A7E`.

**Nove tracce contro tredici data type**: o il disco retail organizza i dati
diversamente dal build di luglio, o alcune tracce contengono più data type, o
la mia lettura dell'header è ancora imprecisa. È la prima cosa da chiarire.

## 4.5 Piano per la sessione 2

Obiettivo: **passare da "ISO opaca" a "asset estratti e visualizzabili"**.

1. **Decodificare l'header `JCD`** in modo definitivo (tabella tracce, mapping
   nel file, dimensione del settore per traccia). Riferimento incrociato: il
   formato è quello letto da BigPEmu, e la struttura si valida controllando che
   ogni offset di traccia dichiarato cada su dati plausibili.
2. **Localizzare la seconda sessione** e mappare `data type -> settore assoluto`.
3. **Validare gli offset di `DATA.INC`** su un asset facile da riconoscere:
   `BO_CINEPAK_TITLES = 83298` sulla traccia dei Cinepak deve dare un header
   `FILM`. Se la firma cade dove previsto, l'intera tabella di luglio '95 è
   valida anche per il retail — risultato enorme, ci regala tutti i nomi.
4. **Estrattore Cinepak** — il più semplice: contenitore FILM, decodifica con
   FFmpeg per verifica visiva.
5. **Estrattore modelli** — validato contro gli 11 modelli in chiaro nel
   sorgente (`MERLOT79.INC` e compagni): parsando dal CD la bottiglia di vino
   dobbiamo ottenere gli stessi vertici.
6. **Estrattore scene** — capire i 110 blocchi, tirare fuori fondale e Z-buffer,
   scriverli come PNG (immagine) e PNG a 16 bit / PGM (profondità).
7. **Estrattore set** — collisioni, eventi, punti di start, tabella scene.
8. **Estrattore animazioni**, **sheet**, **suoni**, **Red Book**.

Ogni estrattore va in `tools/`, scrive in `assets/` (ignorato da git) e produce
un manifest JSON con nomi presi da `CDLINK.INC`/`DATA.INC`/`WORLD.INC`.
