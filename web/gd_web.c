/* GrooveDeck FM-1 - Browser-Fassung des Bedienfelds (WebAssembly). Dieselbe Kern-Software wie gd_panel und
   die Firmware; Zeichnen und Bedienung aus host/gd_panel_draw.c. Die Webseite (web/index.template.html)
   ruft die exportierten Funktionen auf: Ton blockweise, Bild als ARGB-Puffer, Eingaben als Ereignisse. */
#include "gd_panel_draw.h"

#define EXPORT __attribute__((visibility("default")))

static float outL[4096], outR[4096];

EXPORT void web_init(float sampleRate) { gd_app_init(&app, sampleRate); layout(); gd_app_set_battery(&app, 100, 1); }
EXPORT uint32_t *web_pixels(void) { return px; }
EXPORT int web_width(void) { return PW; }
EXPORT int web_height(void) { return PH; }
EXPORT float *web_left(void) { return outL; }
EXPORT float *web_right(void) { return outR; }

/* n Stereo-Samples (<= 4096) in outL/outR rechnen; MIDI-Ausgang verwerfen */
EXPORT void web_audio(int n)
{
    uint8_t mo[64];
    int done = 0;
    if (n > 4096) n = 4096;
    while (done < n) {
        const int c = n - done < 64 ? n - done : 64;
        gd_app_audio(&app, outL + done, outR + done, c);
        done += c;
    }
    while (gd_app_midi_out_read(&app, mo, sizeof mo) > 0) { }
}
EXPORT void web_tick(int ms) { gd_app_tick(&app, ms); }
EXPORT void web_draw(void) { draw_panel(app.audioMs); }

/* Eingaben */
EXPORT int web_hit(int x, int y) { return hit(x, y); }
EXPORT int web_kind(int i) { return i >= 0 && i < nctl ? ctl[i].kind : -1; }
EXPORT int web_id(int i) { return i >= 0 && i < nctl ? ctl[i].id : -1; }
EXPORT void web_press(int i, int down) { if (i >= 0 && i < nctl) press(i, down); }
EXPORT void web_hold(int i, int down) { if (i >= 0 && i < nctl) pressed[i] = down; }   /* Regler: nur Optik */
EXPORT void web_turn(int enc, int delta) { turn(enc, delta); }
EXPORT float web_master(void) { return master; }
EXPORT void web_set_master(float v) { set_master(v); }
EXPORT int web_find_white(int n) { return find(C_KEY, gd_white_key(n)); }
EXPORT int web_find_black(int n) { return find(C_KEY, gd_black_key(n)); }
EXPORT int web_find_button(int b) { return find(C_BTN, b); }

/* MIDI vom Geraet: nicht zugeordnete Nachrichten (Clock, Start/Stop, Noten auf anderen Kanaelen) an den Kern */
static gd_midi midiIn;
EXPORT int web_find(int kind, int id) { return find(kind, id); }
EXPORT void web_midi_byte(int b) { gd_midi_byte(&midiIn, &app, (uint8_t)b); }
