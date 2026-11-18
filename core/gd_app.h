/* GrooveDeck FM-1 - Bedienlogik (hardwareunabhaengig). Die Zielplattform liefert Tasten, Buttons, Drehgeber und das
   MASTER-Poti und ruft gd_app_audio (Audio-Task) und gd_ui_draw (Display, streifenweise) auf. */
#ifndef GD_APP_H
#define GD_APP_H
#include <stdint.h>
#include "gd_preset.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bedienelemente des FM-1 (Beschriftung auf dem Gehaeuse) */
enum { GB_FX = 0, GB_SEL, GB_ENV, GB_LFO, GB_EDIT, GB_GLO, GB_HOME, GB_SAVE, GB_ARP, GB_SEQ, GB_PLAY, GB_REC,
       GB_OCTDN, GB_OCTUP, GB_COUNT };
enum { GE_SELECT = 0, GE_PRESETS, GE_ALGO, GE_K1, GE_K2, GE_K3, GE_K4, GE_COUNT };
#define GD_KEYS 27
#ifndef GD_KEY_BASE_NOTE
#define GD_KEY_BASE_NOTE 53 /* unterste Taste = F3 (wie die Werks-Firmware) */
#endif

/* Klaviatur: 27 Tasten chromatisch von links, die unterste ist ein F (F3, wie die Werks-Firmware).
   Untere Reihe = 16 weisse Tasten, obere Reihe = 11 schwarze (Lage wie im Foto/Emulator: schwarze Taste
   nach den weissen 1, 2, 3, 5, 6, 8, 9, 10, 12, 13, 15). */
static inline int gd_key_white(int key)   /* Index 0..15 der weissen Taste oder -1 */
{
    static const signed char w[12] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6 };
    if (key < 0 || key >= GD_KEYS || w[key % 12] < 0) return -1;
    return (key / 12) * 7 + w[key % 12];
}
static inline int gd_key_black(int key)   /* Index 0..10 der schwarzen Taste oder -1 */
{
    static const signed char b[12] = { -1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1 };
    if (key < 0 || key >= GD_KEYS || b[key % 12] < 0) return -1;
    return (key / 12) * 5 + b[key % 12];
}
static inline int gd_white_key(int idx)   /* Tastennummer der weissen Taste idx (0..15) */
{
    static const signed char k[7] = { 0, 2, 4, 6, 7, 9, 11 };
    return (idx / 7) * 12 + k[idx % 7];
}
static inline int gd_black_key(int idx)   /* Tastennummer der schwarzen Taste idx (0..10) */
{
    static const signed char k[5] = { 1, 3, 5, 8, 10 };
    return (idx / 5) * 12 + k[idx % 5];
}
/* STEP-Modus: schwarze Tasten 1-11 = Kanaele BD SD TM PC CP CH OH CY C2 BS LD (mit OCT- + OCT+ gehalten: Mute) */

enum { PG_HOME = 0, PG_ENV, PG_LFO, PG_EDIT, PG_FX, PG_GLO, PG_ARP, PG_SEL, PG_COUNT };

/* Ereignis aus dem Bedien-Task an den Audio-Task (Einweg-Ringpuffer, ein Schreiber, ein Leser) */
typedef struct { uint8_t type, a; int16_t b; } gd_evt;
enum { EV_KEY = 1, EV_BTN, EV_ENC, EV_MASTER, EV_MIDI };
#define GD_EVQ 64

