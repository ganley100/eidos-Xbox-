/*
 * host/xbox/xbox_compat.h -- included before every host file of the Xbox
 * build (/FI): what MSVC's C library for UWP apps lacks or names otherwise.
 * The host is written for GCC and Clang (MinGW on Windows); the Xbox's
 * build is MSVC, since SDL2's UWP half is C++/CX (docs/xbox.md).
 */
#ifndef HOST_XBOX_COMPAT_H
#define HOST_XBOX_COMPAT_H

#include <io.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* No signals on Windows: wasm2c's runtime saves no signal mask, so the
 * plain jumps are the same thing. */
#define sigsetjmp(buf, save) setjmp(buf)
#define siglongjmp(buf, val) longjmp(buf, val)

/* GCC's thread-local spelling (host/switch/sdl3_on_sdl2.c) */
#define __thread __declspec(thread)

#ifndef strcasecmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

/* POSIX's setenv, on the C library's environment (which the module reads
 * through WASI's environ, host/wasi.c, from the process's own) */
static __inline int xbox_setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
#define setenv xbox_setenv

#endif
