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
#ifndef GD_FLASH_SAVE
#define GD_FLASH_SAVE 0   /* build.py --flash-save schaltet ein (noch ungetestet) */
#endif
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "os/os_api.h"
#include "system/sys_time.h"
#include "asm/iis.h"
#include "asm/wdt.h"
#if GD_FLASH_SAVE
#include "asm/sfc_norflash_api.h"
#include "device/ioctl_cmds.h"
#endif
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
/* GD_ENGINE_DIV 2 (build.py --emu-light): Engine rechnet mit halber Abtastrate, Ausgabe wird linear auf 44,1 kHz
   hochgerechnet. Halbiert die Rechenarbeit, klingt aber dumpfer - nur fuer langsame PCs im Emulator gedacht. */
#ifndef GD_ENGINE_DIV
#define GD_ENGINE_DIV 1
#endif
#define GD_ENGINE_FRAMES (GD_AUDIO_FRAMES / GD_ENGINE_DIV)
/* GD_DUAL_CORE 1: Stimmen und Master-Kette in zwei Tasks (je ein Kern), build.py --single-core schaltet ab */
#ifndef GD_DUAL_CORE
#define GD_DUAL_CORE 1
#endif
#define GD_RING_BLOCKS 32            /* 32 x 1,45 ms = 46 ms Puffer (Reserve, solange das Display ueber SPI sendet) */

