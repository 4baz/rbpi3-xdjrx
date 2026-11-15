#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Host toolchains with newer glibc headers may rewrite strtol/strtoul/sscanf calls to
 * __isoc23_* entry points. The RX/RX3 runtime glibc (2.13) does not export
 * those symbols, so provide thin wrappers to the legacy symbols that do exist.
 */
extern long legacy_strtol(const char *nptr, char **endptr, int base) __asm__("strtol");
extern unsigned long legacy_strtoul(const char *nptr, char **endptr, int base) __asm__("strtoul");
extern int legacy_vsscanf(const char *s, const char *format, va_list arg) __asm__("__isoc99_vsscanf");

long __isoc23_strtol(const char *nptr, char **endptr, int base)
{
    return legacy_strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base)
{
    return legacy_strtoul(nptr, endptr, base);
}

int __isoc23_sscanf(const char *s, const char *format, ...)
{
    va_list ap;
    int rc;
    va_start(ap, format);
    rc = legacy_vsscanf(s, format, ap);
    va_end(ap);
    return rc;
}
