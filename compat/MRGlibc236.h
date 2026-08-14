#ifndef MRGLIBC236_H
#define MRGLIBC236_H

/*
 * CachyOS glibc enables C23 conversion entry points through _GNU_SOURCE.
 * Keep the GNU feature set, but compile release binaries against the legacy
 * conversion ABI that is present in glibc 2.36.
 */
#include <features.h>

#if defined(__GLIBC__) && defined(__GLIBC_USE_ISOC23)
#undef __GLIBC_USE_ISOC23
#define __GLIBC_USE_ISOC23 0
#endif

#if defined(__GLIBC__) && defined(__GLIBC_USE_C23_STRTOL)
#undef __GLIBC_USE_C23_STRTOL
#define __GLIBC_USE_C23_STRTOL 0
#endif

#endif
