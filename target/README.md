# GrooveDeck FM-1 – Firmware bauen und im Emulator testen

**Stand:** Der Quelltext für die Hardware-Anbindung (`gd_fm1_main.c`, `build.py`) ist ein **Entwurf und wurde noch nie kompiliert**. In der Arbeitsumgebung, in der er entstanden ist, waren die JieLi-Toolchain und das AC79-SDK nicht erreichbar. Rechne beim ersten Bauen mit Fehlermeldungen. Schick sie mir (`build/build.log`), dann korrigiere ich sie.

Die Musik-Engine selbst (`core/`) ist fertig und am PC geprüft (siehe Haupt-README).

## 1. Was du brauchst

| Was | Woher | Hinweis |
|---|---|---|
| WSL2 mit Ubuntu | `wsl --install` in einer Admin-PowerShell | für Toolchain (Linux) und Emulator |
| JieLi-Toolchain (clang 4.0.1 für pi32v2) | `curl -fL https://pkgman.jieliapp.com/s/linux-toolchain \| tar -xJ -C ~/.jieli` | dieselbe Quelle, die Felucca nutzt; Ordner `~/.jieli/jieli-linux-toolchains-*/pi32v2/bin` |
| fm1-nes (Treiber, Apache-2.0) | `git clone https://github.com/Keitark/fm1-nes ~/fm1-nes` | liefert Display-, Tasten- und Poti-Treiber |
| AC79-SDK (Apache-2.0) | in `~/fm1-nes`: `python3 scripts/dependencies.py --fetch` | holt die von fm1-nes festgelegte SDK-Version nach `.deps/ac79-sdk` |
| fm1-emulator | `curl -fL https://github.com/simonjohansson/fm1-emulator/releases/latest/download/fm1-emulator-linux-amd64.tar.gz \| tar -xz` | läuft unter WSL2 mit Fenster und Ton (WSLg) |

Unter Windows ohne WSL geht es auch, wenn du eine Windows-Version der JieLi-Toolchain hast (`clang.exe` usw., fm1-nes erwartet sie unter `C:/JL/pi32/bin`). `build.py` erkennt beide Varianten.

## 2. Bauen

```bash
cd ~/GrooveDeck-FM1
export FM1_NES_DIR=~/fm1-nes
export FM1_TOOLCHAIN_DIR=$(ls -d ~/.jieli/jieli-linux-toolchains-*/pi32v2/bin | tail -1)
python3 target/build.py --out build
```

Ergebnis: `build/groovedeck.app.bin` (für das Gerät) und `build/groovedeck-emu.bin` (für den Emulator, siehe Abschnitt 3).

Optionen:
- `--y-offset 40`: verwenden, falls das Bild im Emulator verschoben oder abgeschnitten ist.
- `--lean`: Spar-Einstellungen für zu wenig Rechenzeit (Hall mono, Soundgoodizer-Analyse mit halber Rate, 4 Stimmen je Bass/Lead).

## 3. Im Emulator starten

```bash
./emulator build/groovedeck-emu.bin
```

Die Emulator-Fassung unterscheidet sich nur darin, dass Parallelbefehle mit `clz` (vom Compiler im Betriebssystem-Kern erzeugt) nacheinander ausgeführt werden. Diese Form kennt der Emulator noch nicht.

Tastatur im Emulator: Z/C spielen Noten, X/V Oktave, P/O ENV/LFO, H HOME; die übrigen Bedienelemente mit der Maus (Klick auf Buttons, Ziehen/Scrollen an den Drehgebern). Zum Prüfen ohne Fenster: `./emulator --headless build/groovedeck-emu.bin`.

Wichtig dabei: Der Emulator misst die Zeit in Befehlen (8 ns je Befehl). Läuft der Ton stotternd oder langsamer als Echtzeit, reicht die Rechenzeit nicht. Dann bitte mit `--lean` bauen und mir Bescheid geben.

## 4. Auf das echte Gerät? Noch nicht.

Diese Firmware enthält **keinen USB-Updater**. Nach dem Aufspielen ließe sich das Gerät nur noch über den Masken-ROM-Downloadmodus zurückflashen, also mit einem RP2040-Adapter und dem FM-1-Transporter oder MvaveFM1Unbricker. Außerdem ist die Speicherfunktion (SAVE) noch nicht an den Flash angebunden. Bevor wir aufs Gerät gehen, kommen:
1. USB-Update-Weg (ohne GPL-Code)
2. SAVE dauerhaft im Flash
3. Prüfung des Flash-Chips (neuere Geräte haben einen UC25HQ80)

## 5. Hardware, für die noch Treiber fehlen

Das Verhalten ist in `core/` fertig. Auf dem FM-1 fehlt jeweils nur die Anbindung:

| Element | Was fehlt | Ansatz |
|---|---|---|
| LEDs (27 Tasten, Buttons, PLAY grün) | Ansteuerung | Die LEDs hängen an der Tastenmatrix: Eine LED leuchtet, solange ihre Spalte der 74HC595-Kette aktiv (low) ist und ihre LED-Leitung high (PA9, PA10, PH6, PH9 für Zeilen 1–4). PLAY grün: Spalte 8, Zeile 1 (Emulator-Zählung). Helligkeit über die Einschaltdauer je Spalte. Der fm1-nes-Scanner hält diese Leitungen low und muss dafür erweitert werden. Werte liefert `gd_leds_compute`. |
| MIDI-Eingang TRS | UART-Pin, Baudrate 31250 | Pin noch nicht dokumentiert; Bytes an `gd_midi_byte` geben |
| USB-MIDI | USB-Gerätestack | ohne GPL-Teile von fm1-nes; Bytes an einen zweiten `gd_midi`-Parser |
| Bluetooth-MIDI | BLE-Stack | spätere Stufe |
| Akku | Spannungsmessung | ADC-Kanal noch nicht dokumentiert; Wert an `gd_app_set_battery` |
| Lautsprecher / Kopfhörer | – | schaltet die Hardware selbst um |

## 6. Bekannte offene Punkte

| Punkt | Stand |
|---|---|
| Display-Zeilenversatz | 0 oder 40, im Emulator prüfen (`--y-offset`) |
| Unterste Taste | Werks-Firmware: F3 (`GD_KEY_BASE_NOTE 53`); das Emulator-Panel beschriftet die Tasten ab C |
| Zweiter Kern | noch ungenutzt; die Engine ist bereits geteilt (`gd_app_audio_voices` / `gd_engine_render_master`) |
| Delay-Länge | auf 0,74 s begrenzt (Speicher); betrifft nur sehr langsame Dub-Presets |
