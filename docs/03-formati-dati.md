# 03 — Strutture dati e formati

Tutte le strutture qui sotto sono documentate esplicitamente nel sorgente
(`STRUCDEF.INC`, `LOGICS.INC`, `DATA.INC`, `WORLD.INC`, `OPCODES.INC`).
Big-endian, allineamento a long dove indicato.

---

## 3.1 Le quattro tabelle del mondo

Il gioco separa nettamente **stato persistente**, **istanza attiva** e
**dati di disegno**.

### World State Table (WST) — `ws`, 16 KB, 512 entry da 32 byte
Lo stato **persistente** di ogni entità del mondo (personaggi e oggetti).
Sopravvive ai cambi di scena e di set, ed è ciò che finisce nel salvataggio.

```
wstSet     .w   id del set
wstRadius  .w   raggio di collisione
wstSheet   .l   puntatore al character sheet (0 = entry vuota)
wstParent  .l   puntatore al WST del "genitore" (es. oggetto in mano)
wstXpos    .l   posizione a 32 bit
wstYpos    .l
wstZpos    .l
wstXface   .b   orientamento
wstYface   .b
wstZface   .b
wstSanity  .b   [0-100]
wstPerson  .b   personalità [0-100]
wstStr     .b   forza
wstLife    .b   punti vita [0-100]
wstFlags   .b
```

Flag (`wstFlags`): `Registered`, `Inanimate`, `Ammo`, `Weapon`, `Collectable`,
`Deactivated`, `Collidable`.

`WORLD.INC` elenca le **116 entry** con nome simbolico: `WORLD_QUENTIN` (0),
`WORLD_RAMIREZ` (1), poi tutti gli NPC e gli oggetti per set. Diversi NPC
portano i nomi dei membri del team (Dave, Kev, Mark, Carl, Matt, Chris...) —
sono i camei degli sviluppatori.

### Active Character Table (ACT) — `activchar`, 8 KB, 128 entry da 64 byte
Chi è **attivo adesso** e come si comporta.

```
actWorld     .l  -> WST
actInst      .l  -> CIT
actFlags     .w  (ACTCreated, ACTControlled)
actStatus    .w
actJoypad    .l  azione joypad corrente
actAction    .l  azione precedente
actCount     .l
actAICommand .l
actAIData1..4 .l
```

Comandi IA (`LOGICS.INC`): `aiNop`, `aiGotoPosition`, `aiGotoPerson`,
`aiFacePosition`, `aiFacePerson`, `aiAttackPerson`, `aiAttackPlayer`,
`aiFacePlayer`, `aiGotoPlayer`, `aiFollowPlayer`, `aiShootPerson`,
`aiShootPlayer`, `aiDefault`, `aiFollowPerson`.

### Character Instance Table (CIT) — `chartbl`
Lo stato di **animazione e fisica** del frame corrente. È la stessa struttura
descritta in `STRUCDEF.INC` come tabella dei personaggi:

```
citSheet    .l  -> character sheet
citDraw     .l  -> draw data area
citWorld    .l  -> WST
citModelNum .w  numero di pezzi da animare
citStance   .b  posa (sinistra / destra / neutra)
citTween    .b  quantità di tweening; bit 7 = reset altezza, 15 = calcola tween,
                0-3 = usa 0-3 passi di interpolazione
citAnimate  .l  -> file di animazione
citFrame    .w  frame in 8.8 fisso, il PROSSIMO da mostrare
citOldFrame .b  ultimo frame mostrato (per interpolazione)
citFlags    .b
citFacing   .b
citMoving   .b
citHeight   .w  altezza del triangolo di collisione corrente
citTriangle .w  triangolo corrente nella mesh di collisione del set
citGravity  .w
citSpeed    .w  velocità di riproduzione dell'animazione
citStyle    .w  0 = una volta, 1 = loop
citCollision .w
citXmoveback/Ymoveback/Zmoveback/Tmoveback .w
```

`citFlags`: `COLLIDE`, `COLTURN`, `PLAYER`, `PICKUP`, `ANIMEND`,
`KNOCKBACK1`, `KNOCKBACK2` (00 = indietro, 10 = avanti).

### Draw Data Area (DDA) — 4 KB, 256 entry da 16 byte
La lista di disegno passata al motore 3D.

```
ddaNext  .l  -> prossima entry (0 = fine)
ddaFlags .l  bit 0 = invisibile, bit 2 = niente illuminazione
ddaInst  .l  -> Instance Data Area (matrice 3x3 + pos + facing, 15 word)
ddaModel .l  -> poliedro
```

---