const struct irq_info irq_info_table[] = { { -1, -1, -1 } };
const struct task_info task_info_table[] = {
    { "app_core", 15, 4096, 1024 },
    { "sys_event", 29, 512, 0 },
    { "systimer", 14, 256, 0 },
    { "sys_timer", 9, 512, 128 },
    { "gd_audio", 20, 4096, 0 },
    { "gd_master", 20, 4096, 0 },
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
volatile uint32_t gd_underruns, gd_audio_irqs, gd_render_us_max, gd_scan_errors;

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

/* ---- Direkter ALNK0-Treiber fuer den fm1-emulator ----
   Der Emulator bildet ALNK0 nur mit wenigen Registern nach (CON0/CON1/Pending/CON3/DMA-Adresse/Laenge);
   der SDK-Treiber beschreibt weitere und bricht dort ab. Die Emulator-Fassung (build.py) setzt
   gd_emu_flag auf GD_EMU_ON; dann wird dieser Treiber benutzt. Auf dem Geraet laeuft der SDK-Treiber. */
/* Kennwert statt 0, damit der Wert sicher im Flash (.rodata) liegt und nicht in .bss */
#define GD_EMU_OFF 0x47444530u /* "GDE0" */
#define GD_EMU_ON 0x47444531u  /* "GDE1" */
__attribute__((used)) const volatile uint32_t gd_emu_flag = GD_EMU_OFF;
#define GD_EMU (gd_emu_flag == GD_EMU_ON)

#define ALNK_BASE 0x12E00u
#define ALNK_CON0 (*(volatile uint16_t *)(ALNK_BASE + 0x00u))
#define ALNK_CON1 (*(volatile uint16_t *)(ALNK_BASE + 0x04u))
#define ALNK_PEND (*(volatile uint8_t *)(ALNK_BASE + 0x08u))
#define ALNK_CON3 (*(volatile uint8_t *)(ALNK_BASE + 0x0Cu))
#define ALNK_ADR (*(volatile uint32_t *)(ALNK_BASE + 0x1Cu))
#define ALNK_LEN (*(volatile uint16_t *)(ALNK_BASE + 0x20u))
#define ALNK_DMA_ON 0x0800u
#define ALNK_ACTIVE_HALF 0x8000u
#define ALNK_HALF_PENDING 0x80u

static int32_t alnk_buf[2][GD_AUDIO_FRAMES * 2] __attribute__((aligned(4)));

static void fill_half(int32_t *dst)
{
    if (ring_r == ring_w) { memset(dst, 0, sizeof alnk_buf[0]); ++gd_underruns; return; }
    memcpy(dst, ring[ring_r % GD_RING_BLOCKS], sizeof alnk_buf[0]);
    ring_r = ring_r + 1;
}

___interrupt
static void gd_alnk_direct_isr(void)
{
    if (ALNK_PEND & ALNK_HALF_PENDING) {
        /* die gerade fertig gewordene Haelfte ist die nicht aktive */
        const unsigned active = (ALNK_CON0 & ALNK_ACTIVE_HALF) ? 1u : 0u;
        ++gd_audio_irqs;
        fill_half(alnk_buf[active ^ 1u]);
        ALNK_PEND = (uint8_t)(ALNK_PEND | (ALNK_HALF_PENDING >> 4));   /* quittieren */
    }
}

static int audio_start_direct(void)
{
    memset(alnk_buf, 0, sizeof alnk_buf);
    ALNK_CON0 = 0;
    ALNK_CON1 = 0x5000u;
    ALNK_CON3 = 0x83u;
    ALNK_ADR = (uint32_t)(uintptr_t)alnk_buf;
    ALNK_LEN = (uint16_t)(GD_AUDIO_FRAMES * 2);
    request_irq(IRQ_ALNK_IDX, 3, gd_alnk_direct_isr, 0);
    ALNK_CON0 = (uint16_t)(0x0180u | ALNK_DMA_ON);
    return 0;
}

static int audio_start(void)
{
    struct iis_platform_data pd;
    if (GD_EMU) return audio_start_direct();
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

/* Weiche Begrenzung am Ausgang: bis 0,8 linear, darueber sanft gegen 1,0 statt hartem Abschneiden
   (MASTER ganz aufgedreht = +6 dB, laute Presets wuerden sonst kratzig uebersteuern). */
static inline float soft_clip(float x)
{
    const float ax = gd_absf(x);
    if (ax <= 0.8f) return x;
    {
        const float y = 0.8f + 0.2f * gd_tanh((ax - 0.8f) * 5.0f);
        return x < 0.0f ? -y : y;
    }
}

/* Einen Engine-Block (GD_ENGINE_FRAMES) als DMA-Block in den Ring schreiben */
static float out_pl, out_pr;   /* letztes Sample des vorigen Blocks (fuer GD_ENGINE_DIV 2) */
static void out_block(const float *l, const float *r)
{
    int32_t *dst = ring[ring_w % GD_RING_BLOCKS];
    /* Geraet: 24 Bit rechtsbuendig; Emulator gibt die Worte als 32 Bit aus -> linksbuendig */
    const float full = GD_EMU ? 2147483392.0f : 8388607.0f;
    int i;
    for (i = 0; i < GD_AUDIO_FRAMES; ++i) {
        float sl, sr;
        if (GD_ENGINE_DIV == 1) { sl = l[i]; sr = r[i]; }
        else if (i & 1) { sl = l[i >> 1]; sr = r[i >> 1]; }
        else {
            const int k = i >> 1;
            sl = 0.5f * ((k ? l[k - 1] : out_pl) + l[k]);
            sr = 0.5f * ((k ? r[k - 1] : out_pr) + r[k]);
        }
        dst[2 * i] = (int32_t)(gd_clampf(soft_clip(sl), -1.0f, 1.0f) * full);
        dst[2 * i + 1] = (int32_t)(gd_clampf(soft_clip(sr), -1.0f, 1.0f) * full);
    }
    out_pl = l[GD_ENGINE_FRAMES - 1];
    out_pr = r[GD_ENGINE_FRAMES - 1];
    ring_w = ring_w + 1;
}

#if GD_DUAL_CORE
/* Zwei Kerne als Pipeline: gd_audio rechnet Sequencer und Stimmen in Bus-Bloecke zu 16 Samples (wie
   gd_seq_render), gd_master holt sie ab und rechnet FX, Soundgoodizer und Master. Das Betriebssystem verteilt
   beide Tasks auf die zwei Kerne; die Semaphoren takten sie. Ergebnis gleich wie gd_app_audio. */
/* 64 Bus-Bloecke = 1024 Samples (23 ms bei 44,1 kHz) Vorlauf: Wecken ueber Kerngrenzen kann bis zu einem
   Betriebssystem-Tick dauern; mit nur wenigen Bloecken Vorlauf bremste das die Pipeline (Takt zu langsam). */
#define GD_VBUS 64
#define GD_VCHUNK 16
static gd_bus vbus[GD_VBUS];
static OS_SEM sem_full, sem_free;

__attribute__((noinline, used))
static void gd_audio_task(void *arg)
{
    unsigned w = 0;
    (void)arg;
    if (audio_start()) { for (;;) os_time_dly(100); }
    for (;;) {
        os_sem_pend(&sem_free, 0);
        gd_app_audio_voices(&app, &vbus[w % GD_VBUS], GD_VCHUNK);
        ++w;
        os_sem_post(&sem_full);
    }
}

__attribute__((noinline, used))
static void gd_master_task(void *arg)
{
    float l[GD_ENGINE_FRAMES], r[GD_ENGINE_FRAMES];
    unsigned rd = 0;
    (void)arg;
    for (;;) {
        int k, i;
        while (ring_w - ring_r >= GD_RING_BLOCKS) { wdt_clear(); os_time_dly(1); }
        {
            const uint32_t t0 = timer_get_ms();
            for (k = 0; k < GD_ENGINE_FRAMES; k += GD_VCHUNK) {
                os_sem_pend(&sem_full, 0);
                for (i = 0; i < GD_VCHUNK; ++i) l[k + i] = r[k + i] = 0.0f;
                gd_engine_render_master(&app.eng, &vbus[rd % GD_VBUS], l + k, r + k);
                ++rd;
                os_sem_post(&sem_free);
            }
            out_block(l, r);
            {
                const uint32_t dt = (timer_get_ms() - t0) * 1000u;
                if (dt > gd_render_us_max) gd_render_us_max = dt;
            }
        }
    }
}
#else
__attribute__((noinline, used))
static void gd_audio_task(void *arg)
{
    float l[GD_ENGINE_FRAMES], r[GD_ENGINE_FRAMES];
    (void)arg;
    if (audio_start()) { for (;;) os_time_dly(100); }
    for (;;) {
        while (ring_w - ring_r < GD_RING_BLOCKS) {
            const uint32_t t0 = timer_get_ms();
            gd_app_audio(&app, l, r, GD_ENGINE_FRAMES);
            out_block(l, r);
            {
                const uint32_t dt = (timer_get_ms() - t0) * 1000u;
                if (dt > gd_render_us_max) gd_render_us_max = dt;
            }
        }
        wdt_clear();
        os_time_dly(1);
    }
}
#endif

/* ------------------------------------------------------- Speichern im Flash
   SAVE (gehalten) legt den Benutzerstand in den letzten 64 KB des Flash (zwei 4-KB-Sektoren), wie
   sdram_config_save_flash.c im SDK: norflash_open, Sektor loeschen, norflash_write, mit norflash_origin_read
   pruefen. Beim Start wird er gelesen und steht als Benutzerstand (SEL-Seite) bereit. */
#if GD_FLASH_SAVE
#define GD_SAVE_MAGIC 0x31534447u   /* "GDS1" */
#define GD_SAVE_SECTORS 2u
extern const char text_rodata_end[];
static uint32_t flash_user_addr;     /* 0 = kein gueltiger Bereich */
static uint8_t flash_buf[GD_SAVE_SECTORS * 4096u] __attribute__((aligned(4)));
volatile uint32_t gd_flash_errors;

static uint32_t sum32(const uint8_t *d, int n)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; ++i) h = (h ^ d[i]) * 16777619u;
    return h;
}

