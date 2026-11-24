// vsnprintf for freestanding code: %d %i %u %x %X %o %c %s %p %f %%, with
// flags '-', '0', '+', ' ', width, precision and the l, ll, z, h, hh lengths.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "con.h"
#include "scif.h"

typedef struct { char *p; size_t n, len; } Out;

static void put(Out *o, char c) { if (o->len + 1 < o->n) o->p[o->len] = c; o->len++; }

static void pad(Out *o, int n, char c) { while (n-- > 0) put(o, c); }

static int sign_flag;     /* '+' or ' ' for the conversion being formatted, else 0 */

static void num(Out *o, unsigned long long v, int neg, unsigned base, int upper, int width, int prec, int zero, int left)
{
    char b[24];
    int i = 0;
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { b[i++] = d[v % base]; v /= base; } while (v);
    while (i < prec) b[i++] = '0';
    const char sign = neg ? '-' : (char)sign_flag;
    int len = i + (sign ? 1 : 0);
    if (!left && !zero) pad(o, width - len, ' ');
    if (sign) put(o, sign);
    if (!left && zero) pad(o, width - len, '0');
    while (i) put(o, b[--i]);
    if (left) pad(o, width - len, ' ');
}

int vsnprintf(char *buf, size_t n, const char *f, va_list ap)
{
    Out o = {buf, n, 0};
    for (; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        sign_flag = 0;
        for (f++; *f == '-' || *f == '0' || *f == '+' || *f == ' '; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') sign_flag = '+';
            else if (!sign_flag) sign_flag = ' ';
        }
        if (*f == '*') { width = va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { prec = 0; f++; if (*f == '*') { prec = va_arg(ap, int); f++; } else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        for (;; f++) {
            if (*f == 'l') lng++; else if (*f == 'z') lng = 1; else if (*f == 'h') {} else break;
        }
        switch (*f) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            num(&o, v < 0 ? (unsigned long long)(-v) : (unsigned long long)v, v < 0, 10, 0, width, prec, zero, left);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            sign_flag = 0;
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            num(&o, v, 0, *f == 'u' ? 10 : *f == 'o' ? 8 : 16, *f == 'X', width, prec, zero, left);
            break;
        }
        case 'p': put(&o, '0'); put(&o, 'x'); num(&o, (uintptr_t)va_arg(ap, void *), 0, 16, 0, 8, 0, 1, 0); break;
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            if (!left) pad(&o, width - len, ' ');
            for (int i = 0; i < len; i++) put(&o, s[i]);
            if (left) pad(&o, width - len, ' ');
            break;
        }
        case 'f': case 'F': case 'g': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = 6;
            int neg = v < 0;
            if (neg) v = -v;
            unsigned long long scale = 1;
            for (int i = 0; i < prec; i++) scale *= 10;
            unsigned long long whole = (unsigned long long)v;
            unsigned long long frac = (unsigned long long)((v - (double)whole) * (double)scale + 0.5);
            if (frac >= scale) { whole++; frac -= scale; }
            int wlen = 1;
            for (unsigned long long t = whole; t >= 10; t /= 10) wlen++;
            int total = (neg || sign_flag) + wlen + (prec ? prec + 1 : 0);
            if (!left) pad(&o, width - total, zero ? '0' : ' ');
            num(&o, whole, neg, 10, 0, 0, 0, 0, 0);
            sign_flag = 0;
            if (prec) { put(&o, '.'); num(&o, frac, 0, 10, 0, 0, prec, 0, 0); }
            if (left) pad(&o, width - total, ' ');
            break;
        }
        case '%': put(&o, '%'); break;
        default: put(&o, '%'); if (*f) put(&o, *f); else f--; break;
        }
    }
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(buf, n, f, ap);
    va_end(ap);
    return r;
}

int con_printf(const char *f, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    scif_puts(0, b);
    return r;
}
