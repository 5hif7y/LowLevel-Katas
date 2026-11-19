/* langinfo.h - the port always runs the CRT in a UTF-8 locale (see
 * compat/src/init.c), so CODESET is a constant. */
#ifndef COMPAT_LANGINFO_H
#define COMPAT_LANGINFO_H
#define CODESET 14
#define nl_langinfo(item) ((item) == CODESET ? "UTF-8" : "")
#endif
