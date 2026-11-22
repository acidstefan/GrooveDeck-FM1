/* Minimale libc fuer die WebAssembly-Fassung (ohne Systembibliothek) */
#ifndef GD_WEB_STRING_H
#define GD_WEB_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strncpy(char *d, const char *s, size_t n);
#endif