/* Diagnose (im Emulator-Abbild lesbar): [0] Kapazitaet, [1] Programmende im Flash, [2] Rueckgabe open,
   [3] Rueckgabe Loeschen, [4] Rueckgabe Schreiben, [5] erste abweichende Position beim Zuruecklesen + 1 */
volatile uint32_t gd_flash_diag[6];

static void flash_init(void)
{
    u32 cap = 0, end;
    gd_flash_diag[2] = (uint32_t)norflash_open(NULL, NULL, NULL);
    norflash_ioctl(NULL, IOCTL_GET_CAPACITY, (u32)&cap);
    gd_flash_diag[0] = cap;
    if (cap < 0x40000u || cap > 0x1000000u) cap = 0x100000u;   /* FM-1: 1 MB (JEDEC 85 60 14) */
    flash_user_addr = cap - 0x10000u;
    /* nie in den Bereich des Programms schreiben: Ende von Code/Konstanten + 64 KB Abstand fuer die danach
       abgelegten Initialwerte. XIP bildet Flash 0x4000 auf 0x02000000 ab, falls die SDK-Umrechnung nichts liefert. */
    end = cpu_addr2flash_addr((u32)(uintptr_t)text_rodata_end);
    if (end == 0 || end > cap) end = (u32)(uintptr_t)text_rodata_end - 0x02000000u + 0x4000u;
    gd_flash_diag[1] = end;
    if (end + 0x10000u > flash_user_addr) flash_user_addr = 0;
}

