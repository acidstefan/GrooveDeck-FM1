/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * GrooveDeck FM-1 auf FM1 Open - Speichern und Platzhalter fuer die FM1-Bibliothek.
 * Ersetzt app/project.c, presets.c, samples.c, transfer.c, fm_library.c und fm_factory.c von FM1 Open.
 *
 * Benutzerstand (SAVE halten): in den ersten zwei Sektoren der Benutzer-Partition (fm1_flash_*), Kopf mit
 * Kennung, Laenge und Pruefsumme. Achtung: FM1 Open legt seine Projekte in dieselbe Partition - vor dem
 * Wechsel auf GrooveDeck Projekte in der Workbench sichern. */
#include "instrument.h"
#include "gd_fm1open.h"
#include <string.h>

#define GD_SAVE_MAGIC 0x31534447u   /* "GDS1" */
#define GD_SAVE_BYTES (2u * FM1_FLASH_SECTOR_BYTES)

static uint8_t save_buf[GD_SAVE_BYTES] __attribute__((aligned(4)));
static uint8_t check[256];
static volatile int save_pending;   /* 1: Audio-Interrupt hat Daten abgelegt, Control-Task schreibt */

static uint32_t sum32(const uint8_t *d, unsigned n)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < n; ++i) h = (h ^ d[i]) * 16777619u;
    return h;
}

void gd_flash_load(void)
{
    uint32_t hdr[4];
    if (fm1_flash_size() < GD_SAVE_BYTES) return;
    if (fm1_flash_read(0, hdr, sizeof hdr) != FM1_FLASH_OK) return;
    if (hdr[0] != GD_SAVE_MAGIC || hdr[1] == 0 || hdr[1] > GD_SAVE_BYTES - 16u) return;
    if (fm1_flash_read(16, save_buf, hdr[1]) != FM1_FLASH_OK) return;
    if (sum32(save_buf, hdr[1]) != hdr[2]) return;
    gd_app_load_user(&gd_state, save_buf, (int)hdr[1]);
}

/* Aus dem Audio-Interrupt (do_save): nur kopieren und vormerken - Flash darf nur der Control-Task */
int gd_flash_save_hook(const uint8_t *data, int len)
{
    uint32_t *hdr = (uint32_t *)save_buf;
    if (save_pending || len <= 0 || (unsigned)len > GD_SAVE_BYTES - 16u) return -1;
    hdr[0] = GD_SAVE_MAGIC; hdr[1] = (uint32_t)len; hdr[2] = sum32(data, (unsigned)len); hdr[3] = 0;
    memcpy(save_buf + 16, data, (size_t)len);
    save_pending = 1;
    return 0;
}

void gd_flash_poll(void)
{
    uint32_t total, i;
    if (!save_pending) return;
    total = (16u + ((uint32_t *)save_buf)[1] + 3u) & ~3u;
    if (fm1_flash_size() < GD_SAVE_BYTES ||
        fm1_flash_erase(0) != FM1_FLASH_OK || fm1_flash_erase(FM1_FLASH_SECTOR_BYTES) != FM1_FLASH_OK ||
        fm1_flash_write(0, save_buf, total) != FM1_FLASH_OK) {
        gd_app_message(&gd_state, "Flash-Fehler");
        save_pending = 0;
        return;
    }
    for (i = 0; i < total; i += sizeof check) {
        const uint32_t n = total - i < sizeof check ? total - i : sizeof check;
        if (fm1_flash_read(i, check, n) != FM1_FLASH_OK || memcmp(check, save_buf + i, n)) {
            gd_app_message(&gd_state, "Flash-Fehler");
            break;
        }
    }
    save_pending = 0;
}

/* ---------------------------------------------------------------- Von der Bibliothek erwartet */
static struct groove_project scratch;
struct groove_project *groove_project_scratch(void) { return &scratch; }
void groove_project_init(void) { memset(&scratch, 0, sizeof scratch); }
void groove_project_poll(void) {}
void groove_project_metadata(const struct groove_project *p) { (void)p; }
void groove_project_name(char name[13]) { memcpy(name, "GROOVEDECK  ", 13); }
void groove_samples_init(void) {}
void groove_samples_poll(void) {}
void groove_transfer_init(void) {}
void groove_transfer_poll(void) {}
void groove_fm_library_init(void) {}
FM1_DSP_CODE void groove_fm_program(uint8_t p[156], unsigned program) { (void)program; for (unsigned i = 0; i < 156; ++i) p[i] = 0; }