## 3.2 Character Sheet (CSH)

Descrive **quali risorse servono** a un personaggio. È la chiave del sistema
di streaming: caricando un set, il motore attraversa i sheet attivi e marca
tutto il resto come garbage.

```
cshNext      .l  -> prossimo sheet (0 = ultimo)
cshFlags     .w  (CSHLoaded, CSHLocked, CSHMarked)
cshModelOff  .b  offset e numero di modelli
cshModelNum  .b
cshAnimOff   .b  offset e numero di animazioni
cshAnimNum   .b
cshMiscOff   .b
cshMiscNum   .b
cshFileOff   .b
cshFileNum   .b
cshBehaviour .w
```

Estensione per i suoni di combattimento (`STRUCDEF.INC`):
`soundKIA` (morte), `soundHIT` (colpito), `soundATT` (attacco),
`soundPAR` (parata).

I 28 sheet sono elencati in `DATA.INC`: `SHEET_SET` (0), `SHEET_QUENTIN`,
`SHEET_HUNTER_SWORD`, `SHEET_HUNTER_OFFICER`, `SHEET_HUNTER_GUN`, `SHEET_CLAW`,
`SHEET_RAMIREZ`, `SHEET_WOMAN`, `SHEET_MANGUS`, `SHEET_ARAK`, `SHEET_KORTAN`,
`SHEET_CLYDE`, `SHEET_DUNDEEB`, `SHEET_DUNDEED`, `SHEET_MACLEODSWORD`,
`SHEET_GASGUN`, `SHEET_WINE`, `SHEET_CHEESE`, `SHEET_LOAF`, `SHEET_KEY`,
`SHEET_LOCKET`, `SHEET_HAND`, `SHEET_REDWATER`, `SHEET_GREENWATER`,
`SHEET_BLUEWATER`, `SHEET_GRINDER`, `SHEET_TANK`, `SHEET_TURRET`.

---

## 3.3 Modelli 3D (poliedri)

Prodotti da `SKELSKIN.EXE 3.6 (beta)` a partire da 3D Studio. Il sorgente
contiene **11 modelli in chiaro** come sorgente assembly, ottimi come banco di
prova per il parser: `MERLOT79.INC` (bottiglia di vino), `CHEESE.INC`,
`LOAF.INC`, `HKEY.INC`, `LOCKET.INC` / `HLOCKET.INC`, `HSWORD_Q.INC` (spada di
Quentin), `HWATAWEE.INC`, `GASGUN.INC`, più `COLINC.INC` con le palette.

```
; header, 4 long
.w  lunghezza del file in byte
.b  numero di origine
.b  numero di origini
.w  numero di vertici
.w  numero di facce
.l  VLP  -> vertex list
.l  FLP  -> facet list
.l  SLP  (0 se assente)
.l  CLP  (0 se assente)
```

**Vertex list** — 4 word per vertice: `x, y, z, 1` (l'ultima è il fattore
omogeneo). Coordinate intere; il commento generato riporta anche il valore
originale in virgola mobile, per esempio `dc.w $fffb ; v0 x, = -22.630207`.
La scala è dichiarata nell'header del file (`Using scaling factor of N mm per unit`).

**Facet list** — per faccia:

```
(RGB | Nx) (Ny | Nz) (V | NP)      ; 3 long
(v0|v1|v2|v3) ... (vu|vv|vx|vy)    ; gruppi da 4 indici a byte
```

`V` = numero di vertici della faccia, `NP` = offset in long alla faccia
successiva, `RGB` = colore a 16 bit, `Nx/Ny/Nz` = normale della faccia.
Indici vertice validi 0-254; **255 = nessun riferimento** (riempimento).
Massimo 32 vertici per faccia.

I colori sono definiti come costanti nominate, con il nome del materiale
3D Studio nel commento: `COLOURmerlot791 .equ $9a5e ; BEIGE MATTE r=155, g=122, b=74`.
Questa mappatura RGB-a-16-bit è utile per capire la codifica CRY/RGB in uso.

---

## 3.4 Animazioni

Header:

```
ANIMSIZE   .w
OFFSETLOW  .w
FRAMESIZE  .w
ANIMMODELS .b   numero di modelli (pezzi) animati
NUMFRAMES  .b
ANIMFPS    .b
SOUNDSHEET .b
SOUNDENTRY .w
HEIGHTSTART .w
```

Frame:

