# pc-highlander

Reimplementazione nativa per PC di **Highlander: The Last of the MacLeods**
(Lore Design Ltd. / Atari Corp., Jaguar CD, 1995).

> **Stato: fase 1 completata.** Il container `.jcd` e' decodificato e il
> layout del disco retail e' mappato. Motore non ancora iniziato.

---

## BYOA — Bring Your Own Assets

Questo repository **non contiene**, e non conterrà mai:

* il codice sorgente originale del 1995 (© Lore Design Ltd. / Atari Corp.);
* immagini disco (`.jcd`, `.cdi`, `.bin`/`.cue`) del gioco;
* asset estratti dal disco (modelli, animazioni, fondali, FMV, audio, script).

Contiene soltanto:

* **documentazione dei formati di dato** (fatti, non espressione creativa);
* **codice nostro** — motore riscritto da zero e tool di estrazione;
* nessun byte copiato dal materiale originale.

Per giocare servirà una copia del disco Jaguar CD di cui si è legittimi
proprietari. Il layout previsto in locale (interamente in `.gitignore`):

```
PC-Highlander/
  Highlander/          <- dump del sorgente 1995 + immagini disco (locale, mai committato)
  assets/              <- output dei tool di estrazione (locale, mai committato)
  docs/  src/  tools/  <- questo repo
```

---

## Il gioco in una riga

Avventura/action 3D in terza persona con **fondali pre-renderizzati a camera
fissa** (stile *Alone in the Dark* / *Resident Evil*), personaggi poligonali
in tempo reale composti sul fondale tramite **Z-buffer pre-calcolato**,
filmati **Cinepak** e parlato in **Red Book audio**.

## Indice della documentazione

| Doc | Contenuto |
|---|---|
| [docs/01-inventario.md](docs/01-inventario.md) | Cosa c'è nel dump del sorgente, cosa manca, com'è messa la ISO |
| [docs/02-architettura.md](docs/02-architettura.md) | Architettura del motore: 68000 + GPU + DSP, game loop, memoria |
| [docs/03-formati-dati.md](docs/03-formati-dati.md) | Strutture dati e formati di file |
| [docs/04-cd-e-asset.md](docs/04-cd-e-asset.md) | Layout del CD, come il gioco indirizza i dati, piano di estrazione |
| [docs/05-roadmap.md](docs/05-roadmap.md) | Strategia di porting e roadmap per fasi |
| [docs/06-formato-jcd.md](docs/06-formato-jcd.md) | Formato del container `.jcd` e layout del disco retail |
| [docs/sessions/](docs/sessions/) | Diario di lavoro, una nota per sessione |

## Crediti dell'originale

Lore Design Ltd., 1994-95 — Andrew M. Harris (motore 3D, CD, eventi),
Robert C. Dibley (animazione, combattimento, script VM, NVRAM),
Matthew Jesson (tool di mappa/asset, script di gioco), Jakes Mo (audio, logiche,
Cinepak), Chris Lowe (tool modelli). Pubblicato da Atari Corp.
