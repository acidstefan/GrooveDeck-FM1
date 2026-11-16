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

enum { PG_HOME = 0, PG_ENV, PG_LFO, PG_EDIT, PG_FX, PG_GLO, PG_ARP, PG_SEL, PG_COUNT };

/* Ereignis aus dem Bedien-Task an den Audio-Task (Einweg-Ringpuffer, ein Schreiber, ein Leser) */
typedef struct { uint8_t type, a; int16_t b; } gd_evt;
enum { EV_KEY = 1, EV_BTN, EV_ENC, EV_MASTER };
#define GD_EVQ 64

typedef struct {
    gd_engine eng;
    gd_seq seq;
    int preset, part, page, sub, stepMode, rec, octave, stepPage;
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
} gd_app;

void gd_app_init(gd_app *a, float sampleRate);
/* Bedien-Task: Ereignisse einreihen (sicher gegenueber dem Audio-Task) */
void gd_app_post_key(gd_app *a, int key, int down);
void gd_app_post_button(gd_app *a, int btn, int down);
void gd_app_post_encoder(gd_app *a, int enc, int delta);
void gd_app_post_master(gd_app *a, float v01);
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
