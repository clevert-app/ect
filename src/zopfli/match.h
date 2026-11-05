//
//  match.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 31.12.15.
//  Copyright © 2015 Felix Hanau.
//

/*
 Finds how long the match of scan and match is. Can be used to find how many
 bytes starting from scan, and from match, are equal. Returns the last byte
 after scan, which is still equal to the correspondinb byte after match.
 scan is the position to compare
 match is the earlier position to compare.
 end is the last possible byte, beyond which to stop looking.
 safe_end is a few (8) bytes before end, for comparing multiple bytes at once.
 */
#ifndef ZOPFLI_MATCH_H_
#define ZOPFLI_MATCH_H_

#include "util.h"
#include <memory.h>
#include <stdint.h>
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

// length (end - scan):
// * `< 8` bytes         75.81%
// * `8-15` bytes        9.89%
// * `16-31` bytes       3.88%
// * `32-63` bytes       2.79%
// * `> 64` bytes         7.63%

ZOPFLI_FORCE_INLINE
static inline const unsigned char *GetMatch_opti_v3(const unsigned char *__restrict scan,
                                                    const unsigned char *__restrict match,
                                                    const unsigned char *__restrict end,
                                                    const unsigned char *__restrict safe_end) {
  // todo
  // return NULL;// 99.9%  scan + 8 <= end
  if (likely(scan + 8 <= end)) {
    // 75.81%
    // Loop Peeling
    uint64_t xor_v = u64read(scan) ^ u64read(match);
    if (likely(xor_v != 0)) {
      return scan + (__builtin_ctzll(xor_v) >> 3);
    }
    // 24.19%
    scan += 8;
    match += 8;
    // unroll overread
    if (scan + 16 <= end) {
      const unsigned char *const end16 = end - 16;
      do {
        xor_v = u64read(scan) ^ u64read(match);
        if (xor_v)
          return scan + (__builtin_ctzll(xor_v) >> 3);
        xor_v = u64read(scan + 8) ^ u64read(match + 8);
        if (xor_v)
          return scan + 8 + (__builtin_ctzll(xor_v) >> 3);
        scan += 16;
        match += 16;
      } while (scan <= end16);
    }
    if (scan + 8 <= end) {
      xor_v = u64read(scan) ^ u64read(match);
      if (xor_v)
        return scan + (__builtin_ctzll(xor_v) >> 3);
      scan += 8;
      match += 8;
    }
    // tail
    if (scan < end) {
      const unsigned char *const end8 = end - 8;
      xor_v = u64read(end8) ^ u64read(match + (end8 - scan));
      if (xor_v)
        return end8 + (__builtin_ctzll(xor_v) >> 3);
    }
    return end;
  }
  // cold
  size_t len = end - scan;
  if (len >= 4) {
    uint32_t xor_v = u32read(scan) ^ u32read(match);
    if (xor_v)
      return scan + (__builtin_ctz(xor_v) >> 3);
    xor_v = u32read(end - 4) ^ u32read(match + len - 4);
    if (xor_v)
      return (end - 4) + (__builtin_ctz(xor_v) >> 3);
    return end;
  }
  if (len >= 2) {
    uint16_t sv, mv;
    memcpy(&sv, scan, 2);
    memcpy(&mv, match, 2);
    if (sv != mv)
      return scan + (__builtin_ctz(sv ^ mv) >> 3);
    return (len == 2 || scan[2] == match[2]) ? end : scan + 2;
  }
  if (len == 1) {
    return scan + (*scan == *match);
  }
  return scan;
}

ZOPFLI_FORCE_INLINE
static inline const unsigned char *GetMatch_opti_v1(const unsigned char *__restrict scan,
                                                    const unsigned char *__restrict match,
                                                    const unsigned char *__restrict end,
                                                    const unsigned char *safe_end) {
  // seems even slower
  // for (; scan + 32 < end; scan += 32, match += 32) {
  //   __m256i v_sv = _mm256_loadu_si256((const __m256i *)scan);
  //   __m256i v_mv = _mm256_loadu_si256((const __m256i *)match);
  //   __m256i v_cmp = _mm256_cmpeq_epi8(v_sv, v_mv);
  //   uint32_t mask = (uint32_t)_mm256_movemask_epi8(v_cmp);
  //   if (~mask) {
  //     scan += __builtin_ctz(~mask);
  //     return scan;
  //   }
  // }
  // #pragma clang loop unroll_count(2)
  // #pragma clang loop unroll(disable)

  // question: will scan+8 cause ub or not?
  // below are: add/sub/endsub varients
  // for (; scan + 8 <= end; scan += 8, match += 8) {
  // for (; end - scan >= 8; scan += 8, match += 8) {
  for (; scan <= end - 8; scan += 8, match += 8) {
    uint64_t xor_v = u64read(scan) ^ u64read(match);
    if (xor_v) {
      scan += __builtin_ctzll(xor_v) >> 3;
      return scan;
    }
  }
  // for (; scan + 4 <= end; scan += 4, match += 4) {
  // for (; end - scan >= 4; scan += 4, match += 4) {
  for (; scan <= end - 4; scan += 4, match += 4) {
    uint32_t xor_v = u32read(scan) ^ u32read(match);
    if (xor_v) {
      scan += __builtin_ctz(xor_v) >> 3;
      return scan;
    }
  }
  while (scan < end && *scan == *match) {
    scan++, match++;
  }
  return scan;
}

ZOPFLI_FORCE_INLINE
static inline const unsigned char *GetMatch_legacy(const unsigned char *__restrict scan,
                                                   const unsigned char *__restrict match,
                                                   const unsigned char *__restrict end,
                                                   const unsigned char *safe_end) {
  /* Optimized Function based on cloudflare's zlib fork.
   * Note that this may read up to 15 bytes beyond end,
   * so it is necessary to allocate memory beyond the
   * end of the data whenever ZopfliDeflate is called.
   */
  do {
    // if (scan + 8 >= end) break;
    uint64_t sv = u64read(scan);
    uint64_t mv = u64read(match);
    uint64_t xor_v = sv ^ mv;
    if (xor_v) {
      scan += (__builtin_ctzll(xor_v) >> 3);
      break;
    }
    scan += 8;
    match += 8;
    // if (scan + 8 >= end) break;
    sv = u64read(scan);
    mv = u64read(match);
    xor_v = sv ^ mv;
    if (xor_v) {
      scan += (__builtin_ctzll(xor_v) >> 3);
      break;
    }
    scan += 8;
    match += 8;
  } while (scan < end);

  if (unlikely(scan > end))
    scan = end;
  return scan;
}

#define GetMatch GetMatch_opti_v1

#endif
