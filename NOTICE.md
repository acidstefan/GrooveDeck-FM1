# Fremdanteile und Herkunft

- **Klangerzeugung, Sequencer, Arpeggiator, Presets:** portiert aus dem eigenen Projekt GrooveDeck (acidstefan/GrooveDeck).
- **Pixelfont** (`core/gd_font.c`, `tools/gen_font.py`): eigene Zeichnung.
- **Tastenmatrix-Tabelle** (`core/gd_input.c`, `k_cells`): Belegung der Werks-Firmware FM-1_010, wie sie fm1-nes dokumentiert (Keitark/fm1-nes, `firmware/nes/src/fm1_stock_keys.c`, Apache-2.0). Die Drehgeber-Kontakte sind aus der Board-Beschreibung des fm1-emulator abgeleitet (Fakten, kein Code übernommen).
- **Hardware-Treiber auf dem FM-1** (Display, Tastenmatrix, MASTER-Poti, Start-Code): werden beim Bauen aus einem fm1-nes-Klon eingebunden (Apache-2.0, Copyright der fm1-nes-Autoren) und nicht in dieses Repository kopiert. Wer ein gebautes Firmware-Image weitergibt, muss den Apache-2.0-Lizenztext und die Hinweise von fm1-nes beilegen.
- **Start-Bruecke** (`target/gd_boot_compat.c`): eigene Umsetzung nach dem Vorbild von fm1-nes `boot_compat.c` (Apache-2.0); die Ersatzwerte fuer den Emulator-Start sind die SDK-Standardwerte des Flash-Kopfs und die vom fm1-emulator dokumentierte Uebergabe-Belegung.
- **JieLi AC79-SDK und Toolchain:** werden beim Bauen eingebunden (Repository-Lizenz Apache-2.0, enthält vorkompilierte Bibliotheken von JieLi). Nicht in diesem Repository.
- **fm1-emulator** (GPL-2.0): nur als Werkzeug zum Testen benutzt, kein Code übernommen.