static void flash_load_user(void)
{
    uint32_t hdr[4];
    if (!flash_user_addr) return;
    norflash_origin_read((u8 *)hdr, flash_user_addr, sizeof hdr);
    if (hdr[0] != GD_SAVE_MAGIC || hdr[1] == 0 || hdr[1] > sizeof flash_buf - 16u) return;
    norflash_origin_read(flash_buf, flash_user_addr + 16u, hdr[1]);
    if (sum32(flash_buf, (int)hdr[1]) != hdr[2]) return;
    gd_app_load_user(&app, flash_buf, (int)hdr[1]);
}

static int flash_save_hook(const uint8_t *data, int len)
{
    uint32_t *hdr = (uint32_t *)flash_buf;
    uint32_t total, i;
    static uint8_t check[256];
    if (!flash_user_addr || len <= 0 || (uint32_t)len > sizeof flash_buf - 16u) { ++gd_flash_errors; return -1; }
    hdr[0] = GD_SAVE_MAGIC; hdr[1] = (uint32_t)len; hdr[2] = sum32(data, len); hdr[3] = 0;
    memcpy(flash_buf + 16, data, (size_t)len);
    total = (16u + (uint32_t)len + 3u) & ~3u;
    for (i = 0; i < GD_SAVE_SECTORS; ++i)
        gd_flash_diag[3] = (uint32_t)norflash_ioctl(NULL, IOCTL_ERASE_SECTOR, flash_user_addr + i * 4096u);
    gd_flash_diag[4] = (uint32_t)norflash_write(NULL, flash_buf, total, flash_user_addr);
    for (i = 0; i < total; i += sizeof check) {   /* zuruecklesen und vergleichen */
        const uint32_t n = total - i < sizeof check ? total - i : sizeof check;
        norflash_origin_read(check, flash_user_addr + i, n);
        if (memcmp(check, flash_buf + i, n)) { gd_flash_diag[5] = i + 1; ++gd_flash_errors; return -1; }
    }
    return 0;
}
#endif

/* --------------------------------------------------------------- Display */
static uint16_t strip[16 * GD_LCD_W];
static uint8_t wire[16 * GD_LCD_W * 2];

static int lcd_cmd(uint8_t c, const uint8_t *p, size_t n)
{
    if (fm1_display_write(0, &c, 1)) return -1;
    return n ? fm1_display_write(1, p, n) : 0;
}

/* Nur Streifen uebertragen, die sich seit dem letzten Bild geaendert haben (Pruefsumme je 16-Zeilen-Streifen).
   Die SPI-Uebertragung wartet aktiv auf das Ende; ein volles Bild kostet so ein Vielfaches der Rechenzeit
   eines Streifens. Beim Spielen aendern sich meist nur Step-Zeile, Pegelanzeige und Kopfzeile. */
static uint32_t strip_sum[GD_LCD_H / 16];
static int strips_valid;

static uint32_t strip_hash(void)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 16 * GD_LCD_W; ++i) h = (h ^ strip[i]) * 16777619u;
    return h;
}