```
animXmove .w    spostamento radice
animYmove .w
animZmove .w
animYturn .b    rotazione su Y
animFlags .b
animSpin  .b
animHit   .b    valore di attacco/difesa (negativo = difesa)
animRange .w    portata dell'attacco
animDirAz .b
animDirEl .b
animSprAz .b
animSprEl .b
animHigh  .w    punto più alto del personaggio
animLow   .w    punto più basso
animAngles ...  gruppi di 3 byte (rotazioni) per ciascun modello
```

Il modello di animazione è quindi **gerarchico a rotazioni**: ogni frame
contiene solo tre angoli per pezzo, più lo spostamento della radice. Il
tweening fra frame è calcolato a runtime (`citTween`).

Il combattimento è dato dai frame stessi: `animHit` positivo = attacco,
negativo = difesa/parata, `animRange` = portata. Vedi `PCOL.TXT` per
l'algoritmo completo di risoluzione.

Flag di stato animazione (`LOGICS.INC`): `FSATurn`, `FSAPlay`, `FSALock`,
`FSAShield`, `FSAHit`.

---

## 3.5 Set e scene

Un **set** è un ambiente (una "stanza" del mondo); una **scena** è
un'inquadratura fissa dentro quel set. `HIGH1.MAK` elenca i **45 set**:

```
CANYO CN1_MK2 CN2_MK2 CN3_MK2 CN4 CN5 CN6_MK2 CN7_MK3 CN8_MK3 CN9
CODER CODER2 COR_DOR COR2_DOR COR3_DOR DOME1 DOME2
DUN_4 DUN1 DUN2 DUN3 DUN5 DUN6
G_ROOM1 G_ROOM2 G_ROOM3 MENU NEWSEWER PRISON2 REST SECUR SEWER
SHANR1 SHANR2 SHANR3 TANK
TENT1 TENT2 TENT3 TENT4 TENT5 TENT6 TENT7 THRONE TRAIN
```

`CDLINK.INC` elenca **594 scene** (inquadrature) totali.

Header dei dati di set:

```
Hinum        .l
Lonum        .l
EventOffset  .l   -> dati eventi
CollOffset   .l   -> dati collisione
InitOffset   .l   -> punti di inizializzazione
SceneOffset  .l   -> tabella scene (block offset sul CD)
ScriptOffset .l   -> script del set
```

### Mesh di collisione

```
; header
TriOffset .w
NumTri    .w
NumVer    .w
(padding) .w
VertData  ...   coppie di long (vertici)

; ogni triangolo, 14 byte
triHeight .w    0 = infinita
triVert0  .w
triVert1  .w
triVert2  .w
triTri01  .w    triangolo adiacente sul lato 0-1, oppure $FFFF = muro
triTri12  .w
triTri20  .w
```

È una **mesh 2D di navigazione con adiacenze esplicite**: il personaggio sa
sempre in che triangolo si trova (`citTriangle`) e cercare il successivo costa
un attraversamento del vicinato, non una ricerca globale (`FINDTRI.GAS`).

---

## 3.6 Il formato `.MAP` (editor di mappe)

Sono file **di testo**, generati dal "Map Editor 1.211b Beta" di Matthew Jesson.
Nel dump ce ne sono solo due (`DUN1.MAP`, `DUN2.MAP`) ma bastano a documentare
il formato per intero.

Blocchi presenti: `MAP`, `BACKGROUND`, `VERTEX`, `COLLISION`, `ORIGIN`, `SCALE`,
`EVENT`, `START`, `MARKER`, `CHARACTER`, `SCENES`, `CAMERAS`.

```
BLOCK VERTEX          NUM_VERTEX 117. / VERTEXn. x. y.      (coordinate 2D)
BLOCK COLLISION       VERTEX_LIST a. b. c.  HEIGHT h.       (un triangolo)
BLOCK ORIGIN          VERTEX n.  HEIGHT h.
BLOCK SCALE           VERTEX0 / VERTEX1 / DISTANCE 8382.    (unità del mondo)
BLOCK EVENT           VERTEX0 / VERTEX1 / HEIGHT / PRIORITY / SCENE <nome>
                      opzionale IFBIT <world state bit>
BLOCK START           VERTEX / ORIENTATION / START <chi> / FROM <scena>
BLOCK MARKER          VERTEX / HEIGHT                        (bersagli per script)
BLOCK CHARACTER       VERTEX / HEIGHT / TYPE / ORIENTATION / RADIUS
                      opzionali SAN / PER / LIFE / STRENGTH
BLOCK SCENES          ( PICTURE <scena> CAMERA <cam>
                        CACHE_SCENE0 <scena> CACHE_SCENE1 <scena> )
BLOCK CAMERAS         THREEDSTUDIO F:\DUN2.3DS
```

