# 06 — Il container `.jcd` e il layout del disco retail

Ricavato per reverse engineering sull'immagine USA. Tool di riferimento:
[tools/jcd/jcdinfo.py](../tools/jcd/jcdinfo.py).

---

## 6.1 Header del container

```
offset  dim  campo
0x00     4   magic "JCD\0"
0x04     3   riservato (00 00 01)
0x07     1   numero di tracce
0x08     4   02 2B 1D 1D   (lead-out, 43:29:29)
0x0C    12*N tabella tracce
```

Record di traccia, 12 byte:

```
+0   1   numero di traccia (1-based)
+1   3   MSF di inizio        min, sec, frame — byte binari, non BCD
+4   1   flag: 0 = audio, 1 = dati
+5   3   lunghezza MSF        stessa codifica
+8   4   offset dei dati nel file, in unita' da 512 byte (big endian)
```

Il campo di offset va **moltiplicato per 512**. Gli MSF di inizio includono i
pregap (~152 blocchi fra una traccia e l'altra) che nel file **non** sono
memorizzati: per navigare nel file si usa solo l'offset, mai l'MSF.

## 6.2 La scoperta chiave: i long sono byte-invertiti

**Tutti i dati delle tracce dati sono memorizzati con ogni gruppo di 4 byte
in ordine inverso.** Senza questo passo il contenuto sembra rumore con
frammenti di testo mescolati.

```c
for (i = 0; i + 3 < n; i += 4) {
    swap(buf[i], buf[i+3]);
    swap(buf[i+1], buf[i+2]);
}
```

Come e' stato trovato: attorno a `0x17A5A7E` compariva testo leggibile ma
scombinato (`...LEROK ~NATRIAFNU EOR E...`). Invertendo i long si legge
`... A KORTAN~FAIRE UNE RONDE A L'EXTERIEUR...` — testo francese in chiaro.

> Effetto collaterale: la firma `ATRI` che si trova cercando ingenuamente nel
> file **non e' quella vera** — e' una coincidenza a offset non allineato. La
> firma reale, una volta invertiti i long, sta a `0x177DC00`, cioe' esattamente
> all'inizio dei dati della traccia 2 (`48110 * 512 = 24.632.320`). E' stata
> anche la conferma definitiva dell'unita' da 512 byte.

Il testo trovato e' **francese e tedesco** — le descrizioni degli oggetti di
gioco. Il disco USA e' multilingua, cosa di cui non c'e' traccia nel sorgente
di luglio.

## 6.3 Struttura di una traccia dati

```
+0     64   lead-in: "ATRI" ripetuto 16 volte
+64    32   "ATARI APPROVED DATA HEADER ATRI" + 1 byte di tipo
+96    64   tag di contenuto, 4 caratteri ripetuti 16 volte
+160   ...  payload
```

Il **byte di tipo** vale `0x20 + indice della traccia dati`: e' esattamente il
*data type* che il gioco passa a `GetTrack` (`CDCONTRO.GAS`), che lo usa come
offset di traccia rispetto alla prima traccia della seconda sessione.

Quindi **il block offset 0 di un data type corrisponde a `payload`**, cioe'
`inizio_traccia + 160`. Verificato: `BO_CINEPAK_BUZZ = 0` cade esattamente
sull'header `FILM` del primo filmato, al byte.

## 6.4 Layout del disco retail (USA)

| tr | tipo | cod | blocchi | MB | contenuto |
|---:|---|---|---:|---:|---|
| 1 | AUDIO | — | 10.472 | 23,5 | traccia audio (PCM 16 bit) |
| 2 | — | `0x20` | 120 | 0,3 | boot, codice 68000 |
| 3 | `DATA` | `0x21` | 2.689 | 6,0 | da identificare |
| 4 | `PICT` | `0x22` | 73.921 | 165,8 | **scene**: fondali + Z-buffer |
| 5 | `DATA` | `0x23` | 1.849 | 4,1 | da identificare |
| 6 | `DATA` | `0x24` | 2.129 | 4,8 | campioni audio (marcatore `WAVE` a +4) |
| 7 | `1111` | `0x25` | 102.127 | 229,1 | **Cinepak** (il tag e' il `sync_header` di `CINEPAK.INC`) |
| 8 | `DATA` | `0x26` | 561 | 1,3 | da identificare |
| 9 | `DATA` | `0x27` | 67 | 0,2 | da identificare, contenuto ad alta entropia |

Totale 193.935 blocchi, circa 456 MB su un disco da ~700 MB.

## 6.5 Quanto e' cambiato fra luglio e ottobre 1995

Molto. Tre riscontri concreti:

**1. I data type sono passati da 13 a 8.** Il sorgente di luglio ne definisce
tredici (`BOOT LOGICS MODELS ANIMS SCENES SOUNDS SETS WAVES BITMAPS PICTURES
SHEETS CINEPAKS CODES`); il disco retail ha otto tracce dati. Categorie che
erano distinte — verosimilmente `SCENES` e `PICTURES` — sono state fuse.

**2. I filmati sono quasi raddoppiati: 20 a luglio, 36 nel retail.**
Tutti `cvid` 320x240. Il primo (`BUZZ`, block offset 0) e' rimasto al suo posto;
tutti gli altri si sono spostati. Confronto dei salti fra filmati consecutivi:

```
luglio  2577 1407 5593 2152 4141 24784 6634 990 3030 1725 ...   (19 salti)
retail   544  977 2688 1472  940  5734 4428 4115 2343 15165 ... (35 salti)
```

**3. Non c'e' piu' una traccia Red Book per il parlato.** Il sorgente di luglio
indirizza 88 battute (`BO_CDAUDIO_LINE005` .. `LINE100`, fino al blocco 26.653)
su una traccia audio dedicata. Sul disco retail l'unica traccia audio e' la
prima, lunga 10.472 blocchi (2:19) — troppo corta. Il parlato e' stato spostato
altrove, molto probabilmente dentro i filmati o fra i campioni.

**Conclusione operativa:** il sorgente di luglio va usato come **specifica del
motore**, non come mappa del disco. Le tabelle `DATA.INC` e `CDLINK.INC` restano
preziose per i *nomi* e per capire cosa esiste, ma gli offset vanno rideterminati
dal disco.

## 6.6 Uso del tool

```
python tools/jcd/jcdinfo.py <immagine.jcd>
python tools/jcd/jcdinfo.py <immagine.jcd> --extract assets/tracks
python tools/jcd/jcdinfo.py <immagine.jcd> --hex 7 0 256
```

`--extract` scrive una traccia per file, gia' de-swappata e senza header, quindi
l'offset 0 di ogni file corrisponde al block offset 0 di quel data type.
