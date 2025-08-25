/* Minimal config.h provided for in-tree LAME build inside miniBAE.
 * Avoid redefining integer types; rely on system <stdint.h> when available.
 * Provide the small set of defines and typedefs LAME expects when built
 * from a repository without running configure.
 */

#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H

/* common positive feature macros expected by some LAME sources */
#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1

/* If stdint.h is available the build already sets HAVE_STDINT_H; include it */
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

/* Provide IEEE float typedefs if not supplied by configure */
#ifndef HAVE_IEEE754_FLOAT32_T
typedef float ieee754_float32_t;
#endif
#ifndef HAVE_IEEE754_FLOAT64_T
typedef double ieee754_float64_t;
#endif
#ifndef HAVE_IEEE854_FLOAT80_T
typedef long double ieee854_float80_t;
#endif

#endif /* LAME_CONFIG_H */

#ifndef FLAC_CONFIG_H
#define FLAC_CONFIG_H

#ifdef USE_OGG_FORMAT
#define FLAC__HAS_OGG 1
#else
#define FLAC__HAS_OGG 0
#endif
#define FLAC__NO_DLL 1
#define HAVE_LROUND 1
#define HAVE_FSEEKO 0
#define PACKAGE_VERSION "1.5.0"
#define VERSION "1.5.0"
#endif
