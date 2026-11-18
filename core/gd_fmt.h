/* GrooveDeck FM-1 - kleines snprintf (die SDK-libc des FM1-Open-Pakets hat keine printf-Familie).
   Unterstuetzt: %s %c %d mit Flags '+', '0' und Breite (z. B. %03d, %+d), %.1f, %%. */
#ifndef GD_FMT_H
#define GD_FMT_H
#include <stddef.h>
int gd_snprintf(char *dst, size_t cap, const char *fmt, ...);
#endif
