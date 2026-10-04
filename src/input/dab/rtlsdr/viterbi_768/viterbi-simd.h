/*
 * Selects the SIMD implementation of the spiral viterbi decoder from the
 * target of the compiler: SSE2 (x86), NEON (32 bit ARM with -mfpu=neon,
 * aarch64) or generic C.
 */
#ifndef VITERBI_SIMD_H
#define VITERBI_SIMD_H

#if !defined(SSE_AVAILABLE) && !defined(CONFIG_NEON) && defined(__SSE2__)
#define SSE_AVAILABLE 1
#endif

#if !defined(SSE_AVAILABLE) && !defined(CONFIG_NEON) && \
    (defined(__ARM_NEON) || defined(__ARM_NEON__))
#define CONFIG_NEON 1
#endif

#endif
