#pragma once
#include <cstddef>
#include <cstdint>
int ect_png(const uint8_t *in_buf, size_t in_size, uint8_t **out_buf, size_t *out_size,
            unsigned mode, int filter_opt, bool strip, bool strict, unsigned mt);