Due cose importanti:

1. **Gli eventi sono segmenti, non cerchi.** Ogni `EVENT` ha due vertici e una
   `PRIORITY`: attraversando quella linea si cambia inquadratura. La priorità
   risolve le sovrapposizioni (vedi `CACHE.TXT`).
2. **`CACHE_SCENE0`/`CACHE_SCENE1`** dichiarano quali due scene precaricare
   quando si è nella scena corrente. È il sistema di caching che nasconde i
   tempi di seek del CD.

---

## 3.7 Il linguaggio di script

Una **VM multitasking** che gira sulla GPU (`SCRIPT.GAS`, 1.603 righe).
Ogni processo ha 128 byte:

```
scriptPC     .l   0 = inattivo
scriptStack  .l
scriptFlags  .l   copia dei flag GPU
scriptPause  .w   contatore alla rovescia (256esimi di secondo)
scriptAction .w   handle dell'azione in attesa (0 = nessuna)
scriptChar   .l   personaggio primario
scriptIdent  .l   identificatore univoco
scriptReg    .l x15   registri
scriptSpace  .l x11   stack corto
```

Set di istruzioni completo in `OPCODES.INC`, macro assembler in `SCRIPT.MAC`.
Per categoria:

| Categoria | Opcode |
|---|---|
| Aritmetica | `not neg abs add sub cmp copy mult div inc dec sett cmpi exg` |
| Controllo | `bra quit bsr rts spawn suicide kill pause` |
| Personaggi | `select swapchar chartoreg regtochar animate animhack setanim chase attack face goto turnto attplay freeze release waitforit waitforanim` |
| Eventi | `cinepak redbook sample camera charchange eventbit waitevent waitredbook eventmask` |
| Accesso dati | `wstread wstwrite actread actwrite citread citwrite` |
| Stato di gioco | `testowner testset testscene testbit setbit testprox testdist testevent` |
| Input | `keytest keymask` |
| Varie | `slideto restore sat patch activation reset fake_scene pickup default poke triangle_height` |

`quit` significa "cedi il controllo fino al prossimo frame": è una VM a
coroutine cooperative, un tick per game loop.

Esempio reale (`DOME1.SCT`, riprodotto per intero):

```
    testbit WSB_PLAYED_DOME_CINEPAK
    branch  lExit, ne
    pause   2560
    setbit  WSB_PLAYED_DOME_CINEPAK
    cinepak BO_CINEPAK_DOME
lExit:
    quit
    abandon
```

`SAMPLE.SCT` e `MENU.SCT` sono script più corposi (menu principale con
navigazione, gestione morte, codice cheat) e valgono come specifica di
riferimento della semantica.

### World State Bits
`WSB.INC` + `MATTSWSB.INC` + `ROBSWSB.INC` definiscono i flag globali di
progressione (128 byte di area `gamestate`), per esempio
`WSB_PRISON_DOOR_OPEN`, `WSB_QUENTIN_HAS_CELL_KEY`, `WSB_SEEN_HOO`,
`WSB_DUN2_OK_LEAVE`, `WSB_COLLECT_PREVENT`.

---

## 3.8 Tipi di dato sul CD

```
DATA_TYPE_BOOT     0     DATA_TYPE_SETS      6
DATA_TYPE_LOGICS   1     DATA_TYPE_WAVES     7   (Red Book)
DATA_TYPE_MODELS   2     DATA_TYPE_BITMAPS   8
DATA_TYPE_ANIMS    3     DATA_TYPE_PICTURES  9   (640x400)
DATA_TYPE_SCENES   4     DATA_TYPE_SHEETS   10
DATA_TYPE_SOUNDS   5     DATA_TYPE_CINEPAKS 11
                         DATA_TYPE_CODES    12
```

Tipi di evento di scena:

```
EVENT_TYPE_SCENE 0   SOUND 1   CINEPAK 2   SOUNDLOOP 3   CDAUDIO 4
SOUNDOFF 5   RESTOREITEM 6   SETBIT 8   RESETBIT 9   CHARCHANGE 14
```

---

## 3.9 Salvataggi (NVRAM)

`NVRAM.S`, `SAVE.TXT`, `NVRAM2.TXT`. Nomi file fissi imposti da Atari
(`GAME1`..`GAME9999`), area di gamestate da 128 byte più un sottoinsieme del
WST per ciascun personaggio e oggetto. C'è anche un percorso alternativo
**senza NVRAM**, basato su codici da inserire col tastierino (`NONNVRAM.TXT`).
Per il port si può ignorare il formato originale e serializzare direttamente
WST + gamestate.
