/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * GrooveDeck FM-1 auf FM1 Open - MIDI. Ersetzt app/midi_router.c von FM1 Open.
 * Eingang (TRS/DIN, USB, Bluetooth): fm1_app_midi_received (Routing-Task) legt die Bytes in einen Ring, der
 * Control-Task gibt sie an den GrooveDeck-Parser (gd_midi_byte). Ausgang: Clock/Start/Stop und live gespielte
 * Noten aus dem GrooveDeck-Kern an alle verfuegbaren Ausgaenge.
 * Die groove_midi_*-Funktionen erwartet die FM1-Bibliothek; GrooveDeck braucht sie nicht. */
#include "instrument.h"
#include "gd_fm1open.h"
#include <string.h>

/* ---------------------------------------------------------------- Eingang: Routing-Task -> Control-Task */
#define RX_SIZE 512u
static volatile uint8_t rx[RX_SIZE];
static volatile unsigned rx_w, rx_r;
static gd_midi parser;

static void barrier(void) { __asm__ volatile("csync" ::: "memory"); }

void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)
{
    unsigned w = rx_w, i;
    (void)source;
    if (!m || !n || n > 3) return;
    if (RX_SIZE - (w - rx_r) < n) return;                 /* voll: Nachricht verwerfen */
    for (i = 0; i < n; ++i) rx[(w + i) % RX_SIZE] = m[i];
    barrier();
    rx_w = w + (unsigned)n;
}

/* ---------------------------------------------------------------- Ausgang */
static uint8_t tx_msg[3];
static unsigned tx_n, tx_need;

static unsigned msg_len(uint8_t status)
{
    if (status >= 0xf8u) return 1;
    if ((status & 0xe0u) == 0xc0u) return 2;
    return 3;
}

static void send(const uint8_t *m, unsigned n)
{
    const unsigned ports = fm1_midi_ready_ports() & fm1_midi_output_ports();
    unsigned p;
    for (p = 0; p < 3; ++p) if (ports & (1u << p)) (void)fm1_midi_send_port(p, m, n);
}

void gd_midi_poll(void)
{
    uint8_t out[64];
    int n, i;
    unsigned r = rx_r;
    while (r != rx_w) {
        barrier();
        gd_midi_byte(&parser, &gd_state, rx[r % RX_SIZE]);
        ++r;
    }
    rx_r = r;
    while ((n = gd_app_midi_out_read(&gd_state, out, (int)sizeof out)) > 0) {
        for (i = 0; i < n; ++i) {
            const uint8_t b = out[i];
            if (b >= 0xf8u) { send(&b, 1); continue; }        /* Echtzeit sofort */
            if (b & 0x80u) { tx_msg[0] = b; tx_n = 1; tx_need = msg_len(b); }
            else if (tx_n && tx_n < 3) tx_msg[tx_n++] = b;
            if (tx_n && tx_n == tx_need) { send(tx_msg, tx_n); tx_n = 0; }
        }
    }
}

/* ---------------------------------------------------------------- Von der Bibliothek erwartet */
void groove_midi_init(void) { rx_w = rx_r = 0; tx_n = 0; memset(&parser, 0, sizeof parser); }
void groove_midi_clock_reset(void) {}
void groove_midi_poll(void) {}
void groove_midi_panic(void) {}
FM1_AUDIO_CODE unsigned groove_midi_mode(unsigned track) { (void)track; return 0; }
FM1_AUDIO_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity) { (void)track; (void)note; (void)velocity; }
FM1_AUDIO_CODE void groove_midi_track_reset(unsigned track) { (void)track; }
FM1_AUDIO_CODE void groove_midi_transport(unsigned owner, uint8_t status) { (void)owner; (void)status; }
FM1_AUDIO_CODE void groove_midi_clock(unsigned owner, unsigned tempo) { (void)owner; (void)tempo; }

/* SysEx der App (Firmware-Updates behandelt die Bibliothek vorher selbst): Preset-Paket aufspielen */
void fm1_app_sysex(const uint8_t *message, size_t bytes) { gd_pack_sysex(message, bytes); }
