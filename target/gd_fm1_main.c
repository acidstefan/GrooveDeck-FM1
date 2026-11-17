/* GrooveDeck FM-1 - Einstieg der Firmware auf dem FM-1 (JieLi AC791N / WL82, AC79-SDK).
 *
 * STATUS: ENTWURF, NOCH NICHT KOMPILIERT. Die Hardware-Treiber kommen aus fm1-nes (Apache-2.0):
 *   - Display: fm1_display_test_init / fm1_display_write (nes/boot/display_test.c, Werks-Initialisierung)
 *   - Tastenmatrix: fm1_wl82_keyscan_* (nes/src/fm1_wl82_keyscan.c), Rohzeilen inkl. Drehgeber
 *   - MASTER-Poti: fm1_volume_* (nes/src/fm1_volume.c, SAR-ADC Kanal 4)
 *   - Audio: SDK-IIS (ALINK0, Kanal 3, 24 Bit in 32-Bit-Worten, 64 Stereo-Frames je DMA-Haelfte) wie fm1_wl82.c
 * Alles Musikalische steckt in core/ (hardwareunabhaengig, am PC getestet).
 *
 * Aufbau: zwei Tasks
 *   gd_audio (hohe Prioritaet): rechnet Bloecke zu 64 Frames in einen Ringpuffer; die ALINK-IRQ holt sie ab.
 *   gd_ui (niedrig): Tastenmatrix jede Millisekunde, Poti alle 2 ms, Display bei Aenderungen streifenweise.
 */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "os/os_api.h"
#include "system/sys_time.h"
#include "asm/iis.h"
#include "asm/wdt.h"
#include <string.h>

#include "display_test.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_volume.h"

#include "gd_app.h"
#include "gd_input.h"
#include "gd_math.h"

#ifndef GD_LCD_Y_OFFSET
#define GD_LCD_Y_OFFSET 0 /* fm1-nes Laufzeit: 0, Werks-Testbild: 40. Bild verschoben? -> 40 probieren */
#endif
#define GD_SR 44100
#define GD_AUDIO_FRAMES 64           /* = eine DMA-Haelfte (512 Byte) */
#define GD_RING_BLOCKS 16            /* 16 x 1,45 ms = 23 ms Puffer */

const struct irq_info irq_info_table[] = { { -1, -1, -1 } };
const struct task_info task_info_table[] = {
    { "app_core", 15, 4096, 1024 },
    { "sys_event", 29, 512, 0 },
    { "systimer", 14, 256, 0 },
    { "sys_timer", 9, 512, 128 },
    { "gd_audio", 20, 4096, 0 },
    { "gd_ui", 8, 4096, 0 },
    { 0, 0, 0, 0, 0 },
};

static gd_app app;
static gd_input input;
static fm1_wl82_keyscan scanner;
static fm1_volume volume;

/* ------------------------------------------------------------------ Audio */
static int32_t ring[GD_RING_BLOCKS][GD_AUDIO_FRAMES * 2];
static volatile unsigned ring_w, ring_r;
volatile uint32_t gd_underruns, gd_audio_irqs, gd_render_us_max;

static void iis_output(void *ctx, u8 *data, int len, u8 channel)
{
    (void)ctx;
    (void)channel;
    if (!data || len != (int)sizeof ring[0]) { if (data && len > 0) memset(data, 0, (size_t)len); return; }
    ++gd_audio_irqs;
    if (ring_r == ring_w) { memset(data, 0, (size_t)len); ++gd_underruns; return; }
    memcpy(data, ring[ring_r % GD_RING_BLOCKS], sizeof ring[0]);
    ring_r = ring_r + 1;
}

___interrupt
static void gd_alink_isr(void)
{
    iis_irq_handler(0); /* SDK: ruft iis_output fuer die fertige DMA-Haelfte */
}

static int audio_start(void)
{
    struct iis_platform_data pd;
    memset(&pd, 0, sizeof pd);
    pd.port_sel = IIS_PORTC;
    pd.channel_out = (u8)(1u << 3);   /* Werks-Route: ALINK0 Kanal 3 (PC6 = DATA) */
    pd.data_width = pd.channel_out;   /* 24 Bit */
    pd.mclk_output = 1;
    pd.update_edge = 0;
    pd.f32e = 0;
    pd.sr_points = 128;               /* SDK: 128 * 4 Byte = 512 Byte je Haelfte */
    if (iis_open(&pd, 0)) return -1;
    iis_set_dec_data_handler(0, iis_output, 0);
    if (iis_set_sample_rate(GD_SR, 0)) return -2;
    request_irq(IRQ_ALNK_IDX, 3, gd_alink_isr, 0);   /* wie die Werks-Firmware: IRQ 11, Prioritaet 3, Kern 0 */
    iis_channel_on(pd.channel_out, 0);
    return 0;
}

