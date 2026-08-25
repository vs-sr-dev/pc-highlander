# Sessione 2 — Fase 1: aprire il disco

**Obiettivo:** decodificare il container `.jcd`, mappare le tracce, verificare
se gli offset di `DATA.INC` (luglio 1995) valgono sul disco retail.

## Fatto

* **Container `.jcd` decodificato** per intero: header da 12 byte, tabella di
  9 record da 12 byte, offset delle tracce in unita' da 512 byte.
* **Trovato lo schema di codifica dei dati:** ogni long e' memorizzato a byte
  invertiti. E' il passaggio che sbloccava tutto — senza, il contenuto sembra
  rumore. Individuato notando testo leggibile ma scombinato e ricostruendolo
  fino a ottenere francese in chiaro.
* **Struttura di traccia dati identificata:** 64 byte di lead-in `ATRI`, 32 di
  `"ATARI APPROVED DATA HEADER ATRI"` piu' un byte di tipo, 64 di tag di
  contenuto, poi il payload. Il byte di tipo e' `0x20 + indice traccia`, cioe'
  il *data type* del gioco.
* **Layout del disco retail mappato:** 1 traccia audio + 8 tracce dati.
  Traccia 4 (`PICT`, 166 MB) = scene; traccia 7 (`1111`, 229 MB) = Cinepak;
  traccia 6 = campioni audio.
* **Verificato il punto zero:** `BO_CINEPAK_BUZZ = 0` cade esattamente
  sull'header `FILM` del primo filmato. La formula
  `offset = inizio_traccia + 160 + blocco * 2352` e' confermata al byte.
* Scritto [tools/jcd/jcdinfo.py](../../tools/jcd/jcdinfo.py): elenca le tracce,
  le estrae gia' de-swappate, fa dump esadecimale.

## Risposta alla domanda "quanto e' vicino il codice di luglio al gioco spedito"

**Meno di quanto speravamo, sul lato dati.** Tre riscontri:

1. **Data type da 13 a 8.** Categorie fuse fra luglio e ottobre.
2. **Filmati da 20 a 36.** Tutti `cvid` 320x240. Solo il primo e' rimasto al
   suo block offset; tutti gli altri si sono spostati.
3. **Sparita la traccia Red Book del parlato.** A luglio 88 battute su una
   traccia audio dedicata (fino al blocco 26.653). Nel retail l'unica traccia
   audio e' lunga 10.472 blocchi: troppo corta. Il parlato e' altrove.

In piu': il disco retail ha testo **francese e tedesco**, di cui nel sorgente di
luglio non c'e' traccia. E' un gioco multilingua che a luglio non lo era ancora.

**Il motore, pero', resta valido.** Quello che e' cambiato e' il *confezionamento*
dei dati, non l'architettura: `GetTrack` funziona esattamente come descritto nel
sorgente, i blocchi sono da 2352 byte, il formato FILM dei Cinepak e' quello di
`CINEPAK.INC`, il tag della traccia 7 e' letteralmente il `sync_header equ '1111'`
dichiarato nel sorgente. Il codice di luglio e' una **specifica affidabile del
motore**; le tabelle di offset non lo sono piu'.

## Aperto

1. Identificare le tracce 3, 5, 8, 9 (`DATA` generico).
2. Confermare la dimensione della scena: 73.921 blocchi / 110 = 672,009, quindi
   l'ipotesi "672 scene da 110 blocchi" e' molto probabile ma va verificata
   visivamente.
3. Ricostruire la corrispondenza nome-filmato per i 36 Cinepak: i 20 nomi di
   `DATA.INC` sono in ordine alfabetico, il che aiuta ma non basta.
4. Capire dove sia finito il parlato.
5. Chiarire la codifica del framebuffer (CRY vs RGB16 con VARMOD) sui fondali.

## Prossima sessione

Fase 2: estrattori. Priorita' alle scene — sono il contenuto piu' pesante e
quello che rende il gioco riconoscibile a colpo d'occhio. Primo traguardo:
un fondale aperto come PNG con il suo Z-buffer accanto.
