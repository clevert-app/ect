// for (; curr <= len; curr++) {
//   float x = price2 + litlentable[curr];
//   if (x < costs[j + curr]){
//     costs[j + curr] = x;
//     length_array[j + curr] = curr + dist;
//   }
// }

#if defined(__AVX2__)
if (curr + 7 <= len) {
  const __m256 prices = _mm256_set1_ps(price2);
  for (; curr + 7 <= len; curr += 8) {
    __m256 candidate = _mm256_add_ps(prices, _mm256_loadu_ps(&litlentable[curr]));
    __m256 previous = _mm256_loadu_ps(&costs[j + curr]);
    __m256 better = _mm256_cmp_ps(candidate, previous, _CMP_LT_OQ);
    if (_mm256_movemask_ps(better)) {
      __m256i lengths = _mm256_add_epi32(_mm256_set1_epi32(curr + dist), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
      __m256i previous_lengths = _mm256_loadu_si256((const __m256i*)&length_array[j + curr]);
      _mm256_storeu_ps(&costs[j + curr], _mm256_blendv_ps(previous, candidate, better));
      _mm256_storeu_si256((__m256i*)&length_array[j + curr],
          _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(previous_lengths), _mm256_castsi256_ps(lengths), better)));
    }
  }
  // remain items can still use simd
  if (curr <= len) {
    curr = len - 7;
    __m256 candidate = _mm256_add_ps(prices, _mm256_loadu_ps(&litlentable[curr]));
    __m256 previous = _mm256_loadu_ps(&costs[j + curr]);
    __m256 better = _mm256_cmp_ps(candidate, previous, _CMP_LT_OQ);
    if (_mm256_movemask_ps(better)) {
      __m256i lengths = _mm256_add_epi32(_mm256_set1_epi32(curr + dist), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
      __m256i previous_lengths = _mm256_loadu_si256((const __m256i*)&length_array[j + curr]);
      _mm256_storeu_ps(&costs[j + curr], _mm256_blendv_ps(previous, candidate, better));
      _mm256_storeu_si256((__m256i*)&length_array[j + curr],
          _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(previous_lengths), _mm256_castsi256_ps(lengths), better)));
    }
    curr = len + 1;
  }
}
if (curr + 3 <= len) {
  const __m128 prices = _mm_set1_ps(price2);
  for (; curr + 3 <= len; curr += 4) {
    __m128 candidate = _mm_add_ps(prices, _mm_loadu_ps(&litlentable[curr]));
    __m128 previous = _mm_loadu_ps(&costs[j + curr]);
    __m128 better = _mm_cmplt_ps(candidate, previous);
    if (_mm_movemask_ps(better)) {
      __m128i lengths = _mm_add_epi32(_mm_set1_epi32(curr + dist), _mm_setr_epi32(0, 1, 2, 3));
      __m128i previous_lengths = _mm_loadu_si128((const __m128i*)&length_array[j + curr]);
      _mm_storeu_ps(&costs[j + curr], _mm_blendv_ps(previous, candidate, better));
      _mm_storeu_si128((__m128i*)&length_array[j + curr],
          _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(previous_lengths), _mm_castsi128_ps(lengths), better)));
    }
  }
}
#endif

// > happyarts-neon
#if defined(__aarch64__)
if (curr + 3 <= len) {
  const float32x4_t p = vdupq_n_f32(price2);
  uint32x4_t lengths = vaddq_u32(vdupq_n_u32(curr + dist), (uint32x4_t){0, 1, 2, 3});
  for (; curr + 3 <= len; curr += 4) {
    float32x4_t x = vaddq_f32(p, vld1q_f32(&litlentable[curr]));
    float32x4_t old = vld1q_f32(&costs[j + curr]);
    uint32x4_t better = vcltq_f32(x, old);
    if (vmaxvq_u32(better)) {
      vst1q_f32(&costs[j + curr], vbslq_f32(better, x, old));
      vst1q_u32(&length_array[j + curr], vbslq_u32(better, lengths, vld1q_u32(&length_array[j + curr])));
    }
    lengths = vaddq_u32(lengths, vdupq_n_u32(4));
  }
}
#endif

// > yumeyao-neon
#if defined(__aarch64__) && defined(USE_YUMEYAO_NEON)
for (; curr + 4 < len; curr += 4) {
  float32x4_t x4 = vaddq_f32(vdupq_n_f32(price2), vld1q_f32(&litlentable[curr]));
  float32x4_t vcost = vld1q_f32(&costs[j + curr]);
  vst1q_f32(&costs[j + curr], vminq_f32(x4, vcost));
  uint32x4_t vlength = vaddq_u32(vdupq_n_u32(curr + dist), (uint32x4_t){0, 1, 2, 3});
  uint32x4_t cmp_mask = vcltq_f32(x4, vcost);
  vlength = vbslq_u32(cmp_mask, vlength, vld1q_u32(&length_array[j + curr]));
  vst1q_u32(&length_array[j + curr], vlength);
}
#endif