__attribute__((noinline, used))
static void gd_audio_task(void *arg)
{
    float l[GD_AUDIO_FRAMES], r[GD_AUDIO_FRAMES];
    (void)arg;
    if (audio_start()) { for (;;) os_time_dly(100); }
    for (;;) {
        while (ring_w - ring_r < GD_RING_BLOCKS) {
            int32_t *dst = ring[ring_w % GD_RING_BLOCKS];
            const uint32_t t0 = timer_get_ms();
            int i;
            gd_app_audio(&app, l, r, GD_AUDIO_FRAMES);
            for (i = 0; i < GD_AUDIO_FRAMES; ++i) {
                dst[2 * i] = (int32_t)(gd_clampf(l[i], -1.0f, 1.0f) * 8388607.0f);
                dst[2 * i + 1] = (int32_t)(gd_clampf(r[i], -1.0f, 1.0f) * 8388607.0f);
            }
            ring_w = ring_w + 1;
            {
                const uint32_t dt = (timer_get_ms() - t0) * 1000u;
                if (dt > gd_render_us_max) gd_render_us_max = dt;
            }
        }
        wdt_clear();
        os_time_dly(1);
    }
}

/* --------------------------------------------------------------- Display */
static uint16_t strip[16 * GD_LCD_W];
static uint8_t wire[16 * GD_LCD_W * 2];

static int lcd_cmd(uint8_t c, const uint8_t *p, size_t n)
{
    if (fm1_display_write(0, &c, 1)) return -1;
    return n ? fm1_display_write(1, p, n) : 0;
}

static void draw_screen(void)
{
    static const uint8_t cols[4] = { 0, 0, 0, 239 };
    int y0, i;
    if (lcd_cmd(0x2a, cols, 4)) return;
    for (y0 = 0; y0 < GD_LCD_H; y0 += 16) {
        gd_canvas c;
        const unsigned ya = (unsigned)(y0 + GD_LCD_Y_OFFSET), yb = ya + 15u;
        const uint8_t rows[4] = { (uint8_t)(ya >> 8), (uint8_t)ya, (uint8_t)(yb >> 8), (uint8_t)yb };
        c.px = strip; c.y0 = y0; c.h = 16;
        gd_ui_draw(&app, &c);
        for (i = 0; i < 16 * GD_LCD_W; ++i) { wire[2 * i] = (uint8_t)(strip[i] >> 8); wire[2 * i + 1] = (uint8_t)strip[i]; }
        if (lcd_cmd(0x2b, rows, 4) || lcd_cmd(0x2c, 0, 0) || fm1_display_write(1, wire, sizeof wire)) return;
    }
}

/* Rueckruf des Display-Treibers (fm1-nes): SPI1/DMA-Register in den Startphasen. Nur zur Fehlersuche,
   z. B. per Emulator-Speicheransicht. */
volatile uint32_t gd_lcd_regs[5][FM1_LCD_REGISTER_COUNT];
void fm1_display_snapshot(unsigned phase, const uint32_t registers[FM1_LCD_REGISTER_COUNT])
{
    unsigned i;
    if (phase >= 5u || !registers) return;
    for (i = 0; i < FM1_LCD_REGISTER_COUNT; ++i) gd_lcd_regs[phase][i] = registers[i];
}

static uint32_t now_us(void *ctx) { (void)ctx; return timer_get_ms() * 1000u; }

__attribute__((noinline, used))
static void gd_ui_task(void *arg)
{
    uint8_t rows[FM1_STOCK_SCAN_ROWS];
    unsigned drawn = ~0u;
    uint32_t last_draw = 0, last_tick = timer_get_ms(), last_vol = 0;
    int scan_ok, step = -2;
    (void)arg;
    fm1_display_test_init();
    scan_ok = fm1_wl82_keyscan_start(&scanner, 0, now_us) == 0;
    fm1_volume_start(&volume);
    for (;;) {
        const uint32_t now = timer_get_ms();
        if (scan_ok && fm1_wl82_keyscan_raw(&scanner, rows) == 0) gd_input_scan(&input, rows, &app);
        if (now - last_vol >= 2u) {
            last_vol = now;
            fm1_volume_tick(&volume);
            if (volume.valid) gd_input_master(&input, volume.accepted, &app);
        }
        gd_app_tick(&app, (int)(now - last_tick));
        last_tick = now;
        /* neu zeichnen bei Aenderung oder Step-Wechsel, hoechstens ca. 30 Bilder/s */
        if ((app.uiStamp != drawn || app.seq.currentStep != step) && now - last_draw >= 33u) {
            drawn = app.uiStamp;
            step = app.seq.currentStep;
            last_draw = now;
            draw_screen();
        }
        wdt_clear();
        os_time_dly(1);
    }
}

void app_main(void)
{
    gd_app_init(&app, (float)GD_SR);
    gd_input_init(&input);
    task_create(gd_audio_task, NULL, "gd_audio");
    task_create(gd_ui_task, NULL, "gd_ui");
}
