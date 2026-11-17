/* GrooveDeck FM-1 - Bruecke zwischen der Start-Uebergabe des FM-1-Bootloaders und dem SDK-Startcode.
 *
 * Laeuft vor der RAM-Initialisierung: nur Stack und XIP-Flash, keine globalen Variablen, keine Bibliotheken.
 * Der Bootloader des Geraets uebergibt sechs Worte; das SDK erwartet 23 (Rest 0). Wort 0 zeigt auf den
 * entschluesselten 32-Byte-Flash-Kopf, Wort 1/2 auf den Programmbereich (NOR-Offset / XIP-Adresse), Wort 3
 * enthaelt den SFC-Schluessel.
 *
 * Wird die Firmware als rohe .bin im fm1-emulator gestartet, ist diese Uebergabe leer (alles 0) und der
 * SDK-Startcode liest ueber einen Nullzeiger. Dann setzen wir Ersatzwerte ein: SDK-Standard-Flash-Kopf
 * (Flash-Groesse 0xFF000, FS-Version 0x10) und den Programmbereich bei NOR 0x4000 = XIP 0x02000000.
 * Auf dem Geraet (Uebergabe vorhanden) bleibt alles wie vom Bootloader geliefert.
 */
#include <stdint.h>
#include "app_config.h"
#if __SDRAM_SIZE__ != 0
#error "nur fuer Builds ohne externes SDRAM"
#endif

extern void __real_boot_info_init(const volatile uint32_t *argument);

/* Flash-Kopf mit SDK-Standardwerten (Felder: +2 Brenner-Groesse 544, +8 Flash-Groesse, +12/+13 FS-Version) */
__attribute__((aligned(4))) static const uint8_t gd_default_flash_header[32] = {
    0x00, 0x00, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF0, 0x0F, 0x00, 0x10, 0x10, 0x00, 0x00,
    'A', 'C', '7', '9', '1', 'N', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

__attribute__((noinline, used))
void __wrap_boot_info_init(const volatile uint32_t *stock_argument)
{
    volatile uint32_t sdk_argument[23];
    unsigned i;
    for (i = 0; i < 6; ++i) sdk_argument[i] = stock_argument ? stock_argument[i] : 0u;
    for (i = 6; i < 23; ++i) sdk_argument[i] = 0u;
    if (sdk_argument[0] == 0u) {            /* keine Uebergabe: Start im Emulator als rohe .bin */
        sdk_argument[0] = (uint32_t)(uintptr_t)gd_default_flash_header;
        sdk_argument[1] = 0x00004000u;      /* NOR-Offset des Programmbereichs */
        sdk_argument[2] = 0x02000000u;      /* XIP-Adresse des Programmbereichs */
        sdk_argument[3] = 0u;               /* SFC-Schluessel: unverschluesselt */
    }
    __real_boot_info_init(sdk_argument);
}

/* Ersatz fuer die Init-Funktion des SDK-Ressourcen-Dateisystems (sdfile_init), nur in der Emulator-Fassung:
   build.py traegt diese Funktion dort in die Init-Tabelle ein. Bei einer rohen .bin gibt es im Flash kein
   JLFS-Dateisystem und sdfile_init loest einen Neustart aus; GrooveDeck braucht keine Flash-Ressourcen. */
__attribute__((noinline, used))
int gd_initcall_noop(void)
{
    return 0;
}

/* Platz fuer Sprungumwege, nur in der Emulator-Fassung genutzt (build.py, fcmp_trampolines): Gleitkomma-
   Vergleiche mit weitem Sprung (FF4x .. 0x80) kennt der fm1-emulator nicht, die kurze Form schon. build.py
   ersetzt jede solche Stelle durch einen Sprung hierher. Liegt bewusst im Programmbereich (.text), denn der
   SDK-Startcode begrenzt den erlaubten PC-Bereich darauf. Auf dem Geraet wird diese Funktion nie aufgerufen. */
__attribute__((noinline, used, aligned(4)))
void gd_emu_tramp(void)
{
    __asm__ volatile(".balign 4\n\t.fill 768, 4, 0x52544447");
}
