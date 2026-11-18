/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * GrooveDeck FM-1 auf FM1 Open - Preset-Paket im Nutzerspeicher.
 *
 * Die Firmware enthaelt nur einen kleinen Preset-Satz (core/gd_presets_mini.c). Alle Song-Presets liegen als
 * Paket im Nutzerspeicher ab GD_PACK_BASE (davor der Benutzerstand, project.c). Format: tools/gen_presets_pack.py.
 * Aufgespielt wird es per USB-MIDI-SysEx (fm1open/uploader/index.html):
 *
 *   F0 7D 47 44 01 n0..n4 F7            Beginn, Gesamtlaenge (5 x 7 Bit, niedrigstes zuerst)
 *   F0 7D 47 44 02 o0..o4 daten.. F7    Daten ab Offset o, je 7 Byte als 8 Byte (erst die oberen Bits)
 *   F0 7D 47 44 03 F7                   Ende: Paket pruefen und aktivieren
 *   F0 7D 47 44 04 F7                   Abfrage: Anzahl Presets im Paket
 *   Antwort: F0 7D 47 44 7F befehl status w0..w4 F7   (status 0 ok, 1 Fehler, 2 anderer Offset erwartet: w)
 *
 * fm1_app_sysex laeuft im Routing-Task und kopiert nur; Flash-Zugriffe macht gd_pack_poll im Control-Task. */
#include "gd_fm1open.h"
#include "gd/gd_preset.h"
#include "gd/gd_fmt.h"
#include <string.h>

#define GD_PACK_BASE 8192u
#define GD_PACK_MAX_COUNT 2000u

static struct { uint8_t bytes[300]; volatile unsigned length; unsigned source; } in;
static uint32_t pack_count, pack_meta, pack_blob, pack_total;
static uint32_t rx_total, rx_next, erased_to;
static int receiving, last_pct = -1;

