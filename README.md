# GrooveDeck FM-1

Eigene Firmware für den **M-VAVE FM-1** auf Basis der Klangerzeugung von [GrooveDeck](https://github.com/acidstefan/GrooveDeck) (Stufe 1): Step-Sequencer mit 16–64 Steps, synthetische ESX-1-artige Drums, Bass und Lead mit 303-Filter, LFOs mit Takt-Sync, Arpeggiator und Chord Variator (32 + 8 Akkordformen), FX und Soundgoodizer mit Auto-Pegel, 440 Werkspresets und 92 Kanal-Presets.

Eigener Code, ohne GPL-Abhängigkeit. Die Hardware-Treiber kommen aus [fm1-nes](https://github.com/Keitark/fm1-nes) (Apache-2.0) und werden nur eingebunden, nicht kopiert (siehe `NOTICE.md`).

## Stand

| Teil | Ordner | Stand |
|---|---|---|
| Klangerzeugung, Sequencer, Arp, Presets | `core/` | fertig, am PC geprüft |
| Bedienoberfläche (240×240), Tasten/Drehgeber-Auswertung | `core/` | fertig, in der PC-Simulation geprüft |
| PC-Werkzeuge und Tests | `host/`, `tests/` | fertig |
| Hardware-Anbindung FM-1, Build-Skript | `target/` | **Entwurf, noch nicht kompiliert** – siehe `target/README.md` |

### Prüfergebnisse

- **Klanggleichheit mit dem Plugin:** Die Original-Engine (`Source/Dsp.h`) und die FM-1-Engine bekommen dieselben Parameter und Noten, für alle 440 Werkspresets:
  - ohne FX: Korrelation 1,000, Pegelabweichung 0,00 dB (sample-genau)
  - mit FX: Korrelation ≥ 0,997, Pegelabweichung 0,00 dB (Delay mit 16 Bit statt 32 Bit)
- Alle 440 Presets laufen ohne Übersteuerung, ohne NaN und ohne Stille.
- **Rechenzeit:** schwerstes Preset ca. 190 Mio. x86-Befehle je Sekunde Audio, davon ca. 100 Mio. Stimmen und ca. 75 Mio. FX/Master. Ein Kern des FM-1 schafft im Emulator-Modell 125 Mio. Befehle/s. Deshalb ist die Engine in zwei Hälften teilbar (zwei Kerne), und es gibt Spar-Schalter (`--lean`). Ob es auf einem Kern reicht, zeigt erst der Emulator.

## PC-Werkzeuge bauen (Windows, Linux, macOS)

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

- `gd_render 0 8 preset.wav`: Preset 1 als 8-Sekunden-WAV rendern (zum Anhören)
- `gd_render --list`: alle Presets anzeigen
- `gd_render --all`: alle Presets prüfen (Pegel, NaN, Rechenzeit)
- `gd_sim ausgabe`: Bedienung simulieren, Bildschirmfotos (PPM) und Ton (WAV) schreiben
- Klangvergleich mit dem Plugin: `cmake -B build -DGROOVEDECK_SOURCE=<Pfad>/GrooveDeck/Source`, danach `compare_ref 0 439 0`

## Bedienung auf dem FM-1

| Bedienelement | Funktion |
|---|---|
| PRESETS (Drehgeber) | Werkspreset wählen (lädt sofort, Wiedergabe läuft weiter) |
| SELECT (Drehgeber) | Kanal wählen: BD SD TM PC CP CH OH CY C2, AC (Akzent), BS (Bass), LD (Lead) |
| ALGORITHM (Drehgeber) | Tempo |
| KNOB 1–4 | Regler der aktuellen Seite |
| HOME / ENV / LFO / EDIT / FX / GLO / ARP / SEL | Reglerseite wählen; nochmal drücken = Seite B |
| SEQ | Tastatur umschalten: STEP (Tasten 1–16 setzen Steps) oder PLAY (Tasten spielen) |
| PLAY/STOP | Start/Stopp |
| REC | Live-Aufnahme: gespielte Noten landen auf dem nächsten Step |
| OCT−/OCT+ | STEP: Step-Seite (17–32 …), PLAY: Oktave; beide zusammen: zurücksetzen |
| SAVE | Stand sichern (noch nur im RAM); SEL + SAVE: gesicherten Stand laden |
| MASTER (Poti) | Master-Lautstärke |

Im PLAY-Modus mit gewähltem Drum-Kanal spielen die untersten neun Tasten die neun Drums (Fingerdrumming). Bei Bass/Lead spielen die Tasten chromatisch, über Arpeggiator/Chord, wenn die eingeschaltet sind. Eine Step-Taste setzt bei Bass/Lead die zuletzt gespielte Note.

Reglerseiten (A / B):

| Seite | A | B |
|---|---|---|
| HOME | Cutoff, Reso, EG Int, Level | Pan, EG Time, Tube, Mute |
| ENV | Cutoff, Reso, EG Int, EG Time | Filtertyp, Modell (SVF/303/Ladder), Acc Sweep, Overdrive |
| LFO | Typ, Tiefe, Tempo, Ziel | Sinus, Noise, BPM-Sync, Takt-LFO |
| EDIT Drums | Pitch, Amp Decay, Low Boost, Pan | Quelle (Sinus/Dreieck), Tube, Roll, Pitch-Mod |
| EDIT Bass/Lead | OSC1, OSC2, OSC2 Pitch, OSC Bal | OSC Mod, Glide, Dist, Tracking |
| FX | FX1 Typ, Edit 1, Edit 2, FX Send | FX2 Typ, Edit 1, Edit 2, FX-Wahl |
| GLO | Tempo, Pattern-Länge (1–64), Gate, Soundgoodizer | Stil, Auto-Pegel, Master Tube, FX-Kette |
| ARP | Arp an, Style, Grid, Oktaven | Chord an, Akkordform, Voicing, Arp Gate |
| SEL | Kanal-Preset, Mute, Level, Sweep-Tempo | – |

## Aufbau

```
core/     hardwareunabhaengig (C99)
  gd_dsp      Engine: Parts, Filter (SVF, 303-Diode, Ladder), LFO, FX, Soundgoodizer
  gd_seq      Sequencer, Keyboard-Routing, Arpeggiator-Laufzeit
  gd_arp      Arpeggiator-Styles, Akkordformen, Voicings
  gd_preset   Preset-Decoder/-Encoder; gd_presets_data.c ist generiert
  gd_app      Bedienlogik und Ereignis-Warteschlange (Bedien-Task -> Audio-Task)
  gd_ui       Display, streifenweise gezeichnet (kein voller Bildspeicher noetig)
  gd_input    Tastenmatrix, Buttons, Drehgeber, MASTER-Poti
  gd_config.h Groessen und Spar-Schalter
host/     PC-Werkzeuge (WAV-Renderer, Bedien-Simulation)
tests/    Offline-Tests, Klangvergleich mit dem Plugin
tools/    Generatoren (Presets aus dem Plugin, Pixelfont)
target/   FM-1: Einstieg, Build-Skript, Anleitung
```

Presets neu erzeugen, wenn sich die Presets im Plugin ändern:
`python tools/gen_presets_c.py <Pfad>/GrooveDeck/Source`