typedef struct {
    gd_engine eng;
    gd_seq seq;
    int preset, part, page, sub, stepMode, rec, octave, stepPage;
    int presetSel;          /* PRESETS-Regler: angewaehltes Preset (wird nach kurzer Pause geladen) */
    uint32_t presetTurnMs;  /* audioMs der letzten Drehung */
    int lastNote[2];
    int keyNote[GD_KEYS], keyPart[GD_KEYS];
    int btnDown[GB_COUNT];
    int chanSel[GD_PARTS];
    char msg[40];
    int msgMs;
    unsigned uiStamp;              /* wird bei jeder Aenderung erhoeht (Display neu zeichnen) */
    gd_evt q[GD_EVQ];
    volatile unsigned qw, qr;
    /* Benutzerspeicher: zuletzt gesicherter Stand (die Plattform schreibt ihn in den Flash) */
    uint8_t user[4096];
    int userLen, userValid;
    int (*save_hook)(const uint8_t *data, int len); /* optional: Plattform speichert dauerhaft, 0 = ok */
    /* Zeit im Audio-Task (ms), Halte-Funktionen, Aenderungsmerker */
    uint32_t audioMs, btnDownMs[GB_COUNT];
    int holdDone[GB_COUNT], comboUsed[GB_COUNT];
    int dirty, transpose;
    int octMuteUsed;              /* OCT- + OCT+ gehalten: schon eine Mute-Taste benutzt */
    int flashMs[GD_PARTS];        /* Aufblitzen der Tasten-LEDs beim Ausloesen eines Drums */
    /* MIDI-Eingang (TRS-Buchse, USB): externe Clock */
    int extClock, clockCount;
    uint32_t clockT0Ms, lastClockMs;
    /* MIDI-Ausgang (TRS-Buchse): Clock/Start/Stop als Master, live gespielte Noten. Ein Schreiber (Audio-Task),
       ein Leser (Plattform, gd_app_midi_out_read). */
    uint8_t midiOut[256];
    volatile unsigned moW, moR;
    int moPlaying;
    float moClock;
    /* Akku (von der Plattform gemeldet): -1 = unbekannt */
    int battery, charging;
} gd_app;

void gd_app_init(gd_app *a, float sampleRate);
/* Bedien-Task: Ereignisse einreihen (sicher gegenueber dem Audio-Task) */
void gd_app_post_key(gd_app *a, int key, int down);
void gd_app_post_button(gd_app *a, int btn, int down);
void gd_app_post_encoder(gd_app *a, int enc, int delta);
void gd_app_post_master(gd_app *a, float v01);
/* MIDI-Bytes einspeisen (UART der TRS-Buchse, USB-MIDI). Mehrere Quellen brauchen je einen eigenen Parser. */
typedef struct { uint8_t status, d0, n, sysex; } gd_midi;
void gd_midi_byte(gd_midi *m, gd_app *a, uint8_t byte);
/* Ausgehende MIDI-Bytes abholen (hoechstens cap), Rueckgabe = Anzahl */
int gd_app_midi_out_read(gd_app *a, uint8_t *dst, int cap);
/* Akkustand 0..100 (-1 = unbekannt), laedt = 1 waehrend USB-Strom anliegt */
void gd_app_set_battery(gd_app *a, int percent, int charging);
/* Kurze Meldung im Display (ca. 1,5 s) */
void gd_app_message(gd_app *a, const char *m);
/* LED-Helligkeiten 0..255 fuer den aktuellen Zustand (Bedien-Task, ca. alle 10 ms).
   keys[27] = Tasten-LEDs, btn[GB_COUNT] = Button-LEDs (bei PLAY: weisse STOP-LED), playGreen = gruene PLAY-LED */
void gd_leds_compute(const gd_app *a, uint32_t nowMs, uint8_t keys[GD_KEYS], uint8_t btn[GB_COUNT], uint8_t *playGreen);

/* Audio-Task: Ereignisse anwenden, dann n Samples rechnen (l/r werden ueberschrieben) */
void gd_app_audio(gd_app *a, float *l, float *r, int n);
/* zwei Haelften fuer zwei Kerne: Kern 0 gd_app_audio_voices, Kern 1 gd_engine_render_master */
void gd_app_audio_voices(gd_app *a, gd_bus *bus, int n);
/* Zeit fuer Meldungen weiterzaehlen (Bedien-Task) */
void gd_app_tick(gd_app *a, int ms);
/* gespeicherten Benutzerstand laden (z. B. beim Start aus dem Flash) */
int gd_app_load_user(gd_app *a, const uint8_t *data, int len);

/* Zeichenflaeche: ein Streifen des 240x240-Displays (RGB565). Zeilen y0 .. y0+h-1, Puffer h*240 Pixel. */
#define GD_LCD_W 240
#define GD_LCD_H 240
typedef struct { uint16_t *px; int y0, h; } gd_canvas;
void gd_ui_draw(const gd_app *a, gd_canvas *c);

#ifdef __cplusplus
}
#endif
#endif