static void barrier(void) { __asm__ volatile("csync" ::: "memory"); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t u35(const uint8_t *p) { uint32_t v = 0; int i; for (i = 4; i >= 0; --i) v = (v << 7) | (p[i] & 0x7Fu); return v; }

/* ---------------------------------------------------------------- Quelle fuer den Kern */
static int src_meta(int index, gd_preset_meta *m)
{
    uint8_t r[32];
    if (fm1_flash_read(GD_PACK_BASE + pack_meta + (uint32_t)index * 32u, r, sizeof r) != FM1_FLASH_OK) return -1;
    m->genre = r[6];
    m->sub = r[7];
    memcpy(m->name, r + 8, sizeof m->name);
    return 0;
}

static int src_read(int index, uint8_t *buf, int cap)
{
    uint8_t r[8];
    uint32_t off, len;
    if (fm1_flash_read(GD_PACK_BASE + pack_meta + (uint32_t)index * 32u, r, sizeof r) != FM1_FLASH_OK) return -1;
    off = rd32(r);
    len = r[4] | (uint32_t)r[5] << 8;
    if (!len || len > (uint32_t)cap || pack_blob + off + len > pack_total) return -1;
    if (fm1_flash_read(GD_PACK_BASE + pack_blob + off, buf, len) != FM1_FLASH_OK) return -1;
    return (int)len;
}

static gd_preset_source source = { 0, src_meta, src_read };

/* Paket pruefen und als Quelle setzen. Rueckgabe: Anzahl Presets oder 0 */
static uint32_t pack_open(void)
{
    uint8_t h[32], b[256];
    uint32_t total, pos, hash = 0x811C9DC5u;
    gd_preset_set_external(0);
    pack_count = 0;
    if (fm1_flash_size() < GD_PACK_BASE + 32u) return 0;
    if (fm1_flash_read(GD_PACK_BASE, h, sizeof h) != FM1_FLASH_OK || memcmp(h, "GDP1", 4)) return 0;
    total = rd32(h + 8);
    if (rd32(h + 4) == 0 || rd32(h + 4) > GD_PACK_MAX_COUNT || total < 32u || total > fm1_flash_size() - GD_PACK_BASE) return 0;
    if (rd32(h + 16) != 32u || rd32(h + 20) != 32u + rd32(h + 4) * 32u || rd32(h + 20) > total) return 0;
    for (pos = 16; pos < total; ) {                       /* FNV-1a ueber [16, total) */
        const uint32_t n = total - pos < sizeof b ? total - pos : (uint32_t)sizeof b;
        uint32_t i;
        if (fm1_flash_read(GD_PACK_BASE + pos, b, n) != FM1_FLASH_OK) return 0;
        for (i = 0; i < n; ++i) hash = (hash ^ b[i]) * 0x01000193u;
        pos += n;
    }
    if (hash != rd32(h + 12)) return 0;
    pack_count = rd32(h + 4);
    pack_meta = rd32(h + 16);
    pack_blob = rd32(h + 20);
    pack_total = total;
    source.count = (int)pack_count;
    gd_preset_set_external(&source);
    return pack_count;
}

static void presets_changed(void)
{
    const int n = gd_preset_count();
    if (gd_state.preset >= n) gd_state.preset = 0;
    if (gd_state.presetSel >= n) gd_state.presetSel = gd_state.preset;
    ++gd_state.uiStamp;
}

void gd_pack_init(void)
{
    in.length = 0;
    receiving = 0;
    pack_open();
    presets_changed();
}

/* ---------------------------------------------------------------- SysEx */
void gd_pack_sysex(const uint8_t *m, size_t n)
{
    if (!m || n < 6 || n > sizeof in.bytes || in.length) return;
    if (m[0] != 0xF0 || m[1] != 0x7D || m[2] != 0x47 || m[3] != 0x44 || m[n - 1] != 0xF7) return;
    in.source = fm1_midi_input_source();
    memcpy(in.bytes, m, n);
    barrier();
    in.length = (unsigned)n;
    fm1_control_request();
}

static void reply(unsigned port, uint8_t cmd, uint8_t st, uint32_t v)
{
    uint8_t r[13] = { 0xF0, 0x7D, 0x47, 0x44, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0xF7 };
    int i;
    r[5] = cmd & 0x7Fu;
    r[6] = st;
    for (i = 0; i < 5; ++i) { r[7 + i] = (uint8_t)(v & 0x7Fu); v >>= 7; }
    (void)fm1_midi_send_port(port, r, sizeof r);
}

static int write_chunk(uint32_t off, const uint8_t *data, uint32_t len)
{
    const uint32_t end = GD_PACK_BASE + off + len;
    while (erased_to < end) {                             /* Sektoren erst beim Hineinschreiben loeschen */
        if (fm1_flash_erase(erased_to) != FM1_FLASH_OK) return -1;
        erased_to += FM1_FLASH_SECTOR_BYTES;
    }
    return fm1_flash_write(GD_PACK_BASE + off, data, len) == FM1_FLASH_OK ? 0 : -1;
}

static void handle(const uint8_t *m, unsigned n, unsigned port)
{
    const uint8_t cmd = m[4];
    if (cmd == 0x04) { reply(port, cmd, 0, pack_count); return; }
    if (cmd == 0x01 && n >= 11) {
        const uint32_t total = u35(m + 5);
        if (total < 32u || fm1_flash_size() < GD_PACK_BASE || total > fm1_flash_size() - GD_PACK_BASE) { reply(port, cmd, 1, 0); return; }
        gd_preset_set_external(0);                        /* altes Paket wird ueberschrieben */
        pack_count = 0;
        presets_changed();
        rx_total = total; rx_next = 0; erased_to = GD_PACK_BASE; receiving = 1; last_pct = -1;
        gd_app_message(&gd_state, "Presets empfangen...");
        reply(port, cmd, 0, 0);
        return;
    }
    if (cmd == 0x02 && n >= 11) {
        uint8_t raw[256];
        const uint32_t off = u35(m + 5);
        const unsigned enc = n - 11;                      /* zwischen Offset und F7 */
        unsigned i, k, len = 0;
        if (!receiving) { reply(port, cmd, 1, 0); return; }
        if (off != rx_next) { reply(port, cmd, 2, rx_next); return; }
        for (i = 0; i < enc; i += 8) {
            const uint8_t hi = m[10 + i];
            for (k = 1; k < 8 && i + k < enc && len < sizeof raw; ++k)
                raw[len++] = (uint8_t)(m[10 + i + k] | (((hi >> (k - 1)) & 1u) << 7));
        }
        if (!len || off + len > rx_total || write_chunk(off, raw, len)) { receiving = 0; reply(port, cmd, 1, off); return; }
        rx_next = off + len;
        {
            const int pct = (int)(rx_next / ((rx_total + 99u) / 100u));
            if (pct / 10 != last_pct / 10) {
                char t[32];
                gd_snprintf(t, sizeof t, "Presets %d %%", pct);
                gd_app_message(&gd_state, t);
                last_pct = pct;
            }
        }
        reply(port, cmd, 0, rx_next);
        return;
    }
    if (cmd == 0x03) {
        uint32_t count = 0;
        if (receiving && rx_next == rx_total) count = pack_open();
        receiving = 0;
        presets_changed();
        gd_app_message(&gd_state, count ? "Presets aufgespielt" : "Preset-Paket defekt");
        reply(port, cmd, count ? 0 : 1, count);
        return;
    }
}

void gd_pack_poll(void)
{
    const unsigned n = in.length;
    if (!n) return;
    barrier();
    handle(in.bytes, n, in.source);
    barrier();
    in.length = 0;
}
