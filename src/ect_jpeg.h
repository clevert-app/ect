#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Returns an empty result if the original should be kept.
int ect_jpeg(const uint8_t *input, size_t size, int level, bool strip, std::vector<uint8_t> &best);
