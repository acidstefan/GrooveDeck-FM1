/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * GrooveDeck FM-1 auf FM1 Open - Bedienung und Display.
 * Ersetzt die Oberflaeche von FM1 Open (app/screen.c). Die Bibliothek ruft groove_screen_init einmal und
 * groove_screen_control laufend aus dem Control-Task; von dort kommen Tasten, Regler, MASTER, Akku und LEDs
 * (SDK-Panel-API), gezeichnet wird mit gd_ui_draw in 16-Zeilen-Streifen; der Ton wird hier vorausgerechnet. */
#include "fm1.h"
#include "fm1_engine_control.h"
#include "fm1_device.h"
#include "instrument.h"
#include "gd_fm1open.h"
#include "gd/gd_math.h"
#include <string.h>

gd_app gd_state;
volatile int gd_ready;

/* Build.ps1 verlangt diese Funktionen der Bibliothek im fertigen Abbild (Bluetooth, Stimmung, Projekt).
   GrooveDeck ruft sie (noch) nicht auf; ohne Verweis entfernt der Linker sie. */
extern const void *const volatile gd_keep_native[71];
static const void *const volatile keep_runtime[] = {
    (const void *)fm1_ble_set_role, (const void *)fm1_ble_scan, (const void *)fm1_ble_connect,
    (const void *)fm1_ble_disconnect, (const void *)fm1_ble_get_status, (const void *)fm1_ble_get_device,
    (const void *)fm1_ble_forget_bonds, (const void *)fm1_bluetooth_configure, (const void *)fm1_bluetooth_get_status,
    (const void *)groove_tuning_capture, (const void *)groove_tuning_apply, (const void *)groove_project_apply_edit, (const void *)fm1_engine_output_level,
};

/* ------------------------------------------------------------------ Audio
   Gerechnet wird im Control-Task (Programmcode im Flash, wie die uebrige App), nicht im Audio-Interrupt:
   waehrend Flash-Schreibvorgaengen darf der Interrupt nur Code aus dem internen RAM ausfuehren. Der Interrupt
   (gd_render_block, im RAM) holt nur fertige Bloecke aus einem Ring; ist er leer, gibt es Stille. */
#define GD_RING 16u                     /* 16 x 128 Frames = 46 ms Vorrat */
static int32_t ring[GD_RING][FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static volatile unsigned ring_w, ring_r;
volatile uint32_t gd_underruns;
static float au_l[FM1_BLOCK_FRAMES], au_r[FM1_BLOCK_FRAMES];

FM1_AUDIO_CODE int gd_render_block(int32_t *out)
{
    unsigned i;
    if (ring_r == ring_w) {
        for (i = 0; i < FM1_BLOCK_WORDS; ++i) out[i] = 0;
        ++gd_underruns;
        return 0;
    }
    {
        const int32_t *src = ring[ring_r % GD_RING];
        for (i = 0; i < FM1_BLOCK_WORDS; ++i) out[i] = src[i];
    }
    ring_r = ring_r + 1u;
    return 0;
}

static inline float soft_clip(float x)
{
    const float ax = gd_absf(x);
    if (ax <= 0.8f) return x;
    {
        const float y = 0.8f + 0.2f * gd_tanh((ax - 0.8f) * 5.0f);
        return x < 0.0f ? -y : y;
    }
}

/* Ring auffuellen (Control-Task) */
static void audio_pump(void)
{
    while (ring_w - ring_r < GD_RING) {
        int32_t *dst = ring[ring_w % GD_RING];
        unsigned i;
        gd_app_audio(&gd_state, au_l, au_r, FM1_BLOCK_FRAMES);
        for (i = 0; i < FM1_BLOCK_FRAMES; ++i) {
            dst[2 * i] = (int32_t)(gd_clampf(soft_clip(au_l[i]), -1.0f, 1.0f) * 8388607.0f);
            dst[2 * i + 1] = (int32_t)(gd_clampf(soft_clip(au_r[i]), -1.0f, 1.0f) * 8388607.0f);
        }
        __asm__ volatile("csync" ::: "memory");
        ring_w = ring_w + 1u;
    }
}

/* ------------------------------------------------------------------ Bedienung */
/* SDK-Reihenfolge der Buttons (Bit 0..13) und Drehgeber (0..6) auf GrooveDeck */
static const uint8_t btn_map[14] = { GB_OCTDN, GB_OCTUP, GB_FX, GB_SEL, GB_ENV, GB_LFO, GB_EDIT, GB_GLO,
                                     GB_HOME, GB_SAVE, GB_ARP, GB_SEQ, GB_PLAY, GB_REC };
static const uint8_t enc_map[7] = { GE_SELECT, GE_ALGO, GE_K1, GE_K2, GE_K3, GE_K4, GE_PRESETS };

static uint32_t prev_keys, prev_buttons, last_ms, last_draw, last_bat, last_led;
static int knob_sent = -1000, drawn_stamp = -1, drawn_step = -2;

/* ------------------------------------------------------------------ Display */
static uint16_t strip[16 * GD_LCD_W];
static uint32_t strip_sum[GD_LCD_H / 16];
static int strips_valid;

static uint32_t strip_hash(void)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < 16u * GD_LCD_W; ++i) h = (h ^ strip[i]) * 16777619u;
    return h;
}

