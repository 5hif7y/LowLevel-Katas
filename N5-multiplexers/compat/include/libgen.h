#ifndef COMPAT_LIBGEN_H
#define COMPAT_LIBGEN_H
#include "compat.h"
#define basename(p) compat_basename(p)
#define dirname(p)  compat_dirname(p)
#endif
