/* GrooveDeck FM-1 - kleines snprintf, siehe gd_fmt.h */
#include "gd_fmt.h"
#include <stdarg.h>

typedef struct { char *d; size_t cap, n; } out_t;
static void put(out_t *o, char c) { if (o->n + 1 < o->cap) o->d[o->n] = c; ++o->n; }

static void put_uint(out_t *o, unsigned long v, int width, char pad, int sign)
{
    char b[24];
    int k = 0, len;
    do { b[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    len = k + (sign ? 1 : 0);
    if (pad == '0' && sign) put(o, (char)sign);
    for (; len < width; ++len) put(o, pad);
    if (pad != '0' && sign) put(o, (char)sign);
    while (k) put(o, b[--k]);
}

int gd_snprintf(char *dst, size_t cap, const char *f, ...)
{
    out_t o;
    va_list ap;
    o.d = dst; o.cap = cap; o.n = 0;
    va_start(ap, f);
    for (; *f; ++f) {
        int plus = 0, width = 0, prec = -1;
        char pad = ' ';
        if (*f != '%') { put(&o, *f); continue; }
        ++f;
        for (;; ++f) { if (*f == '+') plus = 1; else if (*f == '0') pad = '0'; else break; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { prec = 0; ++f; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        switch (*f) {
            case 's': { const char *s = va_arg(ap, const char *); int l = 0; const char *t;
                        if (!s) s = "";
                        for (t = s; *t; ++t) ++l;
                        for (; l < width; ++l) put(&o, ' ');
                        while (*s) put(&o, *s++);
                        break; }
            case 'c': put(&o, (char)va_arg(ap, int)); break;
            case 'd': { const int v = va_arg(ap, int);
                        const unsigned long u = v < 0 ? (unsigned long)(-(long)v) : (unsigned long)v;
                        put_uint(&o, u, width, pad, v < 0 ? '-' : plus ? '+' : 0);
                        break; }
            case 'f': { double v = va_arg(ap, double);
                        int sign = v < 0.0 ? '-' : plus ? '+' : 0;
                        unsigned long scale = 1, ip, fp;
                        int i;
                        if (prec < 0) prec = 6;
                        if (prec > 6) prec = 6;
                        for (i = 0; i < prec; ++i) scale *= 10u;
                        if (v < 0.0) v = -v;
                        v = v * (double)scale + 0.5;
                        ip = (unsigned long)v / scale; fp = (unsigned long)v % scale;
                        put_uint(&o, ip, width > prec + 1 ? width - prec - 1 : 0, pad, sign);
                        if (prec) { put(&o, '.'); put_uint(&o, fp, prec, '0', 0); }
                        break; }
            case '%': put(&o, '%'); break;
            case 0: --f; break;
            default: put(&o, '%'); put(&o, *f); break;
        }
    }
    va_end(ap);
    if (cap) dst[o.n < cap ? o.n : cap - 1] = 0;
    return (int)o.n;
}
