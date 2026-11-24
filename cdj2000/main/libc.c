// The little of libc a freestanding build still needs (GCC may emit calls).
#include <stddef.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t len = strlen(s);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int strcasecmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (lower(*a) != lower(*b)) return lower((unsigned char)*a) - lower((unsigned char)*b);
        if (!*a) return 0;
    }
    return 0;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}

char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}

double floor(double x)
{
    if (x >= 9007199254740992.0 || x <= -9007199254740992.0 || x != x) return x;
    long long i = (long long)x;
    return (double)(x < (double)i ? i - 1 : i);
}

double ceil(double x) { return -floor(-x); }
double fabs(double x) { return x < 0 ? -x : x; }

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
    return 0;
}