static void draw_screen(void)
{
    int y0;
    for (y0 = 0; y0 < GD_LCD_H; y0 += 16) {
        gd_canvas c;
        uint32_t h;
        c.px = strip; c.y0 = y0; c.h = 16;
        gd_ui_draw(&gd_state, &c);
        h = strip_hash();
        if (strips_valid && strip_sum[y0 / 16] == h) continue;   /* unveraenderte Streifen nicht senden */
        strip_sum[y0 / 16] = h;
        if (fm1_screen_blit(0, (unsigned)y0, GD_LCD_W, 16, strip, sizeof strip / sizeof strip[0]) != 0) {
            strips_valid = 0;
            return;
        }
    }
    strips_valid = 1;
}

static void update_leds(uint32_t now)
{
    uint8_t keys[GD_KEYS], btn[GB_COUNT], green;
    unsigned i;
    gd_leds_compute(&gd_state, now, keys, btn, &green);
    for (i = 0; i < 14; ++i) gd_led_level(i, btn[btn_map[i]]);
    for (i = 0; i < GD_KEYS; ++i) gd_led_level(14 + i, keys[i]);
    gd_led_green(green);
}

void groove_screen_init(void)
{
    unsigned i;
    for (i = 0; i < sizeof keep_runtime / sizeof keep_runtime[0]; ++i) if (!keep_runtime[i]) return;
    for (i = 0; i < 71; ++i) if (!gd_keep_native[i]) return;
    fm1_controls_init();
    fm1_screen_init();
    gd_app_init(&gd_state, (float)FM1_SAMPLE_RATE);
    gd_flash_load();
    gd_state.save_hook = gd_flash_save_hook;
    for (i = 0; i < 41; ++i) gd_led_level(i, 0);
    gd_led_green(0);
    prev_keys = prev_buttons = 0;
    last_ms = last_draw = last_bat = last_led = fm1_millis();
    strips_valid = 0;
    ring_w = ring_r = 0;
    gd_ready = 1;
    audio_pump();
}

void groove_screen_control(void)
{
    struct fm1_controls in;
    const uint32_t now = fm1_millis();
    uint32_t diff;
    unsigned i;
    if (!gd_ready) return;
    fm1_controls_read(&in);

    diff = in.buttons ^ prev_buttons;
    for (i = 0; i < 14; ++i)
        if ((diff >> i) & 1u) gd_app_post_button(&gd_state, btn_map[i], (int)((in.buttons >> i) & 1u));
    prev_buttons = in.buttons;
    diff = in.keys ^ prev_keys;
    for (i = 0; i < GD_KEYS; ++i)
        if ((diff >> i) & 1u) gd_app_post_key(&gd_state, (int)i, (int)((in.keys >> i) & 1u));
    prev_keys = in.keys;
    for (i = 0; i < 7; ++i)
        if (in.encoders[i]) gd_app_post_encoder(&gd_state, enc_map[i], in.encoders[i]);

    /* MASTER: Rauschen unter 4/1023 ignorieren */
    if ((int)in.knob - knob_sent > 3 || knob_sent - (int)in.knob > 3) {
        knob_sent = in.knob;
        gd_app_post_master(&gd_state, (float)in.knob / 1023.0f);
    }

    /* Akku alle 2 s: PB1 ueber Spannungsteiler. Annahme (noch am Geraet zu pruefen): Teiler 1:2, Referenz 3,3 V;
       0 % bei 3,40 V, 100 % bei 4,15 V. USB angeschlossen = laedt. */
    if (now - last_bat >= 2000u) {
        const int mv = (int)in.battery * 6600 / 1023;
        last_bat = now;
        gd_app_set_battery(&gd_state, gd_clampi((mv - 3400) * 100 / (4150 - 3400), 0, 100), fm1_device_usb_connected() ? 1 : 0);
    }

    gd_app_tick(&gd_state, (int)(now - last_ms));
    last_ms = now;
    audio_pump();
    gd_midi_poll();
    gd_flash_poll();

    if (now - last_led >= 5u) { last_led = now; update_leds(now); }
    if (((int)gd_state.uiStamp != drawn_stamp || gd_state.seq.currentStep != drawn_step) && now - last_draw >= 40u) {
        drawn_stamp = (int)gd_state.uiStamp;
        drawn_step = gd_state.seq.currentStep;
        last_draw = now;
        draw_screen();
        audio_pump();      /* das Zeichnen dauert: Vorrat gleich wieder auffuellen */
    }
}

/* Von der Bibliothek erwartet; GrooveDeck nutzt dafuer eigene Wege */
void groove_screen_tap(uint32_t now) { (void)now; }
void groove_screen_mmc(unsigned command)
{
    /* MIDI Machine Control: 1 Stop, 2 Play, 3 Deferred Play */
    if (command == 1u && gd_state.seq.playing) { gd_app_post_button(&gd_state, GB_PLAY, 1); gd_app_post_button(&gd_state, GB_PLAY, 0); }
    if ((command == 2u || command == 3u) && !gd_state.seq.playing) { gd_app_post_button(&gd_state, GB_PLAY, 1); gd_app_post_button(&gd_state, GB_PLAY, 0); }
}