static void draw_screen(void)
{
    static const uint8_t cols[4] = { 0, 0, 0, 239 };
    int y0, i, cols_sent = 0;
    for (y0 = 0; y0 < GD_LCD_H; y0 += 16) {
        gd_canvas c;
        const unsigned ya = (unsigned)(y0 + GD_LCD_Y_OFFSET), yb = ya + 15u;
        const uint8_t rows[4] = { (uint8_t)(ya >> 8), (uint8_t)ya, (uint8_t)(yb >> 8), (uint8_t)yb };
        c.px = strip; c.y0 = y0; c.h = 16;
        gd_ui_draw(&app, &c);
        {
            const uint32_t h = strip_hash();
            if (strips_valid && strip_sum[y0 / 16] == h) continue;
            strip_sum[y0 / 16] = h;
        }
        if (!cols_sent) { if (lcd_cmd(0x2a, cols, 4)) { strips_valid = 0; return; } cols_sent = 1; }
        for (i = 0; i < 16 * GD_LCD_W; ++i) { wire[2 * i] = (uint8_t)(strip[i] >> 8); wire[2 * i + 1] = (uint8_t)strip[i]; }
        if (lcd_cmd(0x2b, rows, 4) || lcd_cmd(0x2c, 0, 0) || fm1_display_write(1, wire, sizeof wire)) {
            strips_valid = 0;   /* Fehler: beim naechsten Mal alles neu */
            return;
        }
    }
    strips_valid = 1;
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

/* MASTER-Poti starten: im Emulator eine Fassung von fm1_volume_start (fm1-nes, Apache-2.0) ohne die zwei
   Zugriffe, die der Emulator nicht nachbildet (PB-Register +0x20 und ANA PLL_CON1 0x119A4). Auf dem Geraet
   unveraendert fm1_volume_start. Abfrage danach in beiden Faellen mit fm1_volume_tick. */
#define GD_RD(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define GD_CHG(a, clr, set) (GD_RD(a) = (GD_RD(a) & ~(uint32_t)(clr)) | (uint32_t)(set))
static void gd_volume_start(fm1_volume *s)
{
    if (!GD_EMU) { fm1_volume_start(s); return; }
    memset(s, 0, sizeof(*s));
    if (GD_RD(0x13100u) & 0x30u) { s->errors = 1; return; }
    GD_CHG(0x50048u, 0, 0x40); GD_CHG(0x5004Cu, 0x40, 0);   /* PB6: Eingang, digital aus */
    GD_CHG(0x50050u, 0x40, 0); GD_CHG(0x50054u, 0x40, 0);   /* keine Pull-ups/-downs */
    GD_CHG(0x11900u, 0x3c0feu, 0x20005u);
    GD_RD(0x13100u) = 0;
    s->running = 1;
    GD_RD(0x13100u) = 0xf44eu; GD_CHG(0x13100u, 0, 0x10); GD_CHG(0x13100u, 0, 0x40);
}

/* ------------------------------------------------------------------ Tastenmatrix + LEDs
   Abtastung wie fm1_wl82_keyscan_raw (fm1-nes, Apache-2.0, DMA-Betriebsart), zusaetzlich werden nach jeder
   Latch-Flanke die vier LED-Leitungen fuer die jetzt gewaehlte Spalte gesetzt (PA9, PA10, PH6, PH9 = Zeilen 1-4,
   Leitung high = LED an). Helligkeit per Sigma-Delta ueber die Abtastdurchlaeufe (etwa 1 kHz). */
#define KS_PA 0x50000u
#define KS_PB 0x50040u
#define KS_PH 0x501c0u
#define KS_SPI 0x11e00u
#define KS_LATCH 2u
static uint8_t led_grid[GD_LED_COLS][GD_LED_LINES];
static uint16_t led_acc[GD_LED_COLS][GD_LED_LINES];
__attribute__((aligned(64), used)) static uint8_t gd_key_dma[64];

static void led_lines(int col)
{
    uint32_t a = 0, h = 0;
    int l;
    for (l = 0; l < GD_LED_LINES; ++l) {
        uint16_t *acc = &led_acc[col][l];
        *acc = (uint16_t)(*acc + led_grid[col][l]);
        if (*acc >= 255u) {
            *acc = (uint16_t)(*acc - 255u);
            if (l == 0) a |= 1u << 9; else if (l == 1) a |= 1u << 10; else if (l == 2) h |= 1u << 6; else h |= 1u << 9;
        }
    }
    GD_CHG(KS_PA, 0x600u, a);
    GD_CHG(KS_PH, 0x240u, h);
}

static int ks_shift(uint16_t word)
{
    uint32_t start;
    GD_RD(KS_SPI) = GD_RD(KS_SPI) | 0x4000u;
    gd_key_dma[0] = (uint8_t)(word >> 8);
    gd_key_dma[1] = (uint8_t)word;
    __asm__ volatile("csync" ::: "memory");
    GD_RD(KS_SPI + 12) = (uint32_t)(uintptr_t)gd_key_dma;
    GD_RD(KS_SPI + 16) = 2;
    GD_CHG(KS_PA, KS_LATCH, 0);              /* Latch erst nach dem Start der Uebertragung runter (wie Werk) */
    /* Auf das Ende warten; die Uhr nur alle 256 Abfragen lesen (timer_get_ms ist teuer) */
    start = now_us(0);
    for (;;) {
        unsigned k;
        for (k = 0; k < 256u; ++k) if (GD_RD(KS_SPI) & 0x8000u) break;
        if (k < 256u) break;
        if ((uint32_t)(now_us(0) - start) >= 20000u) {
            if (GD_RD(KS_SPI) & 0x8000u) break;   /* inzwischen fertig (Task war nur verdraengt): zaehlt */
            return -1;
        }
    }
    GD_CHG(KS_SPI, 0, 0x4000u);
    return 0;
}

static int gd_scan(uint8_t rows[FM1_STOCK_SCAN_ROWS])
{
    unsigned i;
    for (i = 0; i < FM1_STOCK_SCAN_ROWS; ++i) {
        uint16_t word;
        rows[scanner.row] = fm1_stock_pack_columns(GD_RD(KS_PA + 4), GD_RD(KS_PB + 4));
        GD_CHG(KS_PA, 0, KS_LATCH);          /* jetzt ist Spalte scanner.row gewaehlt */
        led_lines(scanner.row);
        if (++scanner.row == FM1_STOCK_SCAN_ROWS) scanner.row = 0;
        word = (uint16_t)(0xffffu ^ (1u << scanner.row));
        if (scanner.row < 2) word &= (uint16_t)~(0x800u << scanner.row);
        if (ks_shift(word)) return -1;
    }
    return 0;
}

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
    gd_volume_start(&volume);
    for (;;) {
        const uint32_t now = timer_get_ms();
        if (scan_ok) {
            uint8_t keys[GD_KEYS], btn[GB_COUNT], green;
            gd_leds_compute(&app, now, keys, btn, &green);
            gd_input_led_grid(keys, btn, green, led_grid);
            /* Zeitueberschreitung (z. B. weil die Audio-Tasks den Bedien-Task verdraengt haben): diese Abtastung
               verwerfen und beim naechsten Durchlauf weitermachen - nie dauerhaft abschalten */
            if (gd_scan(rows) == 0) gd_input_scan(&input, rows, &app);
            else ++gd_scan_errors;
        }
        if (now - last_vol >= 2u) {
            last_vol = now;
            fm1_volume_tick(&volume);
            if (volume.valid) gd_input_master(&input, volume.accepted, &app);
        }
        gd_app_tick(&app, (int)(now - last_tick));
        last_tick = now;
        /* neu zeichnen bei Aenderung oder Step-Wechsel, hoechstens 20 Bilder/s (das Display wartet aktiv) */
        if ((app.uiStamp != drawn || app.seq.currentStep != step) && now - last_draw >= 50u) {
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
    gd_app_init(&app, (float)GD_SR / (float)GD_ENGINE_DIV);
    gd_input_init(&input);
#if GD_FLASH_SAVE
    flash_init();
    flash_load_user();
    app.save_hook = flash_save_hook;
#endif
#if GD_DUAL_CORE
    os_sem_create(&sem_free, GD_VBUS);
    os_sem_create(&sem_full, 0);
    task_create(gd_master_task, NULL, "gd_master");
#endif
    task_create(gd_audio_task, NULL, "gd_audio");
    task_create(gd_ui_task, NULL, "gd_ui");
}
