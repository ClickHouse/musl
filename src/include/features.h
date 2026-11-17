#ifndef FEATURES_H
#define FEATURES_H

#include "../../include/features.h"

#define weak __attribute__((__weak__))
#define hidden __attribute__((__visibility__("hidden")))
/* Exempts a function's indirect calls from Clang's control flow integrity
 * checks, for calls whose target type cannot match the call. */
#ifdef __clang__
#define no_cfi_icall __attribute__((__no_sanitize__("cfi-icall")))
#else
#define no_cfi_icall
#endif
/* Exempts the indirect calls that can reach a sanitizer runtime built
 * without CFI, in builds that define CFI_UNCHECKED_RUNTIME (the runtime that
 * reports CFI violations instead of trapping). */
#ifdef CFI_UNCHECKED_RUNTIME
#define no_cfi_icall_runtime no_cfi_icall
#else
#define no_cfi_icall_runtime
#endif
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

#endif
