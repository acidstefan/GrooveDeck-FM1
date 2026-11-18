/* GrooveDeck FM-1 - Auswertung der Tastenmatrix: 11 Abtastspalten (74HC595-Kette) x 6 Rueckleitungen,
   aktiv low. Liefert Tasten (27), Buttons (14) und Drehgeber (7) als Ereignisse an gd_app.
   Die Matrixpositionen: Werks-Tabelle (FM-1_010, ueber fm1-nes) und Board-Modell des fm1-emulator. */
#ifndef GD_INPUT_H
#define GD_INPUT_H
#include <stdint.h>
#include "gd_app.h"

#define GD_SCAN_ROWS 11

typedef struct {
    uint64_t stable, cand;   /* entprellte bzw. Kandidaten-Kontakte (Bit = Slot 0..40, 1 = gedrueckt) */
    uint8_t candCount;
    uint8_t encState[GE_COUNT];
    int8_t encAcc[GE_COUNT];
    uint16_t master;         /* letzter gemeldeter MASTER-Wert (0..1000) */
} gd_input;

void gd_input_init(gd_input *in);
/* Eine vollstaendige Abtastung (rows[i] = Rueckleitungen der Spalte i, Bit = Leitung, 0 = Kontakt geschlossen).
   Etwa jede Millisekunde aufrufen. Aenderungen gehen als Ereignisse an die App. */
void gd_input_scan(gd_input *in, const uint8_t rows[GD_SCAN_ROWS], gd_app *app);
/* LED-Helligkeiten (gd_leds_compute) auf das Multiplex-Raster legen: grid[Spalte 0..10][LED-Leitung 0..3].
   Spalte = Bit im 74HC595-Wort, Leitung 0..3 = PA9, PA10, PH6, PH9 (Matrixzeilen 1..4). */
#define GD_LED_COLS 11
#define GD_LED_LINES 4
void gd_input_led_grid(const uint8_t keys[GD_KEYS], const uint8_t btn[GB_COUNT], uint8_t playGreen,
                       uint8_t grid[GD_LED_COLS][GD_LED_LINES]);
/* MASTER-Poti, Rohwert 0..1023 (SAR-ADC Kanal 4) */
void gd_input_master(gd_input *in, int raw1023, gd_app *app);

#endif
