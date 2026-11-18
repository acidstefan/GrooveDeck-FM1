# GrooveDeck FM-1 auf FM1 Open

Diese Fassung setzt GrooveDeck auf **FM1 Open**, das offizielle Entwicklungspaket von M-VAVE
([github.com/m-vave/fm1_open](https://github.com/m-vave/fm1_open)). Die Systembibliothek von M-VAVE bleibt
unverändert und liefert weiter:

- USB-Update (Workbench, M-VAVE-Updater): wird vor der App behandelt
- MIDI-Eingang über die Klinke (TRS), USB und Bluetooth; MIDI-Ausgang über USB und Bluetooth
- Tasten, Buttons, Drehgeber, MASTER, Akku-ADC, LEDs, Display, Flash-Speicher, Audio-Ausgang

Ersetzt wird nur die App-Schicht von FM1 Open: Oberfläche, Projekte, MIDI-Weiterleitung. An ihre Stelle
kommen GrooveDeck-Klang, Sequencer, Display und Bedienung (Ordner `core`, dieselbe Software wie in
`gd_panel` am PC).

| Datei in `overlay/app` | Aufgabe |
|---|---|
| `screen.c` | Bedienung (SDK-Panel-API → GrooveDeck), Display, LEDs, Akku, MASTER, Ton im Voraus rechnen |
| `midi_router.c` | MIDI-Eingang an GrooveDeck, MIDI-Ausgang (Clock, Start/Stop, Noten) |
| `project.c` | Benutzerstand im Flash (SAVE halten), Platzhalter für die Bibliothek |
| `board/audio.c` | Audio-Interrupt holt die fertigen Blöcke (statt der Instrument-Engine von FM1 Open) |
| `board/fm1_input.h`, `board/panel.c` | LED-Helligkeit in Stufen (Sigma-Delta in der Tastenabtastung) |
| `audio_output.c` | Ausgabe ohne zweite Lautstärke (MASTER sitzt in GrooveDeck) |

## Lizenz

Die App-Schicht von FM1 Open steht unter **GPLv3 mit SDK-Linking-Ausnahme**. Diese Fassung von GrooveDeck
ist ein darauf aufbauendes Werk und wird ebenso unter GPLv3 weitergegeben (Dateien in `fm1open/`, und
zusammen mit ihnen der Kern in `core/`). Die Systembibliothek `libfm1_engine.a` behält M-VAVEs eigene
Bedingungen (Weitergabe nur als Teil von Firmware für FM1-Geräte), siehe `THIRD_PARTY.md` in FM1 Open.

## Bauen (Windows)

Voraussetzungen:
- Windows x64 mit PowerShell, `git`
- **JieLi-Toolchain 2.5.3 für Windows** (enthält PI32v2 clang 4.0.1): Installer laut README von FM1 Open,
  Installation nach `C:\JL`; gebraucht wird `C:\JL\pi32\bin\clang.exe`

```powershell
mkdir C:\fm1; cd C:\fm1
git clone https://github.com/m-vave/fm1_open.git
cd fm1_open; git checkout 4c7496daac04a95ff10d1afdcc8f9ead2dbabc41; cd ..
git clone https://github.com/acidstefan/GrooveDeck-FM1.git
powershell -ExecutionPolicy Bypass -File GrooveDeck-FM1\fm1open\install.ps1 -Fm1Open C:\fm1\fm1_open
cd fm1_open
powershell -ExecutionPolicy Bypass -File sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32
```

Ergebnis: `C:\fm1\fm1_open\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc` (Firmware-Paket) und
`build\dx7\firmware-report.json` (Speicher- und Prüfbericht von Build.ps1).

`install.ps1` kennt Schalter für den Speicher-/Rechenbedarf: `-DelayLen 24576` (Delay je Kanal),
`-Voices 4`, `-StereoReverb`, `-GoodFullRate`. Die Voreinstellungen sind sparsam, weil die Bibliothek
selbst rund 220 KB des Arbeitsspeichers belegt und Build.ps1 mindestens 48 KB freien Heap verlangt.

## Online bauen (GitHub Actions)

Ohne eigene Toolchain: Unter *Actions → FM1 Open Firmware* baut jeder Push (oder „Run workflow“) die Firmware
auf einem Windows-Rechner von GitHub. Download am Lauf unter *Artifacts* → `groovedeck-fm1open`
(`FM1_OFFICIAL_OPEN.fwsc`, `firmware-report.json`, `build-log.txt`, `sdk.map`).

## Presets

Das Firmware-Abbild darf höchstens rund 443 KB groß sein; alle 440 Song-Presets (200 KB) passen nicht hinein.
Darum:
- **In der Firmware:** je Richtung ein Song-Preset (22) und alle 92 Kanal-Presets, LZ-komprimiert
  (`core/gd_presets_mini.c`, 5 KB).
- **Im Nutzerspeicher:** alle 440 Song-Presets als Paket (213 KB) ab Byte 8192 der Benutzer-Partition
  (davor der mit SAVE gesicherte Stand). Aufspielen: `fm1open/uploader/index.html` in Chrome oder Edge öffnen,
  FM-1 per USB anschließen, „Presets aufspielen“. Das Paket wird beim Start per Prüfsumme geprüft; ist es
  defekt oder fehlt es, nimmt GrooveDeck den kleinen Satz.

Beides erzeugt `python tools/gen_presets_pack.py` aus `core/gd_presets_data.c`.

## Vor dem Aufspielen

Noch nicht auf das Gerät spielen, solange der Bau nicht fehlerfrei durchläuft und die Punkte unten nicht
geklärt sind. Allgemein gilt:
- In der Workbench Projekte, DX7-Sounds und Samples sichern: GrooveDeck nutzt dieselbe Benutzer-Partition.
- Zurück zur Original-Firmware geht es mit M-VAVEs Updater oder der Workbench („Return to official V15“).
  Bleibt das Display nach einem abgebrochenen Update schwarz, meldet sich der Chip am PC als `WL80UBOOT`
  (Boot-Modus des Chips); dann lässt sich die Original-Firmware wieder aufspielen.

## Offen / ungetestet

- Baut mit der Windows-Toolchain und besteht die Prüfungen von Build.ps1; auf dem Gerät noch nicht getestet.
- Ton wird im Control-Task vorausgerechnet (46 ms Vorrat); wie oft die Bibliothek diesen Task aufruft, zeigt
  erst das Gerät (Meldung bei Aussetzern: Zähler `gd_underruns`).
- Drehrichtung der Regler, Akku-Umrechnung (Annahme: Teiler 1:2, 3,3 V) und LED-Helligkeit am Gerät prüfen.
